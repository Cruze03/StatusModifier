#include "statusmodifier.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include "vprof.h"
#include <stdio.h>

#include <inetchannel.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/inetworkserializer.h>

#include "protobuf/generated/cstrike15_usermessages.pb.h"
#include "protobuf/generated/usermessages.pb.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>

#include "gameconfig.h"
#include "playermanager.h"
#include "recipientfilter.h"
#include "serversideclient.h"

StatusModifier g_StatusModifier;
PLUGIN_EXPOSE(StatusModifier, g_StatusModifier);
IVEngineServer2 *engine = nullptr;
CGameEntitySystem *g_pEntitySystem = nullptr;

CGameConfig *g_GameConfig = nullptr;

IGameEventSystem *g_gameEventSystem = nullptr;

double g_flUniversalTime;
float g_flLastTickedTime;
bool g_bHasTicked;

std::unordered_set<int> g_mExcludeSlots;

void (*StatusFullPrintClient_t)(CNetworkGameServerBase *pBase,
                                int slot) = nullptr;

using namespace DynLibUtils;

funchook_t *m_StatusFullHook;

std::vector<std::string> g_StatusArray;

CPlayerManager *g_playerManager = nullptr;
ISteamGameServer *g_pGameServer = nullptr;
CSteamGameServerAPIContext steamctx;
std::string g_sServerIP;
std::chrono::time_point<std::chrono::steady_clock> g_chServerStartTime;

CGameEntitySystem *GameEntitySystem()
{
  static int offset = g_GameConfig->GetOffset("GameEntitySystem");
  return *reinterpret_cast<CGameEntitySystem **>(
      (uintptr_t)(g_pGameResourceServiceServer) + offset);
}

// Will return null between map end & new map startup, null check if necessary!
CGlobalVars *GetGlobals() { return engine->GetServerGlobals(); }

class GameSessionConfiguration_t
{
};

SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0,
                   const GameSessionConfiguration_t &, ISource2WorldSession *,
                   const char *);
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0,
                   CPlayerSlot, ENetworkDisconnectionReason, const char *,
                   uint64, const char *);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0,
                   CPlayerSlot, const char *, uint64, const char *,
                   const char *, bool);
SH_DECL_HOOK6(IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool,
              CPlayerSlot, const char *, uint64, const char *, bool,
              CBufferString *);

#define HUD_PRINTCONSOLE 2

void ClientPrint(CPlayerSlot slot, int hud_dest, const char *msg, ...)
{
  va_list args;
  va_start(args, msg);

  char buf[256];
  V_vsnprintf(buf, sizeof(buf), msg, args);

  va_end(args);

  INetworkMessageInternal *pNetMsg =
      g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
  auto data = pNetMsg->AllocateMessage()->ToPB<CUserMessageTextMsg>();

  data->set_dest(hud_dest);
  data->add_param(buf);

  CSingleRecipientFilter filter(slot);

  g_gameEventSystem->PostEventAbstract(-1, false, &filter, pNetMsg, data, 0);

  delete data;
}

void FASTCALL Hook_StatusFullPrintClient(CNetworkGameServerBase *pBase,
                                         int slot)
{
  // StatusFullPrintClient_t(pBase, slot);

  bool playerlist = false;
  std::string buffer;
  for (std::string message : g_StatusArray)
  {
    if (message.find("{PLAYERUSERID}") != std::string::npos)
    {
      if (playerlist)
        continue;

      for (int i = 0; i < MAXPLAYERS; i++)
      {
        if (g_mExcludeSlots.find(i) != g_mExcludeSlots.end())
          continue;

        buffer = CheckMessageVariables(message, i);
        if (buffer.length() > 0)
          ClientPrint(CPlayerSlot(slot), HUD_PRINTCONSOLE, buffer.c_str());
      }
      playerlist = true;
      continue;
    }

    message = CheckMessageVariables(message);
    ClientPrint(CPlayerSlot(slot), HUD_PRINTCONSOLE, message.c_str());
  }
}

