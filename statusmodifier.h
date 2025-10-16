#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <sh_vector.h>
#include "utlvector.h"
#include "ehandle.h"
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "module.h"
#include "ctimer.h"
#include "funchook.h"
#include "bitvec.h"

#ifdef _WIN32
#define ROOTBIN "/bin/win64/"
#define GAMEBIN "/csgo/bin/win64/"
#else
#define ROOTBIN "/bin/linuxsteamrt64/"
#define GAMEBIN "/csgo/bin/linuxsteamrt64/"
#endif

#define MAXPLAYERS 65

class StatusModifier final : public ISmmPlugin, public IMetamodListener
{
public:
    bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
    bool Unload(char *error, size_t maxlen);

    void Hook_StartupServer(const GameSessionConfiguration_t &config, ISource2WorldSession *pSession, const char *pszMapName);
    void Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick);

private:
    const char *GetAuthor();
    const char *GetName();
    const char *GetDescription();
    const char *GetURL();
    const char *GetLicense();
    const char *GetVersion();
    const char *GetDate();
    const char *GetLogTag();
};

void RegisterEventListeners();
void UnregisterEventListeners();
void PrintToChatAll(const char *msg, ...);
uint32 GetSoundEventHash(const char *pszSoundEventName);
std::string formatCurrentTime();
std::string formatCurrentTime2();
void ErrorLog(const char *msg, ...);

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
