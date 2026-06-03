#pragma once

extern IGameEventSystem *g_gameEventSystem;
extern ISmmAPI *g_SMAPI;

extern ISteamGameServer *g_pGameServer;
extern std::string g_sServerIP;

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

std::string FormatCurrentTime()
{
    std::time_t currentTime = std::time(nullptr);
    std::tm *localTime = std::localtime(&currentTime);
    std::ostringstream formattedTime;
    formattedTime << std::put_time(localTime, "%m/%d/%Y - %H:%M:%S");
    return formattedTime.str();
}

std::string FormatCurrentTime2()
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

std::vector<std::string_view> SplitString(std::string_view str, std::string_view delim)
{
    std::vector<std::string_view> result;
    size_t start = 0, pos;

    while ((pos = str.find(delim, start)) != std::string_view::npos)
    {
        result.emplace_back(str.substr(start, pos - start));
        start = pos + delim.size();
    }
    result.emplace_back(str.substr(start)); // last token
    return result;
}

bool IsInvalidChar(char c)
{
    return !(c >= 0 && c < 128);
}

void StripUnicode(std::string &str)
{
    str.erase(std::remove_if(str.begin(), str.end(), IsInvalidChar), str.end());
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
                        g_SMAPI->GetBaseDir(), FormatCurrentTime2().c_str());
    g_SMAPI->Format(szBuffer, sizeof(szBuffer), "L %s: %s\n",
                    FormatCurrentTime().c_str(), buf);

    FILE *pFile = fopen(szPath, "a");
    if (pFile)
    {
        fputs(szBuffer, pFile);
        fclose(pFile);
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