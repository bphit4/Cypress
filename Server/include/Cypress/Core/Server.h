#pragma once
#include <Cypress/Core/Config.h>
#include <Kyber/SocketManager.h>
#include <ServerBanlist.h>
#include <ServerPlaylist.h>
#include <SideChannel.h>
#include <HWID.h>
#include <unordered_map>
#include <mutex>
#include <cstdint>

#ifdef CYPRESS_BFN
#include <Cypress/Core/Console/ConsoleFunctions.h>
#else
#include <fb/Engine/ConsoleContext.h>
#endif
#include <fb/Engine/LevelSetup.h>
#include <fb/Engine/Server.h>

#ifdef CYPRESS_GW2
#include "Anticheat/Anticheat.h"
#endif

namespace fb
{
	class ServerPlayer;
}

#if(HAS_DEDICATED_SERVER)
namespace Cypress
{
	struct PlayerMetadata
	{
		unsigned int playerId = 0;
		std::string playerName;
		int teamId = -1;
		std::string team = "unknown";
		std::string className;
		std::string weaponName;
		uint64_t updatedAtMs = 0;
	};

	class Server
	{
	public:
		Server();
		~Server();

		void UpdateStatus(void* fbServerInstance, float deltaTime);

		HWND* GetMainWindow() { return m_mainWindow; }
#ifdef CYPRESS_BFN
		HWND GetListBox() { return m_listBox; }
		HWND GetCommandBox() { return m_commandBox; }
		HWND GetToggleLogButtonBox() { return m_toggleLogButtonBox; }
		HWND* GetStatusBox() { return m_statusBox; }
#else
		HWND* GetCommandBox() { return m_commandBox; }
		HWND* GetToggleLogButtonBox() { return m_toggleLogButtonBox; }
#endif

		bool GetRunning() { return m_running; }
		void SetRunning(bool running) { m_running = running; }
		
		bool GetStatusUpdated() { return m_statusUpdated; }
		void SetStatusUpdated(bool statusUpdated) { m_statusUpdated = statusUpdated; }

#ifdef CYPRESS_BFN
		bool GetServerLogEnabled() { return m_serverLogEnabled; }
#else
		bool GetServerLogEnabled() { return true; }
#endif
		void SetServerLogEnabled(bool value)
		{
			m_serverLogEnabled = value;
#if !defined(CYPRESS_GW1) && !defined(CYPRESS_BFN)
			*(bool*)(OFFSET_M_LOGOUTPUTENABLED) = value;
#endif
		}

		bool IsUsingPlaylist() { return m_usingPlaylist; }

#ifdef CYPRESS_BFN
		void InitThinClientWindow();
#else
		bool GetIsLoadRequestFromLevelControl() { return m_loadRequestFromLevelControl; }
		void SetLoadRequestFromLevelControl(bool value) { m_loadRequestFromLevelControl = value; }
#endif

		void* GetFbServerInstance() { return m_fbServerInstance; }
		Kyber::SocketManager* GetSocketManager() { return m_socketManager; }
		size_t GetMemoryUsage();
		unsigned int GetSystemTime();
		std::string& GetStatusColumn1() { return m_statusCol1; }
		void SetStatusColumn1(std::string newStatus) { m_statusCol1 = newStatus; }
		std::string& GetStatusColumn2() { return m_statusCol2; }
		void SetStatusColumn2(std::string newStatus) { m_statusCol2 = newStatus; }

		ServerBanlist* GetServerBanlist() { return &m_banlist; }
		ServerPlaylist* GetServerPlaylist() { return &m_playlist; }

#ifdef CYPRESS_GW2
		Anticheat* GetAnticheat() { return &Anticheat::getInstance(); }
#endif

