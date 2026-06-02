#pragma once
#include "bitvec.h"
#include "utlvector.h"
#include <playerslot.h>

class Player
{
public:
    Player(CPlayerSlot slot, bool m_bFakeClient = false) : m_slot(slot), m_bFakeClient(m_bFakeClient)
    {
    }

    bool IsFakeClient() { return m_bFakeClient; }
    bool IsConnected() { return m_bConnected; }
    uint64 GetSteamId64() { return m_SteamID->ConvertToUint64(); }
    std::string GetSteam2Id()
    {
        char steam2[32];
        V_snprintf(
            steam2,
            sizeof(steam2),
            "STEAM_%u:%u:%u",
            m_SteamID->GetEUniverse(),
            m_SteamID->GetAccountID() & 1,
            m_SteamID->GetAccountID() >> 1);
        return steam2;
    }
    CCSPlayerController *GetController()
    {
        return CCSPlayerController::FromSlot(m_slot.Get());
    }

    void SetConnected() { m_bConnected = true; }
    void SetSteamId(const CSteamID *steamID) { m_SteamID = steamID; }

private:
    bool m_bConnected;
    const CSteamID *m_SteamID;
    CPlayerSlot m_slot;
    bool m_bFakeClient;
};

class CPlayerManager
{
public:
    CPlayerManager()
    {
        V_memset(m_vecPlayers, 0, sizeof(m_vecPlayers));
    }

    void OnBotConnected(CPlayerSlot slot)
    {
        // ConMsg("OnBotConnected: %d\n", slot.Get());

        m_vecPlayers[slot.Get()] = new Player(slot, true);
        m_vecPlayers[slot.Get()]->SetConnected();
        m_iBotCount++;
    }

    bool OnClientConnected(CPlayerSlot slot, uint64 xuid, const char *pszNetworkID)
    {
        Assert(m_vecPlayers[slot.Get()] == nullptr);

        Player *pPlayer = new Player(slot);
        pPlayer->SetConnected();

        // ConMsg("OnClientConnected: %d %lld\n", slot.Get(), xuid);

        if (xuid != 0)
        {
            pPlayer->SetSteamId(new CSteamID(xuid));
            m_iPlayerCount++;
        }
        m_vecPlayers[slot.Get()] = pPlayer;
        return true;
    }

    void OnClientDisconnect(CPlayerSlot slot)
    {
        // ConMsg("OnClientDisconnect: %d\n", slot.Get());

        Player *pPlayer = m_vecPlayers[slot.Get()];

        if (pPlayer)
        {
            if (pPlayer->IsFakeClient())
            {
                m_iBotCount--;
            }
            else
            {
                m_iPlayerCount--;
            }
        }

        delete m_vecPlayers[slot.Get()];
        m_vecPlayers[slot.Get()] = nullptr;
    }

    Player *GetPlayer(int slot)
    {
        return GetPlayer(CPlayerSlot(slot));
    }
    Player *GetPlayer(CPlayerSlot slot)
    {
        if (slot.Get() < 0 || slot.Get() >= MAXPLAYERS)
            return nullptr;

        return m_vecPlayers[slot.Get()];
    }

    int GetPlayerCount()
    {
        return m_iPlayerCount;
    }
    int GetBotCount()
    {
        return m_iBotCount;
    }

private:
    Player *m_vecPlayers[MAXPLAYERS];
    int m_iBotCount;
    int m_iPlayerCount;
};

extern CPlayerManager *g_playerManager;