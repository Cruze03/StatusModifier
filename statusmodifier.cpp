#include "statusmodifier.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

#include "cstrike15_usermessages.pb.h"
#include "usermessages.pb.h"
#include <chrono>
#include <fstream>
#include <inetchannel.h>
#include <iomanip>
#include <networksystem/inetworkmessages.h>
#include <networksystem/inetworkserializer.h>
#include <sstream>

#include "gameconfig.h"
#include "khook_helpers.h"
#include "playermanager.h"
#include "recipientfilter.h"
#include "utils.h"

StatusModifier g_StatusModifier;
PLUGIN_EXPOSE(StatusModifier, g_StatusModifier);
IVEngineServer2* g_pEngineServer2 = nullptr;
CGameEntitySystem* g_pEntitySystem = nullptr;
IGameEventSystem* g_pGameEventSystem = nullptr;

CGameConfig* g_GameConfig = nullptr;
CPlayerManager* g_PlayerManager = nullptr;
bool g_bRequiredInitLoaded = true;

std::unordered_set<int> g_mExcludeSlots;
std::vector<std::string> g_StatusArray;
std::string g_sServerIP;
std::chrono::time_point<std::chrono::steady_clock> g_chServerStartTime;
PipeLayout g_HeaderLayout;

CGameEntitySystem* GameEntitySystem()
{
    static int offset = g_GameConfig->GetOffset("GameEntitySystem");
    return *reinterpret_cast<CGameEntitySystem**>((uintptr_t)(g_pGameResourceServiceServer) + offset);
}

// Will return null between map end & new map startup, null check if necessary!
CGlobalVars* GetGlobals() { return g_pEngineServer2->GetServerGlobals(); }

KHOOK_VIRTUAL(StartupServer, &INetworkServerService::StartupServer, g_pNetworkServerService, nullptr, Hook_StartupServer_Post);
KHOOK_VIRTUAL(ClientConnect, &IServerGameClients::ClientConnect, g_pSource2GameClients, Hook_ClientConnect, nullptr);
KHOOK_VIRTUAL(OnClientConnected, &IServerGameClients::OnClientConnected, g_pSource2GameClients, Hook_OnClientConnected, nullptr);
KHOOK_VIRTUAL(ClientDisconnect, &IServerGameClients::ClientDisconnect, g_pSource2GameClients, nullptr, Hook_ClientDisconnect_Post);

KHOOK_MEMBER(FilterMessage, Detour_ServerSideClient_FilterMessage, nullptr);

std::vector<CKHookBase*>& GetKHookList()
{
    static std::vector<CKHookBase*> s_vecSigHooks;
    return s_vecSigHooks;
}

void InitKHooks()
{
    for (auto hook : GetKHookList())
        hook->Configure();
}

void InitModules()
{
    modules::engine = new CModule(ROOTBIN, "engine2");
    modules::server = new CModule(GAMEBIN, "server");
}

bool StatusModifier::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer2, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkSystem, INetworkSystem, NETWORKSYSTEM_INTERFACE_VERSION);

    g_GameConfig = new CGameConfig();
    char conf_error[255] = "";
    if (!g_GameConfig->Init(conf_error, sizeof(conf_error)))
    {
        snprintf(error, maxlen, "Could not read %s: %s", g_GameConfig->GetPath().c_str(), conf_error);
        ErrorLog(error);
        return false;
    }

    InitKHooks();

    if (!g_bRequiredInitLoaded)
    {
        snprintf(error, maxlen,
                 "One or more address lookups, patches or detours failed, please "
                 "refer to startup logs for more information");
        return false;
    }

    META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

    LoadConfig();

    g_SMAPI->AddListener(this, this);

    g_PlayerManager = new CPlayerManager();

    g_chServerStartTime = std::chrono::steady_clock::now();

    if (late)
    {
        g_pEntitySystem = GameEntitySystem();
        g_PlayerManager->OnLateLoad();
    }

    return true;
}

bool StatusModifier::Unload(char* error, size_t maxlen)
{
    ConVar_Unregister();

    if (g_PlayerManager) delete g_PlayerManager;

    return true;
}