		SideChannelServer* GetSideChannel() { return &m_sideChannel; }
		SideChannelTunnel* GetSideChannelTunnel() { return &m_sideChannelTunnel; }
		std::unordered_map<std::string, std::pair<std::string, Cypress::HardwareFingerprint>>& GetPlayerHwCache() { return m_playerHwCache; }
		void SetPlayerMetadata(const PlayerMetadata& metadata);
		void ClearPlayerMetadata(const std::string& playerName, unsigned int playerId = 0);
		bool TryGetPlayerMetadata(const std::string& playerName, PlayerMetadata& out) const;
		void AppendPlayerMetadata(nlohmann::json& entry, const std::string& playerName, unsigned int playerId = 0) const;
		void TickPlayerMetadata();
		void StartSideChannel();
		void StopSideChannel();
		void RegisterSideChannelHandlers();

		void LoadPlaylistSetup(const PlaylistLevelSetup* nextSetup);
		void LevelSetupFromPlaylistSetup(fb::LevelSetup* setup, const PlaylistLevelSetup* playlistSetup);
		void ApplySettingsFromPlaylistSetup(const PlaylistLevelSetup* playlistSetup);

		static void InitDedicatedServer(void* thisPtr);

		// Commands
#ifdef CYPRESS_BFN
		static void ServerRestartLevel( ArgList args );
		static void ServerLoadLevel( ArgList args );
		static void ServerLoadNextRound(ArgList args);
		static void ServerKickPlayer( ArgList args );
		static void ServerKickPlayerById( ArgList args );
		static void ServerBanPlayer( ArgList args );
		static void ServerBanPlayerById( ArgList args );
		static void ServerUnbanPlayer( ArgList args );
		static void ServerAddBan( ArgList args );
#else
		static void ServerRestartLevel(fb::ConsoleContext& cc);
		static void ServerLoadLevel(fb::ConsoleContext& cc);
		static void ServerKickPlayer(fb::ConsoleContext& cc);
		static void ServerKickPlayerById(fb::ConsoleContext& cc);
		static void ServerBanPlayer(fb::ConsoleContext& cc);
		static void ServerBanPlayerById(fb::ConsoleContext& cc);
		static void ServerUnbanPlayer(fb::ConsoleContext& cc);
		static void ServerAddBan(fb::ConsoleContext& cc);
		static void ServerSay(fb::ConsoleContext& cc);
		static void ServerSayToPlayer(fb::ConsoleContext& cc);
		static void ServerLoadNextPlaylistSetup(fb::ConsoleContext& cc);
#endif

	private:
		Kyber::SocketManager* m_socketManager;
		void* m_fbServerInstance;
		HWND* m_mainWindow;
#ifdef CYPRESS_BFN
		HWND m_listBox;
		HWND m_commandBox;
		HWND m_toggleLogButtonBox;
		HWND m_statusBox[5];
#else
		HWND* m_commandBox;
		HWND* m_toggleLogButtonBox;
#endif
		bool m_running;
		bool m_statusUpdated;
		bool m_serverLogEnabled;
		bool m_usingPlaylist;
#ifndef CYPRESS_BFN
		bool m_loadRequestFromLevelControl;
#endif
		std::string m_statusCol1;
		std::string m_statusCol2;
		ServerBanlist m_banlist;
		ServerPlaylist m_playlist;
		SideChannelServer m_sideChannel;
		SideChannelTunnel m_sideChannelTunnel;

		// hw cache: persists after player disconnects so we can ban by name offline
		std::unordered_map<std::string, std::pair<std::string, Cypress::HardwareFingerprint>> m_playerHwCache; // name -> {hwid, fingerprint}
		mutable std::mutex m_playerMetadataMutex;
		std::unordered_map<std::string, PlayerMetadata> m_playerMetadataByName;
		std::unordered_map<unsigned int, std::string> m_playerNameById;
		uint64_t m_lastPlayerMetadataPollMs = 0;
		void BroadcastPlayerMetadata(const PlayerMetadata& metadata);
#ifdef CYPRESS_BFN
		bool TryBuildBFNPlayerMetadata(fb::ServerPlayer* player, PlayerMetadata& metadata) const;
		bool TryGetBFNClassToken(fb::ServerPlayer* player, std::string& outClassToken) const;
#endif

		friend class Program;
	};
}
#endif