bool StatusModifier::Load(PluginId id, ISmmAPI *ismm, char *error,
                          size_t maxlen, bool late)
{
  PLUGIN_SAVEVARS();

  GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2,
                      SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
  GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem,
                      FILESYSTEM_INTERFACE_VERSION);
  GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer,
                      IGameResourceService,
                      GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem,
                  SCHEMASYSTEM_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server,
                  SOURCE2SERVER_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService,
                  INetworkServerService,
                  NETWORKSERVERSERVICE_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages,
                  NETWORKMESSAGES_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetEngineFactory, g_gameEventSystem, IGameEventSystem,
                  GAMEEVENTSYSTEM_INTERFACE_VERSION);
  GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients,
                  SOURCE2GAMECLIENTS_INTERFACE_VERSION);

  SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService,
              SH_MEMBER(this, &StatusModifier::Hook_StartupServer), true);
  SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server,
              SH_MEMBER(this, &StatusModifier::Hook_GameFrame), true);
  SH_ADD_HOOK(
      IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server,
      SH_MEMBER(this, &StatusModifier::Hook_GameServerSteamAPIActivated),
      false);
  SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pSource2GameClients,
              SH_MEMBER(this, &StatusModifier::Hook_ClientDisconnect), true);
  SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_pSource2GameClients,
              SH_MEMBER(this, &StatusModifier::Hook_OnClientConnected), false);
  SH_ADD_HOOK(IServerGameClients, ClientConnect, g_pSource2GameClients,
              SH_MEMBER(this, &StatusModifier::Hook_ClientConnect), false);

  g_GameConfig = new CGameConfig();
  char conf_error[255] = "";
  if (!g_GameConfig->Init(conf_error, sizeof(conf_error)))
  {
    snprintf(error, maxlen, "Could not read %s: %s",
             g_GameConfig->GetPath().c_str(), conf_error);
    ErrorLog(error);
    return false;
  }

  CModule libengine(engine);

  const char *szSignature = g_GameConfig->GetSignature("StatusCommandFull");

  StatusFullPrintClient_t = libengine.FindPattern(szSignature)
                                .RCast<decltype(StatusFullPrintClient_t)>();
  if (!StatusFullPrintClient_t)
  {
    ErrorLog("[%s] Failed to find function to get StatusPrintFull of client",
             g_PLAPI->GetLogTag());
    return false;
  }
  else
  {
    m_StatusFullHook = funchook_create();
    funchook_prepare(m_StatusFullHook, (void **)&StatusFullPrintClient_t,
                     (void *)Hook_StatusFullPrintClient);
    funchook_install(m_StatusFullHook, 0);
    ConMsg("[StatusModifier] StatusPrintFull of client hooked successfully.\n");
  }

  META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

  LoadConfig();

  g_SMAPI->AddListener(this, this);

  g_playerManager = new CPlayerManager();

  g_chServerStartTime = std::chrono::steady_clock::now();

  if (late)
  {
    g_pEntitySystem = GameEntitySystem();
    g_playerManager->OnLateLoad();
    Hook_GameServerSteamAPIActivated();
  }

  return true;
}

bool StatusModifier::Unload(char *error, size_t maxlen)
{
  ConVar_Unregister();

  SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService,
                 SH_MEMBER(this, &StatusModifier::Hook_StartupServer), true);
  SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pSource2Server,
                 SH_MEMBER(this, &StatusModifier::Hook_GameFrame), true);
  SH_REMOVE_HOOK(
      IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server,
      SH_MEMBER(this, &StatusModifier::Hook_GameServerSteamAPIActivated),
      false);
  SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pSource2GameClients,
                 SH_MEMBER(this, &StatusModifier::Hook_ClientDisconnect), true);
  SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_pSource2GameClients,
                 SH_MEMBER(this, &StatusModifier::Hook_OnClientConnected),
                 false);
  SH_REMOVE_HOOK(IServerGameClients, ClientConnect, g_pSource2GameClients,
                 SH_MEMBER(this, &StatusModifier::Hook_ClientConnect), false);

  if (m_StatusFullHook)
    funchook_destroy(m_StatusFullHook);

  RemoveTimers();

  if (g_playerManager)
    delete g_playerManager;

  return true;
}

