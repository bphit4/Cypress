#pragma once
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <fb/Engine/ServerPlayer.h>
#include <fb/Engine/ServerConnection.h>
#ifndef CYPRESS_BFN
#include <fb/Engine/ServerGhostManager.h>
#endif

namespace fb
{
    class ServerPeer
    {
    public:
#ifdef CYPRESS_BFN

        ServerConnection* connectionForPlayer(ServerPlayer* player)
        {
            auto func = reinterpret_cast<ServerConnection*(*)(ServerPeer*, ServerPlayer*)>(CYPRESS_GW_SELECT(0x140703000, 0x140627760, 0x140F48140));
            return func(this, player);
        }

        void sendMessage(void* networkableMessage, void* filter)
        {
            auto func = reinterpret_cast<void(*)(ServerPeer*, void*, void*)>(0x140627D40);
            func(this, networkableMessage, filter);
        }

        void* GetGhostManager()
        {
            return ptrread<void*>(this, CYPRESS_GW_SELECT(0x3F90, 0x3F90, 0x45F8));
        }

        unsigned int maxClientCount()
        {
            return ptrread<unsigned int>(this, CYPRESS_GW_SELECT(0x3B98, 0x4020, 0x4688));
        }

#else // GW1 / GW2

#ifdef CYPRESS_GW1
        char pad_0000[15144]; //0x0000
#else
        char pad_0000[16304]; //0x0000
#endif
        eastl::vector<eastl::string> m_bannedPlayers; //0x3FB0
        eastl::vector<eastl::string> m_bannedMachines; //0x3FD0

        ServerConnection* connectionForPlayer(ServerPlayer* player)
        {
            auto func = reinterpret_cast<ServerConnection*(*)(ServerPeer*, ServerPlayer*)>(CYPRESS_GW_SELECT(0x140703000, 0x140627760, 0));
            return func(this, player);
        }

        void sendMessage(void* networkableMessage, void* filter)
        {
#ifdef CYPRESS_GW1
            auto func = reinterpret_cast<void(*)(ServerPeer*, void*)>(0x140702010);
            func(this, networkableMessage);
#elif defined (CYPRESS_GW2)
            auto func = reinterpret_cast<void(*)(ServerPeer*, void*, void*)>(0x140627D40);
            func(this, networkableMessage, filter);
#endif
            
        }

        ServerGhostManager* GetGhostManager()
        {
            return ptrread<ServerGhostManager*>(this, CYPRESS_GW_SELECT(0x3B18, 0x3F90, 0));
        }

        unsigned int maxClientCount()
        {
            return ptrread<unsigned int>(this, CYPRESS_GW_SELECT(0x3B98, 0x4020, 0));
        }

        void removeBannedMachine(const char* bannedMachineId)
        {
            for (auto it = m_bannedMachines.begin(); it != m_bannedMachines.end();)
            {
                if (!strcmp(it->c_str(), bannedMachineId))
                {
                    m_bannedMachines.erase(it);
                    return;
                }
                else
                {
                    ++it;
                }
            }
        }

        void removeBannedPlayer(const char* playerName)
        {
            for (auto it = m_bannedPlayers.begin(); it != m_bannedPlayers.end();)
            {
                if (!strcmp(it->c_str(), playerName))
                {
                    m_bannedPlayers.erase(it);
                    return;
                }
                else
                {
                    ++it;
                }
            }
        }
#endif
    }; //Size: 0x60B8
}