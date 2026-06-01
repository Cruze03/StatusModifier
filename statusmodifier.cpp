#include "statusmodifier.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include "vprof.h"
#include <stdio.h>

#include "protobuf/generated/cstrike15_usermessages.pb.h"

#include <iomanip>
#include <sstream>

#include "serversideclient.h"
#include "gameconfig.h"

StatusModifier g_StatusModifier;
PLUGIN_EXPOSE(StatusModifier, g_StatusModifier);
IVEngineServer2 *engine = nullptr;
CGameEntitySystem *g_pEntitySystem = nullptr;

CGameConfig *g_GameConfig = nullptr;

double g_flUniversalTime;
float g_flLastTickedTime;
bool g_bHasTicked;

std::unordered_set<int> g_mExcludeSlots;

void (*StatusPrintClient_t)(CServerSideClientBase *pPlayer, bool verbose,
							CBufferString *clients) = nullptr;

using namespace DynLibUtils;

funchook_t *m_StatusHook;

CGameEntitySystem *GameEntitySystem()
{
	static int offset = g_GameConfig->GetOffset("GameEntitySystem");
	return *reinterpret_cast<CGameEntitySystem **>((uintptr_t)(g_pGameResourceServiceServer) + offset);
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

void FASTCALL Hook_StatusPrintClient(CServerSideClientBase *pPlayer,
									 bool verbose, CBufferString *clients)
{
	if (!pPlayer || pPlayer->IsFakeClient() ||
		std::string(pPlayer->GetClientName()).empty())
	{
		StatusPrintClient_t(pPlayer, verbose, clients);
		return;
	}

	if (g_mExcludeSlots.find(pPlayer->GetPlayerSlot().Get()) !=
		g_mExcludeSlots.end())
	{
		return;
	}

	StatusPrintClient_t(pPlayer, verbose, clients);
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

	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService,
				SH_MEMBER(this, &StatusModifier::Hook_StartupServer), true);
	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server,
				SH_MEMBER(this, &StatusModifier::Hook_GameFrame), true);

	g_GameConfig = new CGameConfig();
	char conf_error[255] = "";
	if (!g_GameConfig->Init(conf_error, sizeof(conf_error)))
	{
		snprintf(error, maxlen, "Could not read %s: %s", g_GameConfig->GetPath().c_str(), conf_error);
		ErrorLog(error);
		return false;
	}

	CModule libengine(engine);

	const char *szSignature = g_GameConfig->GetSignature("StatusCommand");

	StatusPrintClient_t =
		libengine.FindPattern(szSignature).RCast<decltype(StatusPrintClient_t)>();
	if (!StatusPrintClient_t)
	{
		ErrorLog("[%s] Failed to find function to get StatusPrint of client",
				 g_PLAPI->GetLogTag());
		return false;
	}
	else
	{
		m_StatusHook = funchook_create();
		funchook_prepare(m_StatusHook, (void **)&StatusPrintClient_t,
						 (void *)Hook_StatusPrintClient);
		funchook_install(m_StatusHook, 0);
		ConMsg("[StatusModifier] StatusPrint of client hooked successfully.\n");
	}

	g_SMAPI->AddListener(this, this);

	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

	if (late)
	{
		g_pEntitySystem = GameEntitySystem();
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

	if (m_StatusHook)
		funchook_destroy(m_StatusHook);

	RemoveTimers();

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

CON_COMMAND_F(mm_excludeslot, "Exclude slot from status",
			  FCVAR_SERVER_CAN_EXECUTE)
{
	if (context.GetPlayerSlot() != -1)
		return;

	if (args.ArgC() < 2)
	{
		ConMsg("Usage: mm_excludeslot <slot>\n");
		return;
	}

	int slot = atoi(args.Arg(1));

	g_mExcludeSlots.insert(slot);
	ConMsg("[StatusModifier] Added slot %d to exclude list.\n", slot);
}

CON_COMMAND_F(mm_removeexcludeslot, "Remove exclusion from status",
			  FCVAR_SERVER_CAN_EXECUTE)
{
	if (context.GetPlayerSlot() != -1)
		return;

	if (args.ArgC() < 2)
	{
		ConMsg("Usage: mm_removeexcludeslot <slot>\n");
		return;
	}

	int slot = atoi(args.Arg(1));

	g_mExcludeSlots.erase(slot);
	ConMsg("[StatusModifier] Removed slot %d from exclude list.\n", slot);
}

CON_COMMAND_F(mm_listexcludeslots, "List exclusion from status",
			  FCVAR_SERVER_CAN_EXECUTE)
{
	if (context.GetPlayerSlot() != -1)
		return;

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