void StatusModifier::Hook_StartupServer(
    const GameSessionConfiguration_t &config, ISource2WorldSession *pSession,
    const char *pszMapName)
{
  g_pEntitySystem = GameEntitySystem();
  g_bHasTicked = false;

  g_mExcludeSlots.clear();
}

void StatusModifier::Hook_GameFrame(bool simulating, bool bFirstTick,
                                    bool bLastTick)
{
  /**
   * simulating:
   * ***********
   * true  | game is ticking
   * false | game is not ticking
   */

  VPROF_BUDGET("StatusModifier::Hook_GameFramePost", "StatusModifierPerFrame");

  if (!GetGlobals())
    return;

  if (simulating && g_bHasTicked)
  {
    g_flUniversalTime += GetGlobals()->curtime - g_flLastTickedTime;
  }

  g_flLastTickedTime = GetGlobals()->curtime;
  g_bHasTicked = true;

  for (int i = g_timers.Tail(); i != g_timers.InvalidIndex();)
  {
    auto timer = g_timers[i];

    int prevIndex = i;
    i = g_timers.Previous(i);

    if (timer->m_flLastExecute == -1)
      timer->m_flLastExecute = g_flUniversalTime;

    // Timer execute
    if (timer->m_flLastExecute + timer->m_flInterval <= g_flUniversalTime)
    {
      if (!timer->Execute())
      {
        delete timer;
        g_timers.Remove(prevIndex);
      }
      else
      {
        timer->m_flLastExecute = g_flUniversalTime;
      }
    }
  }
}

void StatusModifier::Hook_GameServerSteamAPIActivated()
{
  steamctx.Init();

  if (!GetPublicIP())
  {
    g_sServerIP = "Unknown";
    // clang-format off
		new CTimer(5.0f, []()
		{
			if(GetPublicIP())
			{
				return -1.0f;
			}
			return 5.0f;
		});
    // clang-format on
  }

  RETURN_META(MRES_IGNORED);
}

void StatusModifier::Hook_OnClientConnected(CPlayerSlot slot,
                                            const char *pszName, uint64 xuid,
                                            const char *pszNetworkID,
                                            const char *pszAddress,
                                            bool bFakePlayer)
{
  static ConVarRefAbstract tv_name("tv_name");
  const char *pszTvName = tv_name.GetString().Get();

  // Ideally we would use CServerSideClient::IsHLTV().. but it doesn't work :(
  if (bFakePlayer && V_strcmp(pszName, pszTvName))
    g_playerManager->OnBotConnected(slot);
}

bool StatusModifier::Hook_ClientConnect(CPlayerSlot slot, const char *pszName,
                                        uint64 xuid, const char *pszNetworkID,
                                        bool unk1,
                                        CBufferString *pRejectReason)
{
  // Player is banned
  if (!g_playerManager->OnClientConnected(slot, xuid, pszNetworkID))
    RETURN_META_VALUE(MRES_SUPERCEDE, false);

  RETURN_META_VALUE(MRES_IGNORED, true);
}

void StatusModifier::Hook_ClientDisconnect(CPlayerSlot slot,
                                           ENetworkDisconnectionReason reason,
                                           const char *pszName, uint64 xuid,
                                           const char *pszNetworkID)
{
  g_playerManager->OnClientDisconnect(slot);
}

void LoadConfig()
{
  const char *pszConfigPath =
      "addons/statusmodifier/configs/statusmodifier.txt";
  char szPath[MAX_PATH];
  V_snprintf(szPath, sizeof(szPath), "%s%s%s", Plat_GetGameDirectory(),
             "/csgo/", pszConfigPath);

  std::ifstream file(szPath);
  if (!file.is_open())
    return;

  std::string line;
  while (std::getline(file, line))
  {
    TrimString(line);

    if (line.empty())
      continue;

    // Skip comment lines
    if (line.find("//") != std::string::npos)
      continue;

    g_StatusArray.push_back(line);
  }

  file.close();
}

