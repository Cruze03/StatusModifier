#include "gameconfig.h"

#undef snprintf
#include "vendor/nlohmann/json.hpp"

#include <cctype>
#include <fstream>

bool CGameConfig::Init(char *conf_error, int conf_error_size)
{
    const char *pszGamedataPath = "addons/statusmodifier/gamedata/statusmodifier.jsonc";
    char szPath[MAX_PATH];
    V_snprintf(szPath, sizeof(szPath), "%s%s%s", Plat_GetGameDirectory(), "/csgo/", pszGamedataPath);
    std::ifstream gamedataFile(szPath);

    if (!gamedataFile.is_open())
    {
        snprintf(conf_error, conf_error_size, "Failed to open %s, gamedata not loaded", pszGamedataPath);
        return false;
    }

    ordered_json jsonGamedata = ordered_json::parse(gamedataFile, nullptr, false, true);

    if (jsonGamedata.is_discarded() || !jsonGamedata.is_object())
    {
        snprintf(conf_error, conf_error_size, "Failed parsing gamedata JSON from %s", pszGamedataPath);
        return false;
    }

#if defined _LINUX
    const char *platform = "linux";
#else
    const char *platform = "windows";
#endif

    for (auto &[strSection, jsonSection] : jsonGamedata.items())
    {
        if (!jsonSection.is_object())
        {
            snprintf(conf_error, conf_error_size, "Section '%s' must be an object", strSection.c_str());
            return false;
        }

        for (auto &[strEntry, jsonEntry] : jsonSection.items())
        {
            if (!jsonEntry.is_object())
            {
                snprintf(conf_error, conf_error_size, "Entry '%s' must be an object", strEntry.c_str());
                return false;
            }

            if (strSection == "Offsets")
            {
                const auto platformOffset = jsonEntry.find(platform);
                if (platformOffset == jsonEntry.end())
                    continue;

                if (!platformOffset->is_number_integer())
                {
                    snprintf(conf_error, conf_error_size, "Offset '%s' '%s' value is not numeric", strEntry.c_str(), platform);
                    return false;
                }

                m_umOffsets[strEntry] = platformOffset->get<int>();
            }
            else if (strSection == "Signatures")
            {
                const auto library = jsonEntry.find("library");
                if (library == jsonEntry.end() || !library->is_string())
                {
                    snprintf(conf_error, conf_error_size, "Signature '%s' is missing string 'library' value", strEntry.c_str());
                    return false;
                }

                m_umLibraries[strEntry] = library->get<std::string>();

                const auto platformValue = jsonEntry.find(platform);
                if (platformValue == jsonEntry.end())
                    continue;

                if (!platformValue->is_string())
                {
                    snprintf(conf_error, conf_error_size, "Signature '%s' '%s' value is not a string", strEntry.c_str(), platform);
                    return false;
                }

                m_umSignatures[strEntry] = platformValue->get<std::string>();
            }
            else if (strSection == "Patches")
            {
                const auto platformValue = jsonEntry.find(platform);
                if (platformValue == jsonEntry.end())
                    continue;

                if (!platformValue->is_string())
                {
                    snprintf(conf_error, conf_error_size, "Patch '%s' '%s' value is not a string", strEntry.c_str(), platform);
                    return false;
                }

                m_umPatches[strEntry] = platformValue->get<std::string>();
            }
        }
    }

    return true;
}

const std::string CGameConfig::GetPath() { return m_szPath; }

const char *CGameConfig::GetSignature(const std::string &name)
{
    auto it = m_umSignatures.find(name);
    if (it == m_umSignatures.end())
    {
        return nullptr;
    }
    return it->second.c_str();
}

const char *CGameConfig::GetPatch(const std::string &name)
{
    auto it = m_umPatches.find(name);
    if (it == m_umPatches.end())
    {
        return nullptr;
    }
    return it->second.c_str();
}

int CGameConfig::GetOffset(const std::string &name)
{
    auto it = m_umOffsets.find(name);
    if (it == m_umOffsets.end())
    {
        return -1;
    }
    return it->second;
}

const char *CGameConfig::GetLibrary(const std::string &name)
{
    auto it = m_umLibraries.find(name);
    if (it == m_umLibraries.end())
    {
        return nullptr;
    }
    return it->second.c_str();
}

std::string CGameConfig::GetDirectoryName(const std::string &directoryPathInput)
{
    std::string directoryPath = std::string(directoryPathInput);

    size_t found = std::string(directoryPath).find_last_of("/\\");
    if (found != std::string::npos)
    {
        return std::string(directoryPath, found + 1);
    }
    return "";
}