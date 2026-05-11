#include "pch.h"
#include "Psapi.h"
#include <Cypress/Core/Server.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <array>
#include <unordered_map>
#include <format>
#include <Cypress/Core/Program.h>
#include <Cypress/Core/Settings.h>
#include <fb/Engine/LevelSetup.h>
#include <fb/Engine/Server.h>
#include <fb/Main.h>
#include <fb/Engine/String.h>
#include <fb/Engine/ServerPlayerManager.h>
#include <fb/Engine/ServerPeer.h>
#include <fb/Engine/ExecutionContext.h>
#include <fb/Engine/ServerGameContext.h>
#include <fb/Engine/ServerConnection.h>
#include <fb/Engine/ServerPlayer.h>
#include <StringUtil.h>
#ifdef CYPRESS_BFN
#include <fb/Engine/Console.h>
#else
#include <fb/TypeInfo/NetworkSettings.h>
#include <fb/TypeInfo/SystemSettings.h>
#include <fb/TypeInfo/GameSettings.h>
#include <GameHooks/fbMainHooks.h>
#ifdef CYPRESS_GW2
#include <Anticheat/LoadoutValidator.h>
#endif
#endif

namespace
{
	Kyber::SocketSpawnInfo CreateSocketSpawnInfo()
	{
		const char* proxyAddress = std::getenv("CYPRESS_PROXY_ADDRESS");
		const char* proxyKey = std::getenv("CYPRESS_PROXY_KEY");
		const bool isProxied = proxyAddress != nullptr && proxyAddress[0] != '\0';
		return Kyber::SocketSpawnInfo(isProxied, isProxied ? proxyAddress : "", isProxied && proxyKey != nullptr ? proxyKey : "");
	}

	std::string NormalizeTeamLabel(int teamId)
	{
		if (teamId == 2) return "plants";
		if (teamId == 1) return "zombies";
		return "unknown";
	}

	std::string CleanAssetName(const char* assetName)
	{
		if (!assetName || assetName[0] == '\0')
			return "";

		const char* slash = strrchr(assetName, '/');
		return slash ? std::string(slash + 1) : std::string(assetName);
	}

#ifdef CYPRESS_BFN
	bool IsRuntimeDataPointer(const void* ptr, size_t minSize = 1)
	{
		if (!ptr)
			return false;

		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi))
			return false;

		if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS))
			return false;

		if (mbi.Type == MEM_IMAGE)
			return false;

		const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		return start + minSize <= regionEnd;
	}

#endif
}

using namespace fb;

// comma separates name from reason
static void ParseFirstArg(const std::string& input, std::string& first, std::string& remainder)
{
	std::string trimmed = input;
	while (!trimmed.empty() && trimmed.front() == ' ') trimmed = trimmed.substr(1);

	if (!trimmed.empty() && trimmed[0] == '"')
	{
		auto closeQuote = trimmed.find('"', 1);
		if (closeQuote != std::string::npos)
		{
			first = trimmed.substr(1, closeQuote - 1);
			remainder = trimmed.substr(closeQuote + 1);
			while (!remainder.empty() && (remainder.front() == ' ' || remainder.front() == ','))
				remainder = remainder.substr(1);
			return;
		}
	}

	auto comma = trimmed.find(',');
	if (comma != std::string::npos)
	{
		first = trimmed.substr(0, comma);
		remainder = trimmed.substr(comma + 1);
		while (!first.empty() && first.back() == ' ') first.pop_back();
		while (!remainder.empty() && remainder.front() == ' ') remainder = remainder.substr(1);
	}
	else
	{
		first = trimmed;
		while (!first.empty() && first.back() == ' ') first.pop_back();
		remainder = "";
	}
}

#if(HAS_DEDICATED_SERVER)
namespace Cypress
{
	void Server::SetPlayerMetadata(const PlayerMetadata& metadata)
	{
		std::lock_guard<std::mutex> lock(m_playerMetadataMutex);
		auto oldNameIt = m_playerNameById.find(metadata.playerId);
		if (oldNameIt != m_playerNameById.end() && oldNameIt->second != metadata.playerName)
			m_playerMetadataByName.erase(oldNameIt->second);

		m_playerMetadataByName[metadata.playerName] = metadata;
		m_playerNameById[metadata.playerId] = metadata.playerName;
	}

	void Server::ClearPlayerMetadata(const std::string& playerName, unsigned int playerId)
	{
		std::lock_guard<std::mutex> lock(m_playerMetadataMutex);
		if (!playerName.empty())
		{
			m_playerMetadataByName.erase(playerName);
		}

		if (playerId != 0)
		{
			auto byIdIt = m_playerNameById.find(playerId);
			if (byIdIt != m_playerNameById.end())
			{
				m_playerMetadataByName.erase(byIdIt->second);
				m_playerNameById.erase(byIdIt);
			}
		}
	}

	bool Server::TryGetPlayerMetadata(const std::string& playerName, PlayerMetadata& out) const
	{
		std::lock_guard<std::mutex> lock(m_playerMetadataMutex);
		auto it = m_playerMetadataByName.find(playerName);
		if (it == m_playerMetadataByName.end())
			return false;
		out = it->second;
		return true;
	}

	void Server::AppendPlayerMetadata(nlohmann::json& entry, const std::string& playerName, unsigned int playerId) const
	{
		PlayerMetadata metadata;
		bool found = false;

		{
			std::lock_guard<std::mutex> lock(m_playerMetadataMutex);
			auto it = m_playerMetadataByName.find(playerName);
			if (it != m_playerMetadataByName.end())
			{
				metadata = it->second;
				found = true;
			}
			else if (playerId != 0)
			{
				auto nameIt = m_playerNameById.find(playerId);
				if (nameIt != m_playerNameById.end())
				{
					auto metaIt = m_playerMetadataByName.find(nameIt->second);
					if (metaIt != m_playerMetadataByName.end())
					{
						metadata = metaIt->second;
						found = true;
					}
				}
			}
		}

		if (!found)
			return;

		entry["team"] = metadata.team.empty() ? NormalizeTeamLabel(metadata.teamId) : metadata.team;
		entry["team_id"] = metadata.teamId;
		entry["class_name"] = metadata.className;
		entry["weapon_name"] = metadata.weaponName;
		entry["updated_at"] = metadata.updatedAtMs;
	}