CON_COMMAND_F(mm_excludeslot, "Exclude slot from status", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
  if (args.ArgC() < 2)
  {
    ConMsg("Usage: mm_excludeslot <slot>\n");
    return;
  }

  int slot = atoi(args.Arg(1));

  g_mExcludeSlots.insert(slot);
  ConMsg("[StatusModifier] Added slot %d to exclude list.\n", slot);
}

CON_COMMAND_F(mm_removeexcludeslot, "Remove exclusion from status", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
  if (args.ArgC() < 2)
  {
    ConMsg("Usage: mm_removeexcludeslot <slot>\n");
    return;
  }

  int slot = atoi(args.Arg(1));

  g_mExcludeSlots.erase(slot);
  ConMsg("[StatusModifier] Removed slot %d from exclude list.\n", slot);
}

CON_COMMAND_F(mm_listexcludeslots, "List exclusion from status", FCVAR_SPONLY | FCVAR_LINKED_CONCOMMAND)
{
  if (g_mExcludeSlots.size() == 0)
  {
    ConMsg("No slots are excluded.\n");
    return;
  }

  ConMsg("List of slots excluded:\n");
  for (int slot : g_mExcludeSlots)
  {
    ConMsg("%i\n", slot);
  }
}

std::string formatCurrentTime()
{
  std::time_t currentTime = std::time(nullptr);
  std::tm *localTime = std::localtime(&currentTime);
  std::ostringstream formattedTime;
  formattedTime << std::put_time(localTime, "%m/%d/%Y - %H:%M:%S");
  return formattedTime.str();
}

std::string formatCurrentTime2()
{
  std::time_t currentTime = std::time(nullptr);
  std::tm *localTime = std::localtime(&currentTime);
  std::ostringstream formattedTime;
  formattedTime << std::put_time(localTime, "error_%m-%d-%Y");
  return formattedTime.str();
}

void TrimString(std::string &s)
{
  // Left trim
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                  { return !std::isspace(c); }));
  // Right trim
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char c)
                       { return !std::isspace(c); })
              .base(),
          s.end());
}

static void ReplaceAll(std::string &str, const std::string &from,
                       const std::string &to)
{
  // Case-insensitive search
  std::string lstr = str, lfrom = from;
  std::transform(lstr.begin(), lstr.end(), lstr.begin(), ::tolower);
  std::transform(lfrom.begin(), lfrom.end(), lfrom.begin(), ::tolower);

  size_t pos = 0;
  while ((pos = lstr.find(lfrom, pos)) != std::string::npos)
  {
    str.replace(pos, from.size(), to);
    lstr.replace(pos, lfrom.size(), to);
    pos += to.size();
  }
}

bool GetPublicIP()
{
  g_pGameServer = SteamGameServer();

  if (g_pGameServer)
  {
    SteamIPAddress_t sAddr = g_pGameServer->GetPublicIP();

    int hostport = CommandLine()->ParmValue("-port", 0);

    if (!sAddr.IsSet() || hostport == 0)
    {
      ConMsg("Server IP / port not set. Retrying...\n");
      return false;
    }
    uint32_t ipaddr = sAddr.m_unIPv4;
    uint32_t ip[4];

    for (char iter = 3; iter > -1; --iter)
    {
      ip[(~iter) & 0x03] = (static_cast<unsigned char>(ipaddr >> (iter * 8)) &
                            0xFF); /* I hate you; SteamTools. */
    }
    char buf[64];
    sprintf(buf, "%i.%i.%i.%i:%i", ip[0], ip[1], ip[2], ip[3], hostport);

    g_sServerIP = buf;

    // ConMsg("Server IP: %s\n", buf);
    return true;
  }
  ConMsg("ISteamGameServer not valid. Retrying...\n");
  return false;
}

template <typename T>
std::string PadRight(const T &value, size_t width)
{
  std::ostringstream ss;
  ss << std::left << std::setw(width) << value;
  return ss.str();
}