KHook::Return<bool> Detour_ServerSideClient_FilterMessage(CServerSideClientBase* pClient, const CNetMessage* pMessage, INetChannel* pChannel)
{
    if (!pClient || !pMessage)
    {
        return { KHook::Action::Ignore, false };
    }

    int msgid = pMessage->GetNetMessage()->GetNetMessageInfo()->m_MessageId;
    static int playerIndex = g_GameConfig->GetOffset("CServerSideClientBase::m_nClientSlot") - WIN_LINUX(8, 48);
    int slot = *(int*)((uintptr_t)pClient + playerIndex);

    if (msgid != clc_ServerStatus)
    {
        return { KHook::Action::Ignore, false };
    }

    bool playerlist = false;
    std::string buffer;
    for (const std::string& message : g_StatusArray)
    {
        if (message.rfind("#HEADER#", 0) == 0)
        {
            ClientPrint(slot, HUD_PRINTCONSOLE, message.c_str() + 8);
            continue;
        }

        if (!playerlist && message.find("{PLAYER") != std::string::npos && message.find("{PLAYERC") == std::string::npos)
        {
            for (int i = 0; i < MAXPLAYERS; i++)
            {
                if (g_mExcludeSlots.find(i) != g_mExcludeSlots.end()) continue;

                buffer = CheckMessageVariables(message, i, g_HeaderLayout);
                if (buffer.length() > 0) ClientPrint(slot, HUD_PRINTCONSOLE, buffer.c_str());
            }
            playerlist = true;
            continue;
        }

        buffer = CheckMessageVariables(message, -1, g_HeaderLayout);
        if (buffer.length() > 0) ClientPrint(slot, HUD_PRINTCONSOLE, buffer.c_str());
    }
    return { KHook::Action::Supersede, true };
}

KHook::Return<void>
Hook_StartupServer_Post(INetworkServerService* pThis, const GameSessionConfiguration_t& config, ISource2WorldSession* pSession, const char* pszMapName)
{
    g_pEntitySystem = GameEntitySystem();
    g_mExcludeSlots.clear();
    return { KHook::Action::Ignore };
}

KHook::Return<bool> Hook_ClientConnect(
    IServerGameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, bool unk1, CBufferString* pRejectReason)
{
    // Player is banned
    if (!g_PlayerManager->OnClientConnected(slot, xuid, pszNetworkID)) return { KHook::Action::Ignore, false };

    return { KHook::Action::Ignore, true };
}