	void Server::TickPlayerMetadata()
	{
#ifdef CYPRESS_BFN
		const uint64_t now = GetTickCount64();
		if (m_lastPlayerMetadataPollMs != 0 && now - m_lastPlayerMetadataPollMs < 1000)
			return;

		m_lastPlayerMetadataPollMs = now;

		fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
		if (!gameContext || !gameContext->m_serverPlayerManager)
			return;

		auto& players = gameContext->m_serverPlayerManager->m_players;
		for (size_t i = 0; i < players.size(); i++)
		{
			fb::ServerPlayer* player = players.at(i);
			if (!player || player->isAIPlayer() || !player->m_name || player->m_name[0] == '\0')
				continue;

			std::string playerName(player->m_name);

			PlayerMetadata metadata;
			if (!TryBuildBFNPlayerMetadata(player, metadata))
				continue;

			PlayerMetadata oldMetadata;
			const bool hadOldMetadata = TryGetPlayerMetadata(metadata.playerName, oldMetadata);
			const bool changed = !hadOldMetadata
				|| oldMetadata.className != metadata.className
				|| oldMetadata.teamId != metadata.teamId
				|| oldMetadata.team != metadata.team;
			if (!changed)
				continue;

			SetPlayerMetadata(metadata);
			BroadcastPlayerMetadata(metadata);
		}
#elif defined(CYPRESS_GW1)
		const uint64_t now = GetTickCount64();
		if (m_lastPlayerMetadataPollMs != 0 && now - m_lastPlayerMetadataPollMs < 1000)
			return;

		m_lastPlayerMetadataPollMs = now;

		fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
		if (!gameContext || !gameContext->m_serverPlayerManager)
			return;

		auto& players = gameContext->m_serverPlayerManager->m_players;

		// a proper vehicle blueprint pointer in ServerPlayer would be cleaner, but this works great
		bool zombieBossAssigned = false;
		bool plantBossAssigned = false;

		for (size_t i = 0; i < players.size(); i++)
		{
			fb::ServerPlayer* player = players.at(i);
			if (!player || player->isAIPlayer() || !player->m_name || player->m_name[0] == '\0')
				continue;

			fb::Asset* kitAsset = player->getKitAsset();
			fb::Asset* weaponAsset = player->getPrimaryWeaponAsset();

			PlayerMetadata metadata;
			metadata.playerId = player->getPlayerId();
			metadata.playerName = player->m_name;
			metadata.teamId = player->getTeamId();
			metadata.team = NormalizeTeamLabel(metadata.teamId);
			metadata.updatedAtMs = now;

			const bool hasKit    = kitAsset && kitAsset->Name;
			const bool hasWeapon = weaponAsset && weaponAsset->Name;

			if (!hasKit && !hasWeapon)
			{
				// no kit or weapon: treat as boss if team is known and no boss claimed yet for that team
				if (metadata.teamId == 1 && !zombieBossAssigned)
				{
					metadata.className = "ZombieBoss";
					metadata.weaponName = "";
					zombieBossAssigned = true;
				}
				else if (metadata.teamId == 2 && !plantBossAssigned)
				{
					metadata.className = "PlantBoss";
					metadata.weaponName = "";
					plantBossAssigned = true;
				}
				else
				{
					continue;
				}
			}
			else if (hasKit && hasWeapon)
			{
				metadata.className = kitAsset->Name;
				metadata.weaponName = weaponAsset->Name;
			}
			else
			{
				// kit present but no weapon: player is in character selection, show team only
				metadata.className = "";
				metadata.weaponName = "";
			}

			PlayerMetadata oldMetadata;
			const bool hadOldMetadata = TryGetPlayerMetadata(metadata.playerName, oldMetadata);
			const bool changed = !hadOldMetadata
				|| oldMetadata.className != metadata.className
				|| oldMetadata.weaponName != metadata.weaponName
				|| oldMetadata.teamId != metadata.teamId
				|| oldMetadata.team != metadata.team;
			if (!changed)
				continue;

			SetPlayerMetadata(metadata);
			BroadcastPlayerMetadata(metadata);
		}
#elif defined(CYPRESS_GW2)
		const uint64_t now = GetTickCount64();
		if (m_lastPlayerMetadataPollMs != 0 && now - m_lastPlayerMetadataPollMs < 1000)
			return;

		m_lastPlayerMetadataPollMs = now;

		fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
		if (!gameContext || !gameContext->m_serverPlayerManager)
			return;

		auto& players = gameContext->m_serverPlayerManager->m_players;
		for (size_t i = 0; i < players.size(); i++)
		{
			fb::ServerPlayer* player = players.at(i);
			if (!player || player->isAIPlayer() || !player->m_name || player->m_name[0] == '\0')
				continue;

			ValidationResult result = LoadoutValidator::validatePlayer(player);

			PlayerMetadata metadata;
			metadata.playerId = player->getPlayerId();
			metadata.playerName = result.playerName.empty() ? std::string(player->m_name) : result.playerName;
			metadata.teamId = result.teamId;
			metadata.team = result.teamName.empty() ? NormalizeTeamLabel(result.teamId) : result.teamName;
			metadata.className = result.characterName;
			metadata.weaponName = result.weaponName;
			metadata.updatedAtMs = now;

			if (metadata.className.empty() && metadata.weaponName.empty())
				continue;

			PlayerMetadata oldMetadata;
			const bool hadOldMetadata = TryGetPlayerMetadata(metadata.playerName, oldMetadata);
			const bool changed = !hadOldMetadata
				|| oldMetadata.className != metadata.className
				|| oldMetadata.weaponName != metadata.weaponName
				|| oldMetadata.teamId != metadata.teamId
				|| oldMetadata.team != metadata.team;
			if (!changed)
				continue;

			SetPlayerMetadata(metadata);
			BroadcastPlayerMetadata(metadata);
		}
#endif
	}

#ifdef CYPRESS_BFN
	bool Server::TryBuildBFNPlayerMetadata(fb::ServerPlayer* player, PlayerMetadata& metadata) const
	{
		if (!player || !player->m_name || player->m_name[0] == '\0')
			return false;

		std::string className;
		if (!TryGetBFNClassToken(player, className))
			return false;

		metadata.playerId = player->getPlayerId();
		metadata.playerName = player->m_name;
		metadata.teamId = player->getTeamId();
		metadata.team = NormalizeTeamLabel(metadata.teamId);
		metadata.className = className;
		metadata.weaponName = "";
		metadata.updatedAtMs = GetTickCount64();
		return true;
	}

	bool Server::TryGetBFNClassToken(fb::ServerPlayer* player, std::string& outClassToken) const
	{
		if (!player || player->isAIPlayer())
			return false;

		const char* classPtr = player->getClassNamePtr();
		if (!classPtr || !IsRuntimeDataPointer(classPtr, 4))
			return false;

		outClassToken = classPtr;
		return !outClassToken.empty();
	}
#endif

	void Server::BroadcastPlayerMetadata(const PlayerMetadata& metadata)
	{
		nlohmann::json playerState = {
			{"type", "scPlayerState"},
			{"id", metadata.playerId},
			{"name", metadata.playerName},
			{"team", metadata.team},
			{"team_id", metadata.teamId},
			{"class_name", metadata.className},
			{"weapon_name", metadata.weaponName},
			{"updated_at", metadata.updatedAtMs}
		};

		m_sideChannel.BroadcastToMods(playerState);
		if (Cypress_IsEmbeddedMode())
		{
			nlohmann::json embeddedState = playerState;
			embeddedState["t"] = embeddedState["type"];
			embeddedState.erase("type");
			Cypress_WriteRawStdout(embeddedState.dump() + "\n");
		}
	}


#ifdef CYPRESS_BFN
	WNDPROC editBoxWndProc;
	LRESULT CALLBACK EditBoxWndProcProxy(HWND hWnd, uint32_t msg, uint32_t wParam, LPARAM lParam)
	{
		if (msg == WM_KEYDOWN)
		{
			if (wParam == VK_RETURN)
			{
				char buffer[1024] = { 0 };
				GetWindowTextA(g_program->GetServer()->GetCommandBox(), buffer, sizeof(buffer));
				SetWindowTextA(g_program->GetServer()->GetCommandBox(), "");

				CYPRESS_LOGTOSERVER(LogLevel::Info, "{}", buffer);

				if (!Cypress::HandleCommand(std::string(buffer)))
				{
					fb::Console::enqueueCommand(std::format("ingame|{}", buffer).c_str());
				}
			}
		}
		return editBoxWndProc(hWnd, msg, wParam, lParam);
	}

	void Server::InitThinClientWindow()
	{
		m_mainWindow = (HWND*)0x14421BA88;

		HINSTANCE hInstance = GetModuleHandleA(nullptr);
		WPARAM fontObj = (WPARAM)GetStockObject(ANSI_VAR_FONT);

		SetWindowPos(*m_mainWindow, NULL, 0, 0, 1024, 720, NULL);

		RECT rect;
		GetClientRect(*m_mainWindow, &rect);

		m_listBox = CreateWindowExA(NULL, "LISTBOX", nullptr, 0x50201000, 0, 0, rect.right, rect.bottom - 88, *m_mainWindow, NULL, hInstance, nullptr);
		SendMessageA(m_listBox, WM_SETFONT, fontObj, NULL);

		m_commandBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", nullptr, WS_CHILD | WS_VISIBLE, 0, rect.bottom - 88, rect.right, 17, *m_mainWindow, NULL, hInstance, nullptr);
		SendMessageA(m_commandBox, WM_SETFONT, fontObj, NULL);

		m_toggleLogButtonBox = CreateWindowExA(NULL, "BUTTON", nullptr, 0x54000000, rect.right - 80, rect.bottom - 16, 80, 16, *m_mainWindow, NULL, hInstance, nullptr);
		SendMessageA(m_toggleLogButtonBox, WM_SETFONT, fontObj, NULL);
		SetWindowTextA(m_toggleLogButtonBox, m_serverLogEnabled ? "Disable Logs" : "Enable Logs");

		int x = 0, current = 0;
		HWND* currentBox = m_statusBox;

		do
		{
			current = x + 200;
			*currentBox = CreateWindowExA(NULL, "EDIT", nullptr, 0x54000804, x, rect.bottom - 71, 200, 71, *m_mainWindow, NULL, hInstance, NULL);
			SendMessageA(*currentBox, WM_SETFONT, fontObj, NULL);
			currentBox++;
			x = current;

		} while (current < 1000);

		editBoxWndProc = (WNDPROC)SetWindowLongPtrA(m_commandBox, GWLP_WNDPROC, (LONG_PTR)EditBoxWndProcProxy);

		SetWindowTextW(*m_mainWindow, L"PVZ Battle for Neighborville Server");
		UpdateWindow(*m_mainWindow);
	}

	void Server::ServerRestartLevel(ArgList args)
	{
		void* fbServer = g_program->GetServer()->GetFbServerInstance();
		if (!fbServer) return;

		void* curLevel = ptrread<void*>(fbServer, CYPRESS_GW_SELECT(0xA0, 0xF0, 0xC8));
		if (!curLevel) return;

		fb::LevelSetup* setup = (fb::LevelSetup*)((__int64)curLevel + CYPRESS_GW_SELECT(0x40, 0x118, 0x118));

		fb::PostServerLoadLevelMessage(setup, true, false);
	}