std::string CheckMessageVariables(const std::string &message, int slot)
{
  std::string sMessage = message;

  // {SERVER_IP}
  if (sMessage.find("{SERVERIP}") != std::string::npos)
    ReplaceAll(sMessage, "{SERVERIP}", g_sServerIP.c_str());

  // {SERVERNAME}
  if (sMessage.find("{SERVERNAME}") != std::string::npos)
  {
    // g_pCVar face is needed for this
    static ConVarRefAbstract hostname("hostname");

    if (hostname.IsValidRef())
    {
      ReplaceAll(sMessage, "{SERVERNAME}", hostname.GetString().Get());
    }
  }

  // {CURRENTMAP}
  if (sMessage.find("{CURRENTMAP}") != std::string::npos)
  {
    if (GetGlobals())
    {
      ReplaceAll(sMessage, "{CURRENTMAP}", GetGlobals()->mapname.ToCStr());
    }
  }

  // {PLAYERCOUNT}
  if (sMessage.find("{PLAYERCOUNT}") != std::string::npos)
    ReplaceAll(sMessage, "{PLAYERCOUNT}",
               std::to_string(g_playerManager->GetPlayerCount()));

  // {MAXPLAYERS}
  if (sMessage.find("{MAXPLAYERS}") != std::string::npos)
  {
    static ConVarRefAbstract visiblemaxplayers("sv_visiblemaxplayers");

    if (GetGlobals())
    {
      ReplaceAll(sMessage, "{MAXPLAYERS}",
                 std::to_string(!visiblemaxplayers.IsValidRef() ||
                                        visiblemaxplayers.GetInt() == -1
                                    ? GetGlobals()->maxClients
                                    : visiblemaxplayers.GetInt()));
    }
  }

  if (slot > -1)
  {
    Player *pPlayer = g_playerManager->GetPlayer(slot);

    if (pPlayer && pPlayer->IsConnected())
    {
      auto pController = CCSPlayerController::FromSlot(slot);

      if (pController)
      {
        constexpr size_t kIntPadding = 4;
        constexpr size_t kSteamPadding = 17;
        constexpr size_t kIpPadding = 17;
        constexpr size_t kTimePadding = 7;

        // {PLAYERUSERID}
        if (sMessage.find("{PLAYERUSERID}") != std::string::npos)
          ReplaceAll(
              sMessage, "{PLAYERUSERID}",
              PadRight(engine->GetPlayerUserId(CPlayerSlot(slot)).Get(), kIntPadding));

        // {PLAYERNAME}
        if (sMessage.find("{PLAYERNAME}") != std::string::npos)
        {
          std::string name = pController->GetPlayerName();

          constexpr size_t kMaxNameLength = 20;
          constexpr size_t kMaxPadding = 16;
          constexpr size_t kPadding = 10;

          if (name.length() > kMaxNameLength)
            name.resize(kMaxNameLength);

          ReplaceAll(
              sMessage, "{PLAYERNAME}",
              PadRight(name, std::min<size_t>(kMaxPadding,
                                              name.length() + kPadding)));
        }

        // {PLAYERSCORE}
        if (sMessage.find("{PLAYERSCORE}") != std::string::npos)
          ReplaceAll(sMessage, "{PLAYERSCORE}", PadRight(pController->m_iScore(), kIntPadding));

        // {STEAMID}
        if (sMessage.find("{STEAMID}") != std::string::npos)
        {
          ReplaceAll(
              sMessage, "{STEAMID}",
              PadRight(pPlayer->IsFakeClient() ? "BOT" : std::to_string(pPlayer->GetSteamId64()),
                       kSteamPadding));
        }

        // {STEAM32}
        if (sMessage.find("{STEAM32}") != std::string::npos)
        {
          ReplaceAll(
              sMessage, "{STEAM32}",
              PadRight(pPlayer->IsFakeClient() ? "BOT" : pPlayer->GetSteam2Id(),
                       kSteamPadding));
        }

        // {STEAM3}
        if (sMessage.find("{STEAM3}") != std::string::npos)
        {
          ReplaceAll(
              sMessage, "{STEAM3}",
              PadRight(pPlayer->IsFakeClient() ? "BOT" : pPlayer->GetSteam3Id(),
                       kSteamPadding));
        }

        // {PLAYERIP}
        if (sMessage.find("{PLAYERIP}") != std::string::npos)
        {
          ReplaceAll(
              sMessage, "{PLAYERIP}",
              PadRight(pPlayer->IsFakeClient() ? "BOT" : pPlayer->GetIpAddress(),
                       kIpPadding));
        }

        // {PLAYERTIME}
        if (sMessage.find("{PLAYERTIME}") != std::string::npos &&
            GetGlobals())
        {
          INetChannelInfo *pInfo = engine->GetPlayerNetInfo(slot);
          ReplaceAll(sMessage, "{PLAYERTIME}",
                     PadRight(pInfo ? FormatShortTime(static_cast<int>(
                                          pInfo->GetTimeConnected()))
                                    : "0s",
                              kTimePadding));
        }

        // {PLAYERPING}
        if (sMessage.find("{PLAYERPING}") != std::string::npos)
          ReplaceAll(sMessage, "{PLAYERPING}",
                     PadRight(pController->m_iPing(), kIntPadding));
      }
      else
        return "";
    }
    else
      return "";
  }

  // {CURRENTDATE}
  if (sMessage.find("{CURRENTDATE}") != std::string::npos)
  {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y", localtime(&now));
    ReplaceAll(sMessage, "{CURRENTDATE}", buf);
  }

  // {CURRENTTIME}
  if (sMessage.find("{CURRENTTIME}") != std::string::npos)
  {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
    ReplaceAll(sMessage, "{CURRENTTIME}", buf);
  }

  // {CURRENTDATETIME}
  if (sMessage.find("{CURRENTDATETIME}") != std::string::npos)
  {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", localtime(&now));
    ReplaceAll(sMessage, "{CURRENTDATETIME}", buf);
  }

  // {UPTIME}
  if (sMessage.find("{UPTIME}") != std::string::npos)
  {
    auto uptime = std::chrono::steady_clock::now() - g_chServerStartTime;
    int seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
    ReplaceAll(sMessage, "{UPTIME}", FormatShortTime(seconds));
  }

  // // {NEXTMAP} — no HL2SDK native; use your own tracking or engine string
  // if (sMessage.find("{NEXTMAP}") != std::string::npos)
  // {
  // 	ConVar *nextmap = g_pCVar->FindVar("sm_nextmap"); // or your nextmap
  // convar 	ReplaceAll(sMessage, "{NEXTMAP}", nextmap ? nextmap->GetString() :
  // "");
  // }

  return sMessage;
}

