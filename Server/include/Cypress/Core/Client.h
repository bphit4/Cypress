#pragma once
#include <fb/Engine/Client.h>
#include <fb/Engine/MessageListener.h>
#include <Kyber/SocketManager.h>
#include <SideChannel.h>

namespace Cypress
{
	class Client : public fb::MessageListener
	{
	public:
		Client();
		~Client();

		virtual void onMessage(fb::Message& inMessage) override;

		void* GetFbClientInstance() { return m_fbClientInstance; }
		void SetFbClientInstance(void* client) { m_fbClientInstance = client; }

		fb::ClientState GetClientState() { return m_clientState; }
		void SetClientState(fb::ClientState newState) { m_clientState = newState; }

		bool GetJoiningServer() { return m_joiningServer; }
		void SetJoiningServer(bool value) { m_joiningServer = value; }

		const char* GetPlayerName() { return m_playerName; }

#ifdef CYPRESS_BFN
		bool AddedPrimaryUser() { return m_addedPrimaryUser; }
		//BFN doesn´t register users by default, so we need to execute this to fix Profile Options and gamepad assignment
		void AddPrimaryUser();
		void SetPrimaryUser(void* user) { m_primaryUser = user; }
#endif

		Kyber::SocketManager* GetSocketManager() { return m_socketManager; }

		SideChannelClient* GetSideChannel() { return &m_sideChannel; }
		SideChannelClientListener* GetClientListener() { return &m_clientListener; }
		void ConnectSideChannel(const std::string& serverIP, int port = 0);
		void DisconnectSideChannel();
		void StartClientListener();
		void StopClientListener();
		const std::string& GetHWID() const { return m_hwid; }
		const Cypress::HardwareFingerprint& GetFingerprint() const { return m_fingerprint; }

	private:
		Kyber::SocketManager* m_socketManager;
		const char* m_playerName;
#ifdef CYPRESS_BFN
		void* m_primaryUser;
#endif
		void* m_fbClientInstance;
		fb::ClientState m_clientState;
		bool m_joiningServer;
#ifdef CYPRESS_BFN
		bool m_addedPrimaryUser;
#endif
		SideChannelClient m_sideChannel;
		SideChannelClientListener m_clientListener;
		std::string m_hwid;
		Cypress::HardwareFingerprint m_fingerprint;

		// game relay bridge for UDP-over-TCP tunneling
		SOCKET m_gameRelayBridgeSock = INVALID_SOCKET;
		SOCKET m_gameRelaySock = INVALID_SOCKET;
		int m_gameRelayBridgePort = 0;
		bool m_gameRelayRunning = false;
		std::thread m_gameRelayBridgeThread;
		std::thread m_gameRelayRecvThread;
		std::mutex m_gameRelaySendMutex;
		sockaddr_in m_gameRelayGameAddr{};
		bool m_haveGameAddr = false;

		void StartGameRelay(const std::string& relayHost, int relayPort, const std::string& proxyKey);
		void StopGameRelay();
		void GameRelayBridgeLoop();
		void GameRelayRecvLoop();

		friend class Program;
	};
}