	void Server::ServerLoadLevel(ArgList args)
	{
		int numArgs = args.size();

		if (numArgs < 3) return;

		std::string& levelName = args[0];
		std::string& inclusion = args[1];
		std::string& startpoint = args[2];

		LevelSetup setup;
		if (strstr(levelName.c_str(), "Levels/") == 0)
		{
			setup.m_levelManagerInitialLevel = std::format("Levels/{}/{}", levelName.c_str(), levelName.c_str());
		}
		else
		{
			setup.m_levelManagerInitialLevel = levelName.c_str();
		}
		setup.setInclusionOptions(inclusion.c_str());

		setup.m_name = "Levels/Level_Picnic_Root/Level_Picnic_Root";
		setup.m_levelManagerStartPoint = startpoint.c_str();

		switch (numArgs)
		{
		case 4:
			setup.m_loadScreen_LevelName = args[3].c_str();
			break;
		case 5:
			setup.m_loadScreen_LevelName = args[3].c_str();
			setup.m_loadScreen_GameMode = args[4].c_str();
			break;
		case 6:
			setup.m_loadScreen_LevelName = args[3].c_str();
			setup.m_loadScreen_GameMode = args[4].c_str();
			setup.m_loadScreen_LevelDescription = args[5].c_str();
			break;
		}

		fb::PostServerLoadLevelMessage(&setup, true, false);
	}

	void Server::ServerLoadNextRound(ArgList args)
	{
		if (!g_program->GetServer()->IsUsingPlaylist())
		{
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Server is not using a playlist.");
			return;
		}

		const PlaylistLevelSetup* nextSetup = g_program->GetServer()->GetServerPlaylist()->GetNextSetup();
		g_program->GetServer()->LoadPlaylistSetup(nextSetup);
	}

	void Server::ServerKickPlayer(ArgList args)
	{
		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(args[0].c_str());
		if (!player)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} not found!", args[0].c_str());
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} is an AI!", args[0].c_str());
			return;
		}

		ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection) return;

		const char* reason = "Kicked by Admin";

		if (args.size() > 1)
		{
			reason = args[1].c_str();
		}

		CYPRESS_LOGTOSERVER(LogLevel::Info, "Kicked {} ({})", player->m_name, reason);
		connection->disconnect(SecureReason_KickedOut, reason);
	}

	void Server::ServerKickPlayerById(ArgList args)
	{
		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->getById(std::atoi(args[0].c_str()));
		if (!player)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} not found!", args[0].c_str());
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} is an AI!", args[0].c_str());
			return;
		}

		ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection) return;

		const char* reason = "Kicked by Admin";

		if (args.size() > 1)
		{
			reason = args[1].c_str();
		}

		CYPRESS_LOGTOSERVER(LogLevel::Info, "Kicked {} ({})", player->m_name, reason);
		connection->disconnect(SecureReason_KickedOut, reason);
	}

	void Server::ServerBanPlayer(ArgList args)
	{
		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(args[0].c_str());
		if (!player)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} not found!", args[0].c_str());
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} is an AI!", args[0].c_str());
			return;
		}

		ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection) return;

		const char* reasonText = "The Ban Hammer has spoken!";

		if (args.size() > 1)
		{
			reasonText = args[1].c_str();
		}

		const Cypress::HardwareFingerprint* fp = nullptr;
		const char* accountId = nullptr;
		auto peer = g_program->GetServer()->GetSideChannel()->FindPeerByName(player->m_name);
		if (peer) { fp = &peer->fingerprint; accountId = peer->accountId.c_str(); }
		g_program->GetServer()->GetServerBanlist()->AddToList(player->m_name, connection->m_machineId.c_str(), reasonText, fp, accountId);
		connection->disconnect(SecureReason_Banned, reasonText);
	}

	void Server::ServerBanPlayerById(ArgList args)
	{
		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->getById(std::atoi(args[0].c_str()));
		if (!player)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} not found!", args[0].c_str());
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			CYPRESS_LOGTOSERVER(LogLevel::Error, "Player {} is an AI!", args[0].c_str());
			return;
		}

		ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection) return;

		const char* reasonText = "Banned by server admin";

		if (args.size() > 1)
		{
			reasonText = args[1].c_str();
		}

		const Cypress::HardwareFingerprint* fp = nullptr;
		const char* accountId = nullptr;
		auto peer = g_program->GetServer()->GetSideChannel()->FindPeerByName(player->m_name);
		if (peer) { fp = &peer->fingerprint; accountId = peer->accountId.c_str(); }
		g_program->GetServer()->GetServerBanlist()->AddToList(player->m_name, connection->m_machineId.c_str(), reasonText, fp, accountId);
		connection->disconnect(SecureReason_Banned, reasonText);
	}

	void Server::ServerUnbanPlayer(ArgList args)
	{
		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;

		const char* playerName = args[0].c_str();
		Server* pServer = g_program->GetServer();

		const auto* entry = pServer->GetServerBanlist()->GetBanEntry(playerName);
		if (!entry)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Player {} is not banned", playerName);
			return;
		}

		pServer->GetServerBanlist()->RemoveFromList(playerName);

		CYPRESS_LOGTOSERVER(LogLevel::Info, "Player {} has been unbanned", playerName);
	}

	void Server::ServerAddBan(ArgList args)
	{
		if (args.empty()) return;
		const char* playerName = args[0].c_str();
		std::string reason;
		for (size_t i = 1; i < args.size(); i++)
		{
			if (i > 1) reason += ' ';
			reason += args[i];
		}
		const char* reasonText = reason.empty() ? "Banned by admin" : reason.c_str();

		Server* pServer = g_program->GetServer();

		// prefer live side channel peer, fall back to hw cache
		auto livePeer = pServer->GetSideChannel()->FindPeerByName(playerName);
		const char* machineId = nullptr;
		const Cypress::HardwareFingerprint* fp = nullptr;
		const char* accountId = nullptr;
		if (livePeer)
		{
			machineId = livePeer->hwid.c_str();
			fp = &livePeer->fingerprint;
			accountId = livePeer->accountId.c_str();
		}
		else
		{
			auto it = pServer->m_playerHwCache.find(playerName);
			if (it != pServer->m_playerHwCache.end())
			{
				machineId = it->second.first.c_str();
				fp = &it->second.second;
			}
		}
		pServer->GetServerBanlist()->AddToList(playerName, machineId, reasonText, fp, accountId);

		CYPRESS_LOGTOSERVER(LogLevel::Info, "Pre-banned {}: {}", playerName, reasonText);
	}
