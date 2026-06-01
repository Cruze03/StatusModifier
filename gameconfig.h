#pragma once
#include "platform.h"
#include "strtools.h"
#include "vendor/nlohmann/json_fwd.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using ordered_json = nlohmann::ordered_json;

class CGameConfig
{
public:
    bool Init(char *conf_error, int conf_error_size);
    const std::string GetPath();
    const char *GetLibrary(const std::string &name);
    const char *GetSignature(const std::string &name);
    const char *GetSymbol(const char *name);
    const char *GetPatch(const std::string &name);
    int GetOffset(const std::string &name);
    static std::string GetDirectoryName(const std::string &directoryPathInput);

private:
    std::string m_szGameDir;
    std::string m_szPath;
    std::unordered_map<std::string, int> m_umOffsets;
    std::unordered_map<std::string, std::string> m_umSignatures;
    std::unordered_map<std::string, std::string> m_umLibraries;
    std::unordered_map<std::string, std::string> m_umPatches;
};
