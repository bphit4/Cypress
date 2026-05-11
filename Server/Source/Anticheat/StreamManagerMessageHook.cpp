#include "pch.h"
#ifdef CYPRESS_GW2
#include "StreamManagerMessageHook.h"

#include <Anticheat/LoadoutValidator.h>
#include <fb/Engine/ServerPlayer.h>
#include <fb/TypeInfo/PVZCharacterWeaponUnlockAsset.h>

#include "Cypress/Core/Program.h"
#include "fb/Engine/ServerGameContext.h"

DEFINE_HOOK(
	fb_network_StreamManagerMessage_addMessagePart,
	__fastcall,
	fb::NetworkableMessage*,

	__int64 a1,
	fb::NetworkableMessage* msg,
	__int64 a3
)
{
	fb::NetworkableMessage* addedMsg = Orig_fb_network_StreamManagerMessage_addMessagePart(a1, msg, a3);

	if (addedMsg)
	{
		fb::ServerPlayer* serverPlayer = nullptr;

		if (msg->is("NetworkPlayerSelectedCustomizationAssetMessage"))
		{
			if (!g_program->GetServer()->GetAnticheat()->GetPreventServerCrash() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
				return addedMsg;

			if (ptrread<void*>(msg, 0x48) == nullptr)
			{
				void* unk = addedMsg->m_serverConnection->validateLocalPlayer(addedMsg->m_localPlayerId, false);

				if (!unk)
				{
					g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "[{}] Couldn't validate LocalPlayer!", addedMsg->getType()->getName());
					return nullptr;
				}

				serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);

				const char* playerName = serverPlayer ? serverPlayer->m_name : "Null player";

				g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "Received {} with null CustomizationAsset from {}!", addedMsg->getType()->getName(), playerName);

				if (serverPlayer)
					serverPlayer->disconnect(fb::SecureReason_KickedOut, "Invalid object");

				return nullptr;
			}
		}

		if (msg->is("PVZGameplaySelfReviveMessage"))
		{
			fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
			if (!gameContext) return addedMsg;
			if (!gameContext->getLevel()) return addedMsg;

			if (!g_program->GetServer()->GetAnticheat()->GetPreventSelfRevive() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
				return addedMsg;

			fb::LevelSetup levelSetup = ptrread<fb::LevelSetup>(gameContext->getLevel(), 0x28);
			const char* mode = levelSetup.getInclusionOption("GameMode");

			bool isAllowedMode =
				strstr(mode, "Coop") ||
				strstr(mode, "Ops0") ||
				strstr(mode, "BossHunt");

			if (isAllowedMode)
				return addedMsg;

			void* unk = addedMsg->m_serverConnection->validateLocalPlayer(addedMsg->m_localPlayerId, false);

			if (!unk)
			{
				g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "[{}] Couldn't validate LocalPlayer!", addedMsg->getType()->getName());
				return nullptr;
			}

			serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);

			const char* playerName = serverPlayer ? serverPlayer->m_name : "Null player";

			g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "{} tried to self revive ({})", playerName, addedMsg->getType()->getName());

			return nullptr;
		}

		if (msg->is("PVZGameplayServerSwapCharactersMessage"))
		{
			fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
			if (!gameContext) return addedMsg;
			if (!gameContext->m_serverPlayerManager) return addedMsg;
			if (!gameContext->getLevel()) return addedMsg;

			if (!g_program->GetServer()->GetAnticheat()->GetPreventPlayerSwap() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
				return addedMsg;

			fb::LevelSetup levelSetup = ptrread<fb::LevelSetup>(gameContext->getLevel(), 0x28);

			const char* mode = levelSetup.getInclusionOption("GameMode");
			const char* hostedMode = levelSetup.getInclusionOption("HostedMode");

			bool isAllowedMode =
				(strstr(mode, "Coop") ||
				strstr(mode, "Ops0") ||
				strstr(mode, "BossHunt")) &&
				strcmp(hostedMode, "LocalHosted") == 0;

			int idx = ptrread<int>(msg, 0x4C);
			if (idx < 0 || idx >= gameContext->m_serverPlayerManager->m_players.size())
				return nullptr;

			fb::ServerPlayer* player = gameContext->m_serverPlayerManager->m_players[idx];

			//only allow player swapping on Ops and BossHunt
			if (!isAllowedMode)
				return nullptr;

			//the TargetPlayer must be an AI
			if (player != nullptr && !player->isAIOrPersistentAIPlayer())
			{
				void* unk = addedMsg->m_serverConnection->validateLocalPlayer(addedMsg->m_localPlayerId, false);

				if (!unk)
				{
					g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "[{}] Couldn't validate LocalPlayer!", addedMsg->getType()->getName());
					return nullptr;
				}

				serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);

				const char* playerName = serverPlayer ? serverPlayer->m_name : "Null player";

				g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "{} tried to swap players ({})", playerName, addedMsg->getType()->getName());

				return nullptr;
			}
			
			return addedMsg;
		}

		if (msg->is( "NetworkPlayerSelectedWeaponMessage" ))
		{
			void* unk = addedMsg->m_serverConnection->validateLocalPlayer(msg->m_localPlayerId, false);
			if (!unk)
			{
				CYPRESS_LOGMESSAGE( LogLevel::Debug, "Couldn't verify local player in wep msg, lpid {}", msg->m_localPlayerId );
				return nullptr;
			}

			serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);

			auto* unlockAssetPtr = ptrread<fb::PVZCharacterWeaponUnlockAsset*>(msg, 0x50);
			auto* upgrades = reinterpret_cast<fb::Array<void*>*>(reinterpret_cast<uint8_t*>(msg) + 0x58);

			if (!unlockAssetPtr)
			{
				serverPlayer->disconnect( fb::SecureReason_KickedOut, "Invalid data" );
				return nullptr;
			}

			bool isUpgradable = std::ranges::binary_search( LoadoutValidator::upgradableWeaponIds,
			                                                unlockAssetPtr->getIdentifier());

			if (!isUpgradable)
				return addedMsg;

			CYPRESS_LOGMESSAGE( LogLevel::Debug, "Checking upgrades for {}", unlockAssetPtr->Name );
			int upgradeCount = upgrades->size();
			CYPRESS_LOGMESSAGE( LogLevel::Debug, "{} upgrades", upgradeCount );
			if (upgradeCount > 8 || upgradeCount < 0)
			{
				serverPlayer->disconnect( fb::SecureReason_KickedOut, "Invalid data" );
				return nullptr;
			}

			for ( int i = 0; i < upgradeCount; i++ )
			{
				void* upgradePtr = upgrades->at( i );
				if (!upgradePtr)
				{
					serverPlayer->disconnect( fb::SecureReason_KickedOut, "Invalid data" );
					return nullptr;
				}
			}

			return addedMsg;
		}

		//todo: add an exception for when the player swap to an AI in ops or bosshunt
		//if (msg->is("NetworkPlayerSelectedWeaponMessage"))
		//{
		//	if (!g_program->GetServer()->GetAnticheat()->GetPreventAliveWeaponChange() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
		//		return ret;
		//
		//	void* unk = ret->m_serverConnection->validateLocalPlayer(ret->m_localPlayerId, false);
		//
		//	if (!unk)
		//	{
		//		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "[{}] Couldn't validate LocalPlayer!", ret->getType()->getName());
		//		return nullptr;
		//	}
		//
		//	serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);
		//
		//	const char* playerName = serverPlayer ? serverPlayer->m_name : "Null player";
		//
		//	if (serverPlayer != nullptr && serverPlayer->getServerPVZCharacterEntity() != nullptr)
		//	{
		//		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "{} tried to change weapons while being alive ({})", playerName, ret->getType()->getName());
		//		return nullptr;
		//	}
		//}

		if (msg->is("ClientBuffApplyFromClientMessage") || msg->is("ClientBuffKillFromClientMessage"))
		{
			const char* apply_or_kill = msg->is("ClientBuffApplyFromClientMessage") ? "apply" : "kill";

			if (!g_program->GetServer()->GetAnticheat()->GetPreventClientBuffs() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
				return addedMsg;

			void* unk = addedMsg->m_serverConnection->validateLocalPlayer(addedMsg->m_localPlayerId, false);

			if (!unk)
			{
				g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "[{}] Couldn't validate LocalPlayer!", addedMsg->getType()->getName());
				return nullptr;
			}
				
			serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);

			const char* playerName = serverPlayer ? serverPlayer->m_name : "Null player";
			
			g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "{} tried to {} a client buff ({})", playerName, apply_or_kill, addedMsg->getType()->getName());

			return nullptr;
		}
	}

	return addedMsg;
}
#endif