std::string FormatShortTime(int seconds)
{
  if (seconds < 0)
    seconds = 0;

  int days = seconds / 86400;
  seconds %= 86400;

  int hours = seconds / 3600;
  seconds %= 3600;

  int minutes = seconds / 60;
  seconds %= 60;

  char buffer[64];

  if (days > 0)
  {
    snprintf(buffer, sizeof(buffer), "%dd %dh %dm", days, hours, minutes);
  }
  else if (hours > 0)
  {
    snprintf(buffer, sizeof(buffer), "%dh %dm", hours, minutes);
  }
  else if (minutes > 0)
  {
    snprintf(buffer, sizeof(buffer), "%dm %ds", minutes, seconds);
  }
  else
  {
    snprintf(buffer, sizeof(buffer), "%ds", seconds);
  }

  return buffer;
}

void ErrorLog(const char *msg, ...)
{
  va_list args;
  va_start(args, msg);

  char buf[1024];
  V_vsnprintf(buf, sizeof(buf), msg, args);
  va_end(args);

  ConColorMsg(Color(255, 0, 0, 255), "[Error] %s\n", buf);

  char szPath[256], szBuffer[2048];
  g_SMAPI->PathFormat(szPath, sizeof(szPath),
                      "%s/addons/logs/status-modifier-%s.txt",
                      g_SMAPI->GetBaseDir(), formatCurrentTime2().c_str());
  g_SMAPI->Format(szBuffer, sizeof(szBuffer), "L %s: %s\n",
                  formatCurrentTime().c_str(), buf);

  FILE *pFile = fopen(szPath, "a");
  if (pFile)
  {
    fputs(szBuffer, pFile);
    fclose(pFile);
  }
}