#else // GW1 / GW2
	void Server::ServerRestartLevel(fb::ConsoleContext& cc)
	{
		//fb::PVZServerLevelManager::restartLevel();
		reinterpret_cast<void(__fastcall*)()>(CYPRESS_GW_SELECT(0x14078EDA0, 0x140674180, 0))();
	}

	void Server::ServerLoadLevel(fb::ConsoleContext& cc)
	{
		auto stream = cc.stream();
		std::string levelName;
		std::string inclusionOptions;
		std::string loadScreenGameMode;
		std::string loadScreenLevelName;
		std::string loadScreenLevelDescription;
		std::string loadScreenUIAssetPath;

		stream >> levelName >> inclusionOptions >> loadScreenGameMode >> loadScreenLevelName >> loadScreenLevelDescription >> loadScreenUIAssetPath;

		if (inclusionOptions.find("GameMode=") == std::string::npos)
		{
			cc.push("InclusionOptions must contain GameMode, syntax is \'GameMode=GameModeName\'");
			return;
		}

		if (inclusionOptions.find("TOD=") == std::string::npos)
		{
			cc.push("TOD InclusionOption not set, defaulting to Day");

			if (!inclusionOptions.ends_with(";"))
				inclusionOptions += ";";

			inclusionOptions += "TOD=Day";
		}

		if (inclusionOptions.find("HostedMode=") == std::string::npos)
		{
			cc.push("HostedMode InclusionOption not set, defaulting to ServerHosted");

			if (!inclusionOptions.ends_with(";"))
				inclusionOptions += ";";

			inclusionOptions += "HostedMode=ServerHosted";
		}

		LevelSetup setup;
		setup.m_name = levelName;

#ifdef CYPRESS_GW2
		if (!levelName.starts_with("Levels/"))
		{
			setup.m_name = std::format("Levels/{}/{}", levelName, levelName);
		}
#endif

		setup.setInclusionOptions(inclusionOptions.c_str());

#ifdef CYPRESS_GW2
		if (!loadScreenGameMode.empty())
			setup.LoadScreen_GameMode = loadScreenGameMode;

		if (!loadScreenLevelName.empty())
			setup.LoadScreen_LevelName = loadScreenLevelName;

		if (!loadScreenLevelDescription.empty())
			setup.LoadScreen_LevelDescription = loadScreenLevelDescription;

		if (!loadScreenUIAssetPath.empty())
			setup.LoadScreen_UIAssetPath = loadScreenUIAssetPath;

#endif

		fb::PostServerLoadLevelMessage(&setup, true, false);
		cc.push("Loading {} {}", levelName, inclusionOptions);
	}

	void Server::ServerLoadNextPlaylistSetup(fb::ConsoleContext& cc)
	{
		if (!g_program->GetServer()->IsUsingPlaylist())
		{
			cc.push("Server is not using a playlist.");
			return;
		}

		const PlaylistLevelSetup* nextSetup = g_program->GetServer()->GetServerPlaylist()->GetNextSetup();
		g_program->GetServer()->LoadPlaylistSetup(nextSetup);
	}

	void Server::ServerKickPlayer(fb::ConsoleContext& cc)
	{
		std::string playerName, reason;
		ParseFirstArg(cc.m_args ? cc.m_args : "", playerName, reason);

		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(playerName.c_str());
		if (!player)
		{
			cc.push("Player {} not found!", playerName.c_str());
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			cc.push("Player {} is an AI!", playerName.c_str());
			return;
		}

		eastl::string eastlReason = "Kicked by Admin";
		if (!reason.empty())
		{
			eastlReason = reason.c_str();
		}
		cc.push("Kicked {} ({})", player->m_name, eastlReason.c_str());
		player->disconnect(SecureReason_KickedOut, eastlReason);
	}

	void Server::ServerKickPlayerById(fb::ConsoleContext& cc)
	{
		auto stream = cc.stream();
		int playerIndex;

		stream >> playerIndex;
		std::string reason = stream.remainder();

		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->getById(playerIndex);
		if (!player)
		{
			cc.push("No player found at index {}", playerIndex);
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			cc.push("Player {} (index {}) is an AI!", player->m_name, playerIndex);
			return;
		}

		eastl::string eastlReason = "Kicked by Admin";
		if (!reason.empty())
		{
			eastlReason = reason.c_str();
		}
		cc.push("Kicked {} ({})", player->m_name, eastlReason.c_str());
		player->disconnect(SecureReason_KickedOut, eastlReason);
	}

	void Server::ServerBanPlayer(fb::ConsoleContext& cc)
	{
		std::string playerName, reason;
		ParseFirstArg(cc.m_args ? cc.m_args : "", playerName, reason);

		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(playerName.c_str());
		if (!player)
		{
			cc.push("Player {} not found!", playerName.c_str());
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			cc.push("Player {} is an AI!", playerName.c_str());
			return;
		}

		ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection) return;

		eastl::string reasonText = "The Ban Hammer has spoken!";
		if (!reason.empty())
		{
			reasonText = reason.c_str();
		}

		const Cypress::HardwareFingerprint* fp = nullptr;
		auto scPeer = g_program->GetServer()->GetSideChannel()->FindPeerByName(player->m_name);
		const char* accountId = scPeer ? scPeer->accountId.c_str() : nullptr;
		if (scPeer) fp = &scPeer->fingerprint;
		g_program->GetServer()->GetServerBanlist()->AddToList(player->m_name, connection->m_machineId.c_str(), reasonText.c_str(), fp, accountId);
		gameContext->m_serverPeer->m_bannedMachines.push_back(connection->m_machineId);
		gameContext->m_serverPeer->m_bannedPlayers.push_back(player->m_name);
		player->disconnect(SecureReason_Banned, reasonText);
	}

	void Server::ServerBanPlayerById(fb::ConsoleContext& cc)
	{
		auto stream = cc.stream();
		int playerIndex;

		stream >> playerIndex;
		std::string reason = stream.remainder();

		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->getById(playerIndex);
		if (!player)
		{
			cc.push("No player found at index {}", playerIndex);
			return;
		}
		if (player->isAIOrPersistentAIPlayer())
		{
			cc.push("Player {} (index {}) is an AI!", player->m_name, playerIndex);
			return;
		}

		ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection) return;

		eastl::string reasonText = "Banned by server admin";
		if (!reason.empty())
		{
			reasonText = reason.c_str();
		}
		const Cypress::HardwareFingerprint* fp = nullptr;
		auto scPeer = g_program->GetServer()->GetSideChannel()->FindPeerByName(player->m_name);
		const char* accountId = scPeer ? scPeer->accountId.c_str() : nullptr;
		if (scPeer) fp = &scPeer->fingerprint;
		g_program->GetServer()->GetServerBanlist()->AddToList(player->m_name, connection->m_machineId.c_str(), reasonText.c_str(), fp, accountId);
		gameContext->m_serverPeer->m_bannedMachines.push_back(connection->m_machineId);
		gameContext->m_serverPeer->m_bannedPlayers.push_back(player->m_name);
		player->disconnect(SecureReason_Banned, reasonText);
	}

	void Server::ServerUnbanPlayer(fb::ConsoleContext& cc)
	{
		std::string playerName, rest;
		ParseFirstArg(cc.m_args ? cc.m_args : "", playerName, rest);

		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;

		auto* entry = g_program->GetServer()->GetServerBanlist()->GetBanEntry(playerName.c_str());
		if (!entry)
		{
			cc.push("Player {} is not banned", playerName);
			return;
		}

		gameContext->m_serverPeer->removeBannedMachine(entry->MachineId.c_str());
		for (const auto& n : entry->Names)
			gameContext->m_serverPeer->removeBannedPlayer(n.c_str());
		g_program->GetServer()->GetServerBanlist()->RemoveFromList(playerName.c_str());

		cc.push("Player {} has been unbanned", playerName);
	}

	void Server::ServerAddBan(fb::ConsoleContext& cc)
	{
		std::string playerName, reason;
		ParseFirstArg(cc.m_args ? cc.m_args : "", playerName, reason);
		if (playerName.empty()) return;
		if (reason.empty()) reason = "Banned by admin";

		ServerGameContext* gameContext = ServerGameContext::GetInstance();

		Server* pServer = g_program->GetServer();

		// prefer live side channel peer, fall back to hw cache
		auto livePeer = pServer->GetSideChannel()->FindPeerByName(playerName.c_str());
		const char* machineId = nullptr;
		const Cypress::HardwareFingerprint* fp = nullptr;
		const char* accountId = nullptr;
		if (livePeer)
		{
			machineId = livePeer->hwid.c_str();
			fp = &livePeer->fingerprint;
			accountId = livePeer->accountId.c_str();
		}
		else
		{
			auto it = pServer->m_playerHwCache.find(playerName);
			if (it != pServer->m_playerHwCache.end())
			{
				machineId = it->second.first.c_str();
				fp = &it->second.second;
			}
		}
		pServer->GetServerBanlist()->AddToList(playerName.c_str(), machineId, reason.c_str(), fp, accountId);

		if (gameContext)
			gameContext->m_serverPeer->m_bannedPlayers.push_back(eastl::string(playerName.c_str()));

		cc.push("Pre-banned {}: {}", playerName, reason);
	}

	void Server::ServerSay(fb::ConsoleContext& cc)
	{
		std::string args(cc.m_args ? cc.m_args : "");
		std::string message;
		float duration = 5.0f;

		// whole arg string is the message, last token might be duration
		auto lastSpace = args.rfind(' ');
		if (lastSpace != std::string::npos)
		{
			std::string lastToken = args.substr(lastSpace + 1);
			try {
				size_t pos;
				float d = std::stof(lastToken, &pos);
				if (pos == lastToken.size()) {
					duration = d;
					message = args.substr(0, lastSpace);
				}
			} catch (...) { message = args; }
		}
		else
		{
			message = args;
		}

		if (message.empty())
		{
			cc.push("Wrong parameters, missing message");
			return;
		}

#ifdef CYPRESS_GW1
		auto func = reinterpret_cast<void* (*)(__int64 arena)>(0x141508040);
		void* msg = func(0x141E7F750);
#elif defined(CYPRESS_GW2)
		auto func = reinterpret_cast<void* (*)(__int64 arena, int localPlayerId)>(0x141FCEE70);
		void* msg = func(0x1429386E0, 0);
#endif


		fb::String msgText = message.c_str();
		float messageDuration = std::clamp(duration, 1.0f, 10.0f);

		ptrset<fb::String>(msg, 0x48, msgText);
		ptrset<float>(msg, 0x50, messageDuration);

		ServerGameContext::GetInstance()->m_serverPeer->sendMessage(msg, nullptr);
	}

	void Server::ServerSayToPlayer(fb::ConsoleContext& cc)
	{
		std::string args(cc.m_args ? cc.m_args : "");
		std::string playerName;
		std::string message;
		float duration = 5.0f;

		// player name can be quoted for names with spaces
		size_t nameStart = 0;
		size_t nameEnd = std::string::npos;
		if (!args.empty() && args[0] == '"') {
			nameStart = 1;
			nameEnd = args.find('"', 1);
			if (nameEnd == std::string::npos) { cc.push("Wrong parameters: Server.SayToPlayer <name> <message> [duration]"); return; }
			playerName = args.substr(nameStart, nameEnd - nameStart);
			if (nameEnd + 1 < args.size()) args = args.substr(nameEnd + 2); else args.clear();
		} else {
			auto firstSpace = args.find(' ');
			if (firstSpace == std::string::npos) { cc.push("Wrong parameters: Server.SayToPlayer <name> <message> [duration]"); return; }
			playerName = args.substr(0, firstSpace);
			args = args.substr(firstSpace + 1);
		}

		std::string rest = args;

		// last token might be duration
		auto lastSpace = rest.rfind(' ');
		if (lastSpace != std::string::npos)
		{
			std::string lastToken = rest.substr(lastSpace + 1);
			try {
				size_t pos;
				float d = std::stof(lastToken, &pos);
				if (pos == lastToken.size()) {
					duration = d;
					message = rest.substr(0, lastSpace);
				}
			} catch (...) { message = rest; }
		}
		else
		{
			message = rest;
		}

		ServerGameContext* gameContext = ServerGameContext::GetInstance();
		if (!gameContext) return;
		if (!gameContext->m_serverPlayerManager) return;

		ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(playerName.c_str());
		if (!player)
		{
			cc.push("Player {} not found!", playerName.c_str());
			return;
		}

		fb::ServerConnection* connection = gameContext->m_serverPeer->connectionForPlayer(player);
		if (!connection)
		{
			cc.push("Connection for player {} not found!", playerName.c_str());
			return;
		}

#ifdef CYPRESS_GW1
		auto func = reinterpret_cast<void* (*)(__int64 arena)>(0x141508040);
		void* msg = func(0x141E7F750);
#elif defined(CYPRESS_GW2)
		auto func = reinterpret_cast<void* (*)(__int64 arena, int localPlayerId)>(0x141FCEE70);
		void* msg = func(0x1429386E0, 0);
#endif

		fb::String msgText = message.c_str();
		float messageDuration = std::clamp(duration, 1.0f, 10.0f);

		ptrset<fb::String>(msg, 0x48, msgText);
		ptrset<float>(msg, 0x50, messageDuration);

		connection->sendMessage(msg);
	}
