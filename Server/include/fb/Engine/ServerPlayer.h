#pragma once
#include <cstdint>
#include <MemUtil.h>
#include <fb/SecureReason.h>
#include <EASTL/string.h>
#ifdef CYPRESS_BFN
#include <EASTL/new_string.h>
#endif

#if defined(CYPRESS_GW2) || defined(CYPRESS_GW1)
#include "fb/TypeInfo/Asset.h"
#endif
#ifdef CYPRESS_GW2
#include "ObjectExtentRegistration.h"
#include "PVZCharacterServerPlayerExtent.h"
#endif

#define OFFSET_SERVERPLAYER_DISCONNECT CYPRESS_GW_SELECT(0x14075D860, 0x140614B60, 0x140F57070)

namespace fb
{
    class ServerPlayer
    {
    public:
#ifdef CYPRESS_BFN
        char pad_0000[240];
        const char m_name[17];
#else
        char pad_0000[24]; //0x0000
        const char* m_name; //0x0018
#ifdef CYPRESS_GW2
        char pad_0020[24]; //0x0020
        uint64_t m_onlineId; //0x0038
        char pad_0040[4952]; //0x0040
        int64_t m_stateMask; //0x1398
#endif
#endif

        unsigned int getPlayerId()
        {
            return ptrread<unsigned int>(this, CYPRESS_GW_SELECT(0xD90, 0x1490, 0x1B8));
        }

        bool isAIPlayer()
        {
#ifdef CYPRESS_BFN
            return ptrread<bool>(this, 0x48);
#elif defined(CYPRESS_GW1)
            return ptrread<bool>(this, 0xCB8);
#else
            return (m_stateMask & StateMask::k_isAI) != 0;
#endif
        }

        bool isSpectator()
        {
#if defined(CYPRESS_BFN) || defined(CYPRESS_GW1)
            return false;
#else
            return (m_stateMask & StateMask::k_isSpectator) != 0;
#endif
        }

        bool isPersistentAIPlayer()
        {
#if defined(CYPRESS_BFN) || defined(CYPRESS_GW1)
            return isAIPlayer();
#else
            return (m_stateMask & StateMask::k_isPersistentAI) != 0;
#endif
        }

        bool isAIOrPersistentAIPlayer()
        {
            return isAIPlayer() || isPersistentAIPlayer();
        }

#ifdef CYPRESS_GW2
        uint8_t getTeamId()
        {
            return ptrread<uint8_t>(this, 0x139C);
        }

        void* getServerPVZCharacterEntity()
        {
            return ptrread<void*>(this, 0x14F8);
        }

        fb::Asset* getPVZCharacterCustomizationAsset()
        {
            return ptrread<fb::Asset*>(this, 0x910);
        }

        fb::Asset* getPVZCharacterBlueprint()
        {
            return ptrread<fb::Asset*>(this, 0x1540);
        }

        PVZCharacterServerPlayerExtent* getPVZCharacterServerPlayerExtent()
        {
            fb::ObjectExtentRegistration* extentRegistration = reinterpret_cast<fb::ObjectExtentRegistration*>(0x142822F50);
            return reinterpret_cast<PVZCharacterServerPlayerExtent*>(reinterpret_cast<uintptr_t>(this) + extentRegistration->m_offset);
        }
#endif

#ifdef CYPRESS_GW1
        int getTeamId()
        {
            return ptrread<int>(this, 0xCBC);
        }

        // _pvz/Gameplay/Kits/Zombie_Swat, _pvz/Gameplay/Kits/Plant_PeaShooter, etc.
        fb::Asset* getKitAsset()
        {
            return ptrread<fb::Asset*>(this, 0x238);
        }

        // _pvz/Weapons/Soldier/Primary/AssaultRifle/U_AssaultRifle1, etc.
        fb::Asset* getPrimaryWeaponAsset()
        {
            return ptrread<fb::Asset*>(this, 0x1130);
        }
#endif

#ifdef CYPRESS_BFN
        int getTeamId()
        {
            return ptrread<int>(this, 0x058);
        }

        const char* getClassNamePtr()
        {
            return ptrread<const char*>(this, 0x0DB8);
        }

        void disconnect(fb::SecureReason reason, eastl::new_string* reasonText)
        {
            auto ServerPlayer__disconnect = reinterpret_cast<void (*)(void* inst, fb::SecureReason reason, eastl::new_string* reasonText)>(OFFSET_SERVERPLAYER_DISCONNECT);
            ServerPlayer__disconnect(this, reason, reasonText);
        }
#else
        void disconnect(fb::SecureReason reason, const eastl::string& reasonText)
        {
            auto ServerPlayer__disconnect = reinterpret_cast<void (*)(void* inst, fb::SecureReason reason, const eastl::string & reasonText)>(OFFSET_SERVERPLAYER_DISCONNECT);
            ServerPlayer__disconnect(this, reason, reasonText);
        }
#endif

#ifndef CYPRESS_BFN
    private:
        enum StateMask
        {
            k_isAI = 1 << 0,
            k_isSpectator = 1 << 1,
            k_isPersistentAI = 1 << 2,
        };
#endif
    }; //Size: 0x1988
}