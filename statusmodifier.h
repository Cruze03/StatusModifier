#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <sh_vector.h>
#include "utlvector.h"
#include "ehandle.h"
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "engine/igameeventsystem.h"

#include "igameevents.h"
#include "CBaseEntity.h"
#include "CCSPlayerController.h"
#include "CCSPlayerPawn.h"

#include "convar.h"
#include "module.h"
#include "ctimer.h"
#include "funchook.h"
#include "bitvec.h"
#include "serversideclient.h"

#ifdef _WIN32
#define ROOTBIN "/bin/win64/"
#define GAMEBIN "/csgo/bin/win64/"
#else
#define ROOTBIN "/bin/linuxsteamrt64/"
#define GAMEBIN "/csgo/bin/linuxsteamrt64/"
#endif

#ifdef AMBUILD
#include "version_gen.h"
#else
#include "version_gen_placeholder.h"
#endif

#define MAXPLAYERS 65

class StatusModifier final : public ISmmPlugin, public IMetamodListener
{
public:
    bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
    bool Unload(char *error, size_t maxlen);

    void Hook_StartupServer(const GameSessionConfiguration_t &config, ISource2WorldSession *pSession, const char *pszMapName);
    void Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, const char *pszAddress, bool bFakePlayer);
    bool Hook_ClientConnect(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1, CBufferString *pRejectReason);
    void Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName, uint64 xuid, const char *pszNetworkID);

public:
    const char *GetAuthor() { return PLUGIN_AUTHOR; }
    const char *GetName() { return PLUGIN_DISPLAY_NAME; }
    const char *GetDescription() { return PLUGIN_DESCRIPTION; }
    const char *GetURL() { return PLUGIN_URL; }
    const char *GetLicense() { return PLUGIN_LICENSE; }
    const char *GetVersion() { return PLUGIN_FULL_VERSION; }
    const char *GetDate() { return __DATE__; }
    const char *GetLogTag() { return PLUGIN_LOGTAG; }
};

void RegisterEventListeners();
void UnregisterEventListeners();
void PrintToChatAll(const char *msg, ...);
uint32 GetSoundEventHash(const char *pszSoundEventName);
std::string FormatCurrentTime();
std::string FormatCurrentTime2();
void ErrorLog(const char *msg, ...);
void LoadConfig();
void TrimString(std::string &s);
static void ReplaceAll(std::string &str, const std::string &from, const std::string &to);
struct PipeLayout
{
    std::vector<size_t> pipePositions;
    std::string header;
};
std::string CheckMessageVariables(const std::string &message, int slot,
                                  const PipeLayout &layout);
PipeLayout ParseHeaderLayout(std::string_view header);
std::string FormatShortTime(int seconds);
const char *GetPublicIP();

template <typename T>
std::string PadRight(const T &value, size_t width);
std::vector<std::string_view> SplitString(std::string_view str, std::string_view delim);

const std::string colors_text[] = {
    "{DEFAULT}",
    "{WHITE}",
    "{RED}",
    "{LIGHTPURPLE}",
    "{GREEN}",
    "{LIME}",
    "{LIGHTGREEN}",
    "{DARKRED}",
    "{GRAY}",
    "{LIGHTOLIVE}",
    "{OLIVE}",
    "{LIGHTBLUE}",
    "{BLUE}",
    "{PURPLE}",
    "{LIGHTRED}",
    "{GRAYBLUE}",
    "\\n"};

const std::string colors_hex[] = {
    "\x01",
    "\x01",
    "\x02",
    "\x03",
    "\x04",
    "\x05",
    "\x06",
    "\x07",
    "\x08",
    "\x09",
    "\x10",
    "\x0B",
    "\x0C",
    "\x0E",
    "\x0F",
    "\x0A",
    "\xe2\x80\xa9"};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