#endif // CYPRESS_BFN

#ifdef CYPRESS_BFN
	Server::Server()
		: m_socketManager(new Kyber::SocketManager(Kyber::ProtocolDirection::Clientbound, CreateSocketSpawnInfo()))
		, m_fbServerInstance(nullptr)
		, m_mainWindow(nullptr)
		, m_listBox(NULL)
		, m_commandBox(NULL)
		, m_toggleLogButtonBox(NULL)
		, m_running(false)
		, m_statusUpdated(false)
		, m_serverLogEnabled(false)
		, m_usingPlaylist(false)
		, m_statusCol1()
		, m_statusCol2()
		, m_banlist()
		, m_playlist()
		, m_statusBox{NULL, NULL, NULL, NULL, NULL}
	{
	}
#else
	Server::Server()
		: m_socketManager(new Kyber::SocketManager(Kyber::ProtocolDirection::Clientbound, CreateSocketSpawnInfo()))
		, m_fbServerInstance(nullptr)
		, m_mainWindow((HWND*)(OFFSET_MAINWND))
		, m_commandBox((HWND*)(OFFSET_COMMANDBOX))
		, m_toggleLogButtonBox((HWND*)(OFFSET_TOGGLELOGBUTTONBOX))
		, m_running(false)
		, m_statusUpdated(false)
		, m_serverLogEnabled(false)
		, m_usingPlaylist(false)
		, m_loadRequestFromLevelControl(false)
		, m_statusCol1()
		, m_statusCol2()
		, m_banlist()
		, m_playlist()
	{

	}
#endif

	Server::~Server()
	{
		StopSideChannel();
	}

	void Server::StartSideChannel()
	{
		// Load moderators from file next to the executable
		m_sideChannel.LoadModerators("moderators.json");
		RegisterSideChannelHandlers();

		if (m_sideChannel.Start())
		{
			// Write discovery file so the launcher can find this instance
			WriteDiscoveryFile(m_sideChannel.GetPort(), CYPRESS_GAME_NAME, true);

			// if proxied, start tunnel so relay can route side-channel connections to us
			const char* proxyAddress = std::getenv("CYPRESS_PROXY_ADDRESS");
			const char* proxyKey = std::getenv("CYPRESS_PROXY_KEY");
			if (proxyAddress && proxyAddress[0] != '\0' && proxyKey && proxyKey[0] != '\0')
			{
				// parse host from "host:port" proxy address
				std::string addr(proxyAddress);
				std::string host = addr;
				auto colon = addr.find(':');
				if (colon != std::string::npos)
					host = addr.substr(0, colon);

				if (m_sideChannelTunnel.Start(host, SIDE_CHANNEL_DEFAULT_PORT, proxyKey, m_sideChannel.GetPort()))
				{
					CYPRESS_LOGTOSERVER(LogLevel::Info, "SideChannel tunnel connected to relay {}:{}", host, SIDE_CHANNEL_DEFAULT_PORT);
				}
				else
				{
					CYPRESS_LOGTOSERVER(LogLevel::Warning, "SideChannel tunnel failed to connect to relay");
				}
			}
		}
		else
		{
			CYPRESS_LOGTOSERVER(LogLevel::Warning, "SideChannel: Failed to start TCP listener");
		}
	}

	void Server::StopSideChannel()
	{
		m_sideChannelTunnel.Stop();
		m_sideChannel.Stop();
		DeleteDiscoveryFile();
	}

	void Server::RegisterSideChannelHandlers()
	{
		// provide game engine player names for serverInfo responses
		m_sideChannel.SetPlayerNamesCallback([this]() -> std::vector<std::string>
		{
			std::vector<std::string> names;
			fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
			if (!gameContext || !gameContext->m_serverPlayerManager) return names;
			auto& players = gameContext->m_serverPlayerManager->m_players;
			for (size_t i = 0; i < players.size(); i++)
			{
				fb::ServerPlayer* p = players.at(i);
				if (p && !p->isAIPlayer() && p->m_name)
					names.emplace_back(p->m_name);
			}
			return names;
		});

		// cache hw on every auth, then check if they're actually banned (catches race where game connect beat side-channel auth)
		m_sideChannel.SetOnAuth([this](SideChannelPeer& peer)
		{
			if (peer.name.empty()) return;
			m_playerHwCache[peer.name] = { peer.hwid, peer.fingerprint };

			auto* banlist = GetServerBanlist();
			if (!banlist->IsBanned(peer.name.c_str(), peer.hwid.c_str(), &peer.fingerprint, peer.accountId.empty() ? nullptr : peer.accountId.c_str())) return;

			// they slipped past the game hook — find and kick them now
			banlist->SpreadComponents(peer.fingerprint, peer.name.c_str());
			fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
			if (!gameContext || !gameContext->m_serverPlayerManager) return;
			fb::ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(peer.name.c_str());
			if (!player) return;
#ifdef CYPRESS_BFN
			fb::ServerConnection* conn = gameContext->m_serverPeer->connectionForPlayer(player);
			if (conn) conn->disconnect(fb::SecureReason_Banned, "Banned from server");
#else
			eastl::string banMsg = "Banned from server";
			player->disconnect(fb::SecureReason_Banned, banMsg);
#endif
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Late-kicked banned player {} after side-channel auth", peer.name);
		});

		// kick players who fail identity verification
		m_sideChannel.SetOnAuthReject([this](const std::string& name, const std::string& reason)
		{
			fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
			if (!gameContext || !gameContext->m_serverPlayerManager) return;
			fb::ServerPlayer* player = gameContext->m_serverPlayerManager->findHumanByName(name.c_str());
			if (!player) return;
#ifdef CYPRESS_BFN
			fb::ServerConnection* conn = gameContext->m_serverPeer->connectionForPlayer(player);
			if (conn) conn->disconnect(fb::SecureReason_KickedOut, reason.c_str());
#else
			eastl::string kickMsg(reason.c_str());
			player->disconnect(fb::SecureReason_KickedOut, kickMsg);
#endif
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Kicked {} - {}", name, reason);
		});

		// When a moderator authenticates, send the actual game player list
		m_sideChannel.SetOnModeratorAuth([this](SideChannelPeer& peer)
		{
			fb::ServerGameContext* gameContext = fb::ServerGameContext::GetInstance();
			if (!gameContext || !gameContext->m_serverPlayerManager) return;

			nlohmann::json playerList = nlohmann::json::array();
			auto& players = gameContext->m_serverPlayerManager->m_players;
			for (size_t i = 0; i < players.size(); i++)
			{
				fb::ServerPlayer* p = players.at(i);
				if (p && !p->isAIPlayer())
				{
					nlohmann::json entry = {{"name", std::string(p->m_name)}, {"id", p->getPlayerId()}};
					AppendPlayerMetadata(entry, p->m_name ? p->m_name : "", p->getPlayerId());
					auto scPeer = m_sideChannel.FindPeerByName(p->m_name);
					if (scPeer)
					{
						// display name for everyone
						std::string displayName = std::string(p->m_name);
						if (!scPeer->identityNickname.empty())
							displayName = scPeer->identityNickname;
						else if (!scPeer->identityUsername.empty())
							displayName = scPeer->identityUsername;

						entry["account_id"] = scPeer->accountId;

						// global mods get full data
						entry["ea_pid"] = scPeer->eaPid;
						entry["hwid"] = scPeer->hwid;
						entry["components"] = scPeer->fingerprint.toJson();
						entry["username"] = scPeer->identityUsername;
						entry["nickname"] = scPeer->identityNickname;

						// "Nickname (@username)" for global mods
						std::string modDisplay = displayName;
						if (!scPeer->identityNickname.empty() && !scPeer->identityUsername.empty())
							modDisplay = scPeer->identityNickname + " (@" + scPeer->identityUsername + ")";
						entry["display_name"] = modDisplay;
					}
					playerList.push_back(entry);
				}
			}
			m_sideChannel.SendToPeer(peer, { {"type", "scPlayerList"}, {"players", playerList} });
		});
		// mod kick, route through console commands (parsers handle spaces)
		m_sideChannel.SetHandler("modKick", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string target = msg.value("player", "");
			if (target.empty()) return;

			// strip quotes to prevent command injection
			std::erase(target, '"');

			CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} kicked {}", peer.name, target);
#ifdef CYPRESS_BFN
			std::string cmd = std::format("Server.KickPlayer \"{}\"", target);
			fb::Console::enqueueCommand(cmd.c_str());
#else
			std::string cmd = std::format("ingame|Server.KickPlayer {}", target);
			void* callback[2]{ nullptr, nullptr };
			using tEnqueueCommand = void(*)(const char*, void**);
			auto enqueue = reinterpret_cast<tEnqueueCommand>(OFFSET_CONSOLE__ENQUEUECOMMAND);
			enqueue(cmd.c_str(), callback);
#endif
		});

		// mod ban, route through console commands (ParseFirstArg splits on comma, not space)
		m_sideChannel.SetHandler("modBan", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string target = msg.value("player", "");
			if (target.empty()) return;

			// strip quotes to prevent command injection
			std::erase(target, '"');

			CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} banned {}", peer.name, target);
