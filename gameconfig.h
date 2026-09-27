#pragma once
#undef snprintf
#include "platform.h"
#include "strtools.h"
#include "vendor/nlohmann/json_fwd.hpp"
#include "wchartypes.h"

#include "utils/module.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class CModule;
using ordered_json = nlohmann::ordered_json;

void ErrorLog(const char* msg, ...);

namespace modules {
inline CModule* engine;
inline CModule* tier0;
inline CModule* server;
inline CModule* schemasystem;
inline CModule* vscript;
inline CModule* client;
inline CModule* networksystem;
inline CModule* vphysics2;
inline CModule* matchmaking;
inline CModule* worldrenderer;
#ifdef _WIN32
inline CModule* hammer;
#endif
} // namespace modules

class CGameConfig
{
  public:
    bool Init(char* conf_error, int conf_error_size);
    const std::string GetPath();
    const char* GetLibrary(const std::string& name);
    const char* GetSignature(const std::string& name);
    const char* GetSymbol(const char* name);
    const char* GetPatch(const std::string& name);
    int GetOffset(const std::string& name);
    void* GetAddress(const std::string& name, void* engine, void* server, char* error, int maxlen);
    CModule** GetModule(const char* name);
    bool IsSymbol(const char* name);
    void* ResolveSignature(const char* name);
    int ParseHexNibble(char c);
    bool ParsePatternBytes(const char* pattern, std::vector<uint8_t>& bytes);
    bool IsValidIDASignature(const char* signature, std::vector<uint8_t>& bytes);
    byte* IDASigToUint8Array(const char* signature, size_t& length);

  private:
    std::string m_szPath;
    std::unordered_map<std::string, int> m_umOffsets;
    std::unordered_map<std::string, std::string> m_umSignatures;
    std::unordered_map<std::string, void*> m_umAddresses;
    std::unordered_map<std::string, std::string> m_umLibraries;
    std::unordered_map<std::string, std::string> m_umPatches;
};

void InitModules();

extern CGameConfig* g_GameConfig;