KHook::Return<void> Hook_OnClientConnected(
    IServerGameClients* pThis, CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
{
    static ConVarRefAbstract tv_name("tv_name");
    const char* pszTvName = tv_name.GetString().Get();

    // Ideally we would use CServerSideClient::IsHLTV().. but it doesn't work :(
    if (bFakePlayer && V_strcmp(pszName, pszTvName)) g_PlayerManager->OnBotConnected(slot);
    return { KHook::Action::Ignore };
}

KHook::Return<void> Hook_ClientDisconnect_Post(
    IServerGameClients* pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* pszName, uint64 xuid, const char* pszNetworkID)
{
    g_PlayerManager->OnClientDisconnect(slot);
    return { KHook::Action::Ignore };
}

void LoadConfig()
{
    const char* pszConfigPath = "addons/statusmodifier/configs/statusmodifier.txt";
    char szPath[MAX_PATH];
    V_snprintf(szPath, sizeof(szPath), "%s%s%s", Plat_GetGameDirectory(), "/csgo/", pszConfigPath);

    std::ifstream file(szPath);
    if (!file.is_open()) return;

    std::string line;
    bool header_found = false;
    while (std::getline(file, line))
    {
        TrimString(line);

        if (line.empty()) continue;

        // Skip comment lines
        if (line.find("//") != std::string::npos) continue;

        if (line.rfind("#HEADER#", 0) == 0)
        {
            header_found = true;
            std::string_view headerView(line);
            headerView.remove_prefix(8);
            g_HeaderLayout = ParseHeaderLayout(headerView);
        }

        g_StatusArray.push_back(line);
    }

    if (!header_found)
    {
        ErrorLog("`#HEADER` Identifier not found. Plugin will not work properly!");
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

PipeLayout ParseHeaderLayout(std::string_view header)
{
    PipeLayout layout;
    layout.header = header;
    layout.pipePositions.push_back(0);
    for (size_t i = 0; i < header.size(); i++)
        if (header[i] == '|') layout.pipePositions.push_back(i);
    return layout;
}

std::string ApplyReplacements(const std::string& input, const std::vector<std::pair<std::string, std::string>>& replacements)
{
    std::string result;
    result.reserve(input.size() * 2);
    size_t i = 0;

    while (i < input.size())
    {
        bool matched = false;
        for (auto& [token, value] : replacements)
        {
            if (input.compare(i, token.size(), token) == 0)
            {
                result += value;
                i += token.size();
                matched = true;
                break;
            }
        }
        if (!matched) result += input[i++];
    }
    return result;
}

std::string BuildAlignedRow(std::string_view prefix, const std::vector<std::string>& values, const PipeLayout& layout)
{
    std::string out(prefix);

    for (size_t i = 0; i < values.size(); i++)
    {
        if (i >= layout.pipePositions.size()) break;

        size_t target = layout.pipePositions[i] + 2;

        for (size_t offset = 0; offset < 4 && target + offset < layout.header.size(); offset++)
        {
            if (layout.header[target + offset] != ' ')
            {
                target += offset;
                break;
            }

            if (offset == 3) target += 4;
        }

        if (out.size() > target) out += ' ';
        else
            out.resize(target, ' ');

        out += values[i];
    }

    return out;
}

std::string CheckMessageVariables(const std::string& message, int slot, const PipeLayout& layout)
{
    std::string sMessage = message;

    // {SERVERIP}
    if (sMessage.find("{SERVERIP}") != std::string::npos) ReplaceAll(sMessage, "{SERVERIP}", GetPublicIP());

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
    if (sMessage.find("{PLAYERCOUNT}") != std::string::npos) ReplaceAll(sMessage, "{PLAYERCOUNT}", std::to_string(g_PlayerManager->GetPlayerCount()));

    // {MAXPLAYERS}
    if (sMessage.find("{MAXPLAYERS}") != std::string::npos)
    {
        static ConVarRefAbstract visiblemaxplayers("sv_visiblemaxplayers");

        if (GetGlobals())
        {
            ReplaceAll(
                sMessage, "{MAXPLAYERS}",
                std::to_string(!visiblemaxplayers.IsValidRef() || visiblemaxplayers.GetInt() == -1 ? GetGlobals()->maxClients : visiblemaxplayers.GetInt()));
        }
    }

    if (slot > -1)
    {
        Player* pPlayer = g_PlayerManager->GetPlayer(slot);

        if (!pPlayer || !pPlayer->IsConnected()) return "";

        auto pController = CCSPlayerController::FromSlot(slot);
        if (!pController) return "";

        // Strip spaces/tabs from token line (keep prefix "# ")
        int idx = 0;
        if (sMessage.size() > 1 && (sMessage[1] == ' ' || sMessage[1] == '\t')) idx = 2;
        sMessage.erase(std::remove_if(sMessage.begin() + idx, sMessage.end(),
                                      [](unsigned char c)
        {
            return c == ' ' || c == '\t';
        }),
                       sMessage.end());

        std::vector<std::string> values(layout.pipePositions.size());

        auto set = [&](int col, std::string val)
        {
            if (col < (int)values.size()) values[col] = std::move(val);
        };

        bool fake = pPlayer->IsFakeClient();

        int column = 0;

        if (sMessage.find("{PLAYERUSERID}") != std::string::npos) set(column++, std::to_string(g_pEngineServer2->GetPlayerUserId(slot).Get()));

        if (sMessage.find("{PLAYERNAME}") != std::string::npos)
        {
            std::string name = pController->GetPlayerName();
            StripUnicode(name);

            size_t length = name.size();

            if (length == 0)
            {
                name = "Player Unknown";
                length = name.size();
            }

            if (length > 20)
            {
                name.resize(20);
                length = 20;
            }
            set(column++, std::move(name));
        }

        if (sMessage.find("{STEAM32}") != std::string::npos) set(column++, fake ? "BOT" : pPlayer->GetSteam2Id());

        if (sMessage.find("{STEAMID}") != std::string::npos) set(column++, fake ? "BOT" : std::to_string(pPlayer->GetSteamId64()));

        if (sMessage.find("{STEAM3}") != std::string::npos) set(column++, fake ? "BOT" : pPlayer->GetSteam3Id());

        if (sMessage.find("{PLAYERIP}") != std::string::npos) set(column++, fake ? "BOT" : pPlayer->GetIpAddress());

        if (sMessage.find("{PLAYERTIME}") != std::string::npos && GetGlobals())
        {
            INetChannelInfo* pInfo = g_pEngineServer2->GetPlayerNetInfo(slot);
            set(column++, pInfo ? FormatShortTime((int)pInfo->GetTimeConnected()) : "0s");
        }

        if (sMessage.find("{PLAYERPING}") != std::string::npos) set(column++, std::to_string(pController->m_iPing()));

        if (sMessage.find("{PLAYERSCORE}") != std::string::npos) set(column++, std::to_string(pController->m_iScore()));

        sMessage = BuildAlignedRow("# ", values, layout);
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
    // convar 	ReplaceAll(sMessage, "{NEXTMAP}", nextmap ? nextmap->GetString()
    // :
    // "");
    // }

    return sMessage;
}