#ifdef CYPRESS_BFN
			std::string cmd = std::format("Server.BanPlayer \"{}\"", target);
			fb::Console::enqueueCommand(cmd.c_str());
#else
			std::string cmd = std::format("ingame|Server.BanPlayer {}", target);
			void* callback[2]{ nullptr, nullptr };
			using tEnqueueCommand = void(*)(const char*, void**);
			auto enqueue = reinterpret_cast<tEnqueueCommand>(OFFSET_CONSOLE__ENQUEUECOMMAND);
			enqueue(cmd.c_str(), callback);
#endif
		});

		// add/remove mods
		m_sideChannel.SetHandler("addMod", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string hwid = msg.value("hwid", "");
			if (!hwid.empty())
			{
				m_sideChannel.AddModerator(hwid);
				m_sideChannel.SaveModerators("moderators.json");
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} added mod HWID: {}...", peer.name, hwid.substr(0, 8));

				if (Cypress_IsEmbeddedMode())
					Cypress_EmitJsonPlayerEvent("modListChanged", -1, "", nullptr);
			}
		});

		m_sideChannel.SetHandler("removeMod", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string hwid = msg.value("hwid", "");
			if (!hwid.empty())
			{
				m_sideChannel.RemoveModerator(hwid);
				m_sideChannel.SaveModerators("moderators.json");
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} removed mod HWID: {}...", peer.name, hwid.substr(0, 8));

				if (Cypress_IsEmbeddedMode())
					Cypress_EmitJsonPlayerEvent("modListChanged", -1, "", nullptr);
			}
		});

		// mod server commands (map change, restart, etc)
		m_sideChannel.SetHandler("modCommand", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string cmd = msg.value("cmd", "");
			if (cmd.empty()) return;

			// only let mods run whitelisted commands
			static const char* allowedPrefixes[] = {
				"Server.LoadLevel",
				"Server.RestartLevel",
				"Server.LoadNextPlaylistSetup",
				"Server.LoadNextRound",
				"Server.Say",
				"Server.SayToPlayer",
				"Server.AddBan",
				"Server.UnbanPlayer",
				"Server.KickPlayer",
				"Server.BanPlayer",
				"Cypress.SetAnticheat",
				"Cypress.SetSetting",
				"Cypress.GetBans"
			};

			bool allowed = false;
			for (const char* prefix : allowedPrefixes)
			{
				if (cmd.starts_with(prefix) && (cmd.size() == strlen(prefix) || cmd[strlen(prefix)] == ' '))
				{
					allowed = true;
					break;
				}
			}

			if (!allowed)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Command not allowed"} });
				return;
			}

			CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} executed: {}", peer.name, cmd);

			// handle GetBans directly — serialize banlist and send back to peer
			if (cmd == "Cypress.GetBans")
			{
				auto& entries = g_program->GetServer()->GetServerBanlist()->GetBannedPlayers();
				nlohmann::json bans = nlohmann::json::array();
				for (const auto& ban : entries)
				{
					bans.push_back({
						{"Names", ban.Names},
						{"MachineId", ban.MachineId},
						{"BanReason", ban.BanReason}
					});
				}
				m_sideChannel.SendToPeer(peer, { {"type", "scModBans"}, {"bans", bans} });
				return;
			}

			// route Cypress.* commands through the command processor, not frostbite console
			if (cmd.starts_with("Cypress."))
			{
				g_program->ProcessCypressCommand(cmd);
				return;
			}

#ifdef CYPRESS_BFN
			if (!Cypress::HandleCommand(cmd))
				fb::Console::enqueueCommand(std::format("ingame|{}", cmd).c_str());
#else
			{
				void* callback[2]{ nullptr, nullptr };
				using tEnqueueCommand = void(*)(const char*, void**);
				auto enqueue = reinterpret_cast<tEnqueueCommand>(OFFSET_CONSOLE__ENQUEUECOMMAND);
				std::string fmtCmd = std::format("ingame|{}", cmd);
				enqueue(fmtCmd.c_str(), callback);
			}
#endif
		});

		// mod freecam, relay to target client
		m_sideChannel.SetHandler("modFreecam", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string target = msg.value("player", "");
			if (target.empty()) return;

			CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} toggled freecam on {}", peer.name, target);
			m_sideChannel.SendTo(target, { {"type", "freecam"} });
		});

		// mod setting, apply a game setting via SettingsManager
		m_sideChannel.SetHandler("modSetting", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!peer.isModerator)
			{
				m_sideChannel.SendToPeer(peer, { {"type", "error"}, {"msg", "Not a moderator"} });
				return;
			}

			std::string key = msg.value("key", "");
			std::string val = msg.value("value", "");
			if (key.empty()) return;

			CYPRESS_LOGTOSERVER(LogLevel::Info, "Moderator {} set {} = {}", peer.name, key, val);
			fb::SettingsManager::GetInstance()->set(key.c_str(), val.c_str());
		});
	}

	void Server::UpdateStatus(void* fbServerInstance, float deltaTime)
	{
		static unsigned int startSystemTime = GetSystemTime();
		unsigned int sec = (GetSystemTime() - startSystemTime) / 1000;
		unsigned int min = (sec / 60) % 60;
		unsigned int hour = (sec / (60 * 60));

		static unsigned int lastSec = 0;
		static float currentDeltaTime = 0;
		static float sumDeltaTime = 0;
		static unsigned int frameCount = 0;

		m_fbServerInstance = fbServerInstance;

		sumDeltaTime += deltaTime;
		++frameCount;

		if (lastSec != sec)
		{
			lastSec = sec;
			currentDeltaTime = sumDeltaTime / float(frameCount);
			sumDeltaTime = 0;
			frameCount = 0;

			// snapshot player names for side-channel queries (safe from main thread)
			m_sideChannel.UpdatePlayerNamesCache();
		}

#ifdef CYPRESS_BFN
		fb::ServerPlayerManager* playerMgr = ptrread<fb::ServerPlayerManager*>(fbServerInstance, 0xB0);
		fb::ServerPeer* serverPeer = ptrread<fb::ServerPeer*>(fbServerInstance, 0xD8);
		void* ghostMgr = serverPeer->GetGhostManager();

		if (ghostMgr)
			ghostMgr = *(void**)((*(__int64*)((uintptr_t)ghostMgr + 0x8)) + 0x30);

		unsigned int numGhosts = ghostMgr ? ptrread<unsigned int>(ghostMgr, 0x190) : 0;
		unsigned int maxPlayerCount = *(int*)(*(__int64*)0x143FEAB80 + 0x44);

		if (serverPeer)
			maxPlayerCount = std::min(maxPlayerCount, serverPeer->maxClientCount());

		std::string playerCountStr = std::format("{}/{} ({}/{}) [{}]",
			playerMgr->humanPlayerCount(),
			maxPlayerCount - playerMgr->maxSpectatorCount(),
			playerMgr->spectatorCount(),
			playerMgr->maxSpectatorCount(),
			maxPlayerCount);

		g_program->GetServer()->SetStatusColumn1(
			std::format(
			"FPS: {} \t\t\t\t"
			"UpTime: {}:{}:{} \t\t\t"
			"PlayerCount: {} \t\t\t"
			"GhostCount: {} \t\t\t"
			"Memory (CPU): {} MB",
			int(1.0f / currentDeltaTime),
			hour,
			min,
			sec % 60,
			playerCountStr,
			numGhosts,
			GetMemoryUsage()
		));

		void* curLevel = ptrread<void*>(fbServerInstance, 0xC8);
		if (curLevel)
		{
			fb::LevelSetup* setup = (fb::LevelSetup*)((__int64)curLevel + 0x118);
			g_program->GetServer()->SetStatusColumn2(
				std::format(
					"Level: {} \t\t"
					"DSub: {} \t\t"
					"GameMode: {} \t\t"
					"StartPoint: {} \t\t"
					"Platform: {}",
					extractFileName(setup->m_name.c_str()),
					setup->m_levelManagerInitialLevel.empty() ? "Not set" : extractFileName(setup->m_levelManagerInitialLevel.c_str()),
					setup->getInclusionOption("GameMode"),
					setup->m_levelManagerStartPoint.empty() ? "Not set" : setup->m_levelManagerStartPoint.c_str(),
					"Win32"
				));
		}
		else
		{
			g_program->GetServer()->SetStatusColumn2("Level: No level");
		}
#else // GW1 / GW2
		fb::ServerPlayerManager* playerMgr = ptrread<fb::ServerPlayerManager*>(fbServerInstance, CYPRESS_GW_SELECT(0x98, 0xA0, 0));
		fb::ServerPeer* serverPeer = ptrread<fb::ServerPeer*>(fbServerInstance, CYPRESS_GW_SELECT(0x90, 0x98, 0));

		if (!playerMgr || !serverPeer)
			return;

		fb::ServerGhostManager* ghostMgr = serverPeer->GetGhostManager();
		int ghostcount = ghostMgr ? ghostMgr->ghostCount() : 0;

		fb::SettingsManager* settingsManager = fb::SettingsManager::GetInstance();
		if (!settingsManager)
			return;

		// +13 to cut off GamePlatform_
		const char* platformName = fb::toString(settingsManager->getContainer<fb::SystemSettings>("Game")->Platform) + 13;

		unsigned int maxPlayerCount = settingsManager->getContainer<fb::NetworkSettings>("Network")->MaxClientCount;
		unsigned int maxSpectatorCount = settingsManager->getContainer<fb::GameSettings>("Game")->MaxSpectatorCount;

		if (serverPeer)
			maxPlayerCount = std::min(maxPlayerCount, serverPeer->maxClientCount());

		std::string playerCountStr = std::format("{}/{} ({}/{}) [{}]",
			playerMgr->humanPlayerCount(),
			maxPlayerCount - maxSpectatorCount,
			playerMgr->spectatorCount(),
			maxSpectatorCount,
			maxPlayerCount);

		g_program->GetServer()->SetStatusColumn1(
			std::format(
			"FPS: {} \t\t\t\t"
			"UpTime: {}:{}:{} \t\t\t"
			"PlayerCount: {} \t\t\t"
			"GhostCount: {} \t\t\t"
			"Memory (CPU): {} MB",
			int(1.0f / currentDeltaTime),
			hour,
			min,
			sec % 60,
			playerCountStr,
			ghostcount,
			GetMemoryUsage()
		));

		static int prevplayercount = playerMgr->humanPlayerCount();
		int curplayercount = playerMgr->humanPlayerCount();

		if (prevplayercount > 0 && curplayercount == 0)
		{
			reinterpret_cast<void(__fastcall*)()>(CYPRESS_GW_SELECT(0x14078EDA0, 0x140674180, 0))();
		}

		prevplayercount = curplayercount;

		fb::LevelSetup setup = ptrread<fb::LevelSetup>(fbServerInstance, CYPRESS_GW_SELECT(0x40, 0x30, 0));
		if (setup.m_name.length() > 0)
		{
			const char* levelName    = extractFileName(setup.m_name.c_str());
			const char* gameMode     = strlen(setup.getInclusionOption("GameMode")) > 1 ? setup.getInclusionOption("GameMode") : "\t";
			const char* hostedMode   = strlen(setup.getInclusionOption("HostedMode")) > 1 ? setup.getInclusionOption("HostedMode") : "\t";
			const char* timeOfDay    = strlen(setup.getInclusionOption("TOD")) > 1 ? setup.getInclusionOption("TOD") : "\t";

			g_program->GetServer()->SetStatusColumn2(
				std::format(
					"Level: {} \t\t"
					"GameMode: {} \t\t"
					"HostedMode: {} \t\t"
					"TOD: {}\t\t\t\t"
					"Platform: {}",
					levelName,
					gameMode,
					hostedMode,
					timeOfDay,
					platformName
				));
		}
		else
		{
			g_program->GetServer()->SetStatusColumn2("Level: No level");
		}
#endif

		static size_t tick = 0;
		size_t fps = int(1.0f / currentDeltaTime);
		if (tick != fps)
		{
			tick = fps;
			g_program->GetServer()->SetStatusUpdated(false);
		}
	}

	size_t Server::GetMemoryUsage()
	{
		PROCESS_MEMORY_COUNTERS_EX pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
			return pmc.WorkingSetSize / (1024 * 1024); //MegaBytes
		}
		return 0;
	}

	void Server::LoadPlaylistSetup(const PlaylistLevelSetup* nextSetup)
	{
		LevelSetup setup;
		LevelSetupFromPlaylistSetup(&setup, nextSetup);
		ApplySettingsFromPlaylistSetup(nextSetup);

		// update browser level/mode
		{
			auto info = GetSideChannel()->GetServerInfo();
			info.level = nextSetup->LevelName;
			info.mode = nextSetup->GameMode;
			GetSideChannel()->SetServerInfo(info);
		}

#ifdef CYPRESS_BFN
		CYPRESS_LOGTOSERVER(LogLevel::Info, "Server is loading playlist setup ({} on {})", setup.m_levelManagerInitialLevel.c_str(), setup.m_levelManagerStartPoint.c_str());
#else
		CYPRESS_LOGTOSERVER(LogLevel::Info, "Server is loading playlist setup ({} on {})", setup.m_name.c_str(), setup.getInclusionOption("GameMode"));
#endif
		fb::PostServerLoadLevelMessage(&setup, true, false);
	}

	void Server::LevelSetupFromPlaylistSetup(LevelSetup* setup, const PlaylistLevelSetup* playlistSetup)
	{
#ifdef CYPRESS_BFN
		setup->m_name = "Levels/Level_Picnic_Root/Level_Picnic_Root";
		setup->m_levelManagerInitialLevel = playlistSetup->LevelName.c_str();
		setup->m_levelManagerStartPoint = playlistSetup->StartPoint.c_str();

		setup->setInlusionOption("GameMode", playlistSetup->GameMode.c_str());

		if (!playlistSetup->Loadscreen_GamemodeName.empty())
			setup->m_loadScreen_GameMode = playlistSetup->Loadscreen_GamemodeName.c_str();
		if (!playlistSetup->Loadscreen_LevelName.empty())
			setup->m_loadScreen_LevelName = playlistSetup->Loadscreen_LevelName.c_str();
		if (!playlistSetup->Loadscreen_LevelDescription.empty())
			setup->m_loadScreen_LevelDescription = playlistSetup->Loadscreen_LevelDescription.c_str();
#else
		setup->m_name = playlistSetup->LevelName.c_str();
		setup->setInlusionOption("GameMode", playlistSetup->GameMode.c_str());
		setup->setInlusionOption("HostedMode", playlistSetup->HostedMode.c_str());
		setup->setInlusionOption("TOD", playlistSetup->TOD.c_str());
#ifdef CYPRESS_GW2
		if (!playlistSetup->Loadscreen_GamemodeName.empty())
			setup->LoadScreen_GameMode = playlistSetup->Loadscreen_GamemodeName.c_str();
		if (!playlistSetup->Loadscreen_LevelName.empty())
			setup->LoadScreen_LevelName = playlistSetup->Loadscreen_LevelName.c_str();
		if (!playlistSetup->Loadscreen_LevelDescription.empty())
			setup->LoadScreen_LevelDescription = playlistSetup->Loadscreen_LevelDescription.c_str();
		if (!playlistSetup->Loadscreen_UIAssetPath.empty())
		setup->LoadScreen_UIAssetPath = playlistSetup->Loadscreen_UIAssetPath.c_str();
#endif
#endif
	}

	void Server::ApplySettingsFromPlaylistSetup(const PlaylistLevelSetup* playlistSetup)
	{
		if (!playlistSetup->SettingsToApply.empty())
		{
			std::vector<std::string> settings = splitString(playlistSetup->SettingsToApply, '|');

			for (const auto& setting : settings)
			{
				std::vector<std::string> settingAndValue = splitString(setting, ' ');
				if (settingAndValue.size() != 2) continue;

				CYPRESS_LOGTOSERVER(LogLevel::Info, "Playlist is setting {} to {}", settingAndValue[0].c_str(), settingAndValue[1].c_str());
				if (settingAndValue[1].c_str()[0] == '^') // empty string
				{
					fb::SettingsManager::GetInstance()->set(settingAndValue[0].c_str(), "");
				}
				else
				{
					fb::SettingsManager::GetInstance()->set(settingAndValue[0].c_str(), settingAndValue[1].c_str());
				}
			}
		}
	}

	void Server::InitDedicatedServer(void* thisPtr)
	{
#ifdef CYPRESS_BFN
		Server* pServer = g_program->GetServer();
		pServer->SetRunning(true);

		LevelSetup initialLevelSetup;

		if (pServer->m_usingPlaylist)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Loading first setup in playlist");
			const PlaylistLevelSetup* playlistSetup;

			if (pServer->m_playlist.IsMixedMode())
			{
				playlistSetup = pServer->m_playlist.GetMixedLevelSetup(false);
			}
			else
			{
				pServer->m_playlist.SetCurrentSetup(0);
				playlistSetup = pServer->m_playlist.GetCurrentSetup();
			}

			pServer->LevelSetupFromPlaylistSetup(&initialLevelSetup, playlistSetup);
			pServer->ApplySettingsFromPlaylistSetup(playlistSetup);
		}
		else
		{
			const char* initialLevel = fb::ExecutionContext::getOptionValue("dsub");
			const char* initialInclusion = fb::ExecutionContext::getOptionValue("inclusion");
			const char* startPoint = fb::ExecutionContext::getOptionValue("startpoint");

			CYPRESS_ASSERT(initialLevel != nullptr, "Must provide a DSub name via the -dsub argument!");
			CYPRESS_ASSERT(initialInclusion != nullptr, "Must provide inclusion options via the -inclusion argument!");
			CYPRESS_ASSERT(startPoint != nullptr, "Must provide a startpoint via -startpoint argument!");

			initialLevelSetup.m_name = "Levels/Level_Picnic_Root/Level_Picnic_Root";
			initialLevelSetup.m_levelManagerStartPoint = startPoint;

			if (strstr(initialLevel, "Levels/") == 0)
			{
				initialLevelSetup.m_levelManagerInitialLevel.set(std::format("Levels/{}/{}", initialLevel, initialLevel).c_str());
			}
			else
			{
				initialLevelSetup.m_levelManagerInitialLevel = initialLevel;
			}

			initialLevelSetup.setInclusionOptions(initialInclusion);
		}

		ServerSpawnInfo* spawnInfo = new ServerSpawnInfo(&initialLevelSetup);

		auto fb_spawnServer = reinterpret_cast<void (*)(void* thisPtr, ServerSpawnInfo* info)>(OFFSET_FB_MAIN_SPAWNSERVER);
		fb_spawnServer(reinterpret_cast<void*>(reinterpret_cast<uint64_t>(thisPtr) + 0x8), spawnInfo);

		g_program->GetGameModule()->RegisterCommands();
		{
			char banPath[MAX_PATH] = "bans.json";
			GetEnvironmentVariableA("CYPRESS_BANLIST_PATH", banPath, sizeof(banPath));
			pServer->m_banlist.LoadFromFile(banPath);
		}

#else // GW1 / GW2
		g_program->GetServer()->SetRunning(true);

		auto fb_createWindow = reinterpret_cast<__int64(*)()>(CYPRESS_GW_SELECT(0x140008D90, 0x14012F7E0, 0));

		if (!g_program->IsHeadless())
		{
			fb_createWindow();
		}

		LevelSetup initialLevelSetup;
		if (g_program->GetServer()->m_usingPlaylist)
		{
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Loading first setup in playlist");
			const PlaylistLevelSetup* playlistSetup = g_program->GetServer()->m_playlist.GetSetup(0);

			if (g_program->GetServer()->m_playlist.IsMixedMode())
				playlistSetup = g_program->GetServer()->m_playlist.GetMixedLevelSetup(0);
			else
				playlistSetup = g_program->GetServer()->m_playlist.GetSetup(0);

			g_program->GetServer()->LevelSetupFromPlaylistSetup(&initialLevelSetup, playlistSetup);
			g_program->GetServer()->ApplySettingsFromPlaylistSetup(playlistSetup);
		}
		else
		{
			const char* initialLevel = fb::ExecutionContext::getOptionValue("level");
			const char* initialInclusion = fb::ExecutionContext::getOptionValue("inclusion");
			CYPRESS_ASSERT(initialLevel != nullptr, "Must provide a level name via the -level argument!");
			CYPRESS_ASSERT(initialInclusion != nullptr, "Must provide inclusion options via the -inclusion argument!");

			if (strstr(initialLevel, "Levels/") == 0)
			{
				initialLevelSetup.m_name = std::format("Levels/{}/{}", initialLevel, initialLevel);
			}
			else
			{
				initialLevelSetup.m_name = initialLevel;
			}
			initialLevelSetup.setInclusionOptions(initialInclusion);

#ifdef CYPRESS_GW2
			const char* lsGameMode = fb::ExecutionContext::getOptionValue("loadScreenGameMode");
			const char* lsLevelName = fb::ExecutionContext::getOptionValue("loadScreenLevelName");
			const char* lsLevelDesc = fb::ExecutionContext::getOptionValue("loadScreenLevelDescription");
			const char* lsUIAsset = fb::ExecutionContext::getOptionValue("loadScreenUIAssetPath");
			CYPRESS_LOGMESSAGE(LogLevel::Info, "LoadScreen args: GameMode={} LevelName={} Desc={} Asset={}",
				lsGameMode ? lsGameMode : "(null)", lsLevelName ? lsLevelName : "(null)",
				lsLevelDesc ? lsLevelDesc : "(null)", lsUIAsset ? lsUIAsset : "(null)");
			if (lsGameMode) initialLevelSetup.LoadScreen_GameMode = lsGameMode;
			if (lsLevelName) initialLevelSetup.LoadScreen_LevelName = lsLevelName;
			if (lsLevelDesc) initialLevelSetup.LoadScreen_LevelDescription = lsLevelDesc;
			if (lsUIAsset) initialLevelSetup.LoadScreen_UIAssetPath = lsUIAsset;
#endif
		}

		ServerSpawnInfo* spawnInfo = new ServerSpawnInfo(&initialLevelSetup);

		auto fb_spawnServer = reinterpret_cast<void (*)(void* thisPtr, ServerSpawnInfo* info)>(OFFSET_FB_MAIN_SPAWNSERVER);
		fb_spawnServer(thisPtr, spawnInfo);

		g_program->GetGameModule()->RegisterCommands();

		{
			char banPath[MAX_PATH] = "bans.json";
			GetEnvironmentVariableA("CYPRESS_BANLIST_PATH", banPath, sizeof(banPath));
			g_program->GetServer()->m_banlist.LoadFromFile(banPath);
		}
		ServerPeer* peer = ServerGameContext::GetInstance()->m_serverPeer;
		for (const auto& player : g_program->GetServer()->m_banlist.GetBannedPlayers())
		{
			for (const auto& n : player.Names)
				peer->m_bannedPlayers.push_back(n.c_str());
			peer->m_bannedMachines.push_back(player.MachineId.c_str());
		}
#endif

		CYPRESS_LOGMESSAGE(LogLevel::Info, "Initialized Dedicated Server");
	}

	unsigned int Server::GetSystemTime()
	{
		static bool l_isInitialized = false;
		static LARGE_INTEGER    l_liPerformanceFrequency;
		static LARGE_INTEGER    l_liBaseTime;

		LARGE_INTEGER liEndTime;

		if (!l_isInitialized) {
			QueryPerformanceFrequency(&l_liPerformanceFrequency);
			QueryPerformanceCounter(&l_liBaseTime);
			l_isInitialized = true;

			return 0;
		}

		unsigned __int64 n;
		QueryPerformanceCounter(&liEndTime);
		n = liEndTime.QuadPart - l_liBaseTime.QuadPart;
		n = n * 1000 / l_liPerformanceFrequency.QuadPart;

		return (unsigned int)n;
	}
}
#endif
