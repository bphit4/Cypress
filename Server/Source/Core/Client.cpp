#include "pch.h"
#include "Cypress/Core/Client.h"
#include <cstdlib>
#include <fb/Engine/Message.h>
#include <fb/Engine/TypeInfo.h>
#include <HWID.h>
#include <Cypress/Core/Logging.h>
#include <FreeCam.h>

#ifdef CYPRESS_BFN
#define OFFSET_FB_CLIENTPLAYERSELECTENTITY_ADDPERMANENTUSER 0x1417A3910
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
}

namespace Cypress
{
	Client::Client()
		: m_socketManager(new Kyber::SocketManager(Kyber::ProtocolDirection::Serverbound, CreateSocketSpawnInfo()))
		, m_playerName(nullptr)
#ifdef CYPRESS_BFN
		, m_primaryUser(nullptr)
#endif
		, m_fbClientInstance(nullptr)
		, m_clientState(fb::ClientState::ClientState_None)
		, m_joiningServer(false)
#ifdef CYPRESS_BFN
		, m_addedPrimaryUser(false)
#endif
	{
	}

	Client::~Client()
	{
		StopClientListener();
		DisconnectSideChannel();
	}

	void Client::onMessage(fb::Message& inMessage)
	{

	}

	void Client::ConnectSideChannel(const std::string& serverIP, int port)
	{
		if (m_sideChannel.IsConnected()) return;

		// kick from game relay if auth is rejected
		m_sideChannel.SetHandler("authResult", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			if (!msg.value("ok", false))
			{
				StopGameRelay();
				m_sideChannel.ForceClose();
			}
		});

		// Register freecam handler before connecting
		m_sideChannel.SetHandler("freecam", [this](const nlohmann::json& msg, SideChannelPeer& peer)
		{
			bool nowActive = ToggleFreeCam();
			CYPRESS_LOGMESSAGE(LogLevel::Info, "Freecam {}", nowActive ? "activated" : "deactivated");
		});

		if (m_sideChannel.Connect(serverIP, port))
		{
			// if we're going through a relay, send handshake so the relay knows which server to proxy to
			const char* proxyKey = std::getenv("CYPRESS_PROXY_KEY");
			if (proxyKey && proxyKey[0] != '\0')
			{
				nlohmann::json relayMsg = {
					{"type", "relay"},
					{"key", std::string(proxyKey)}
				};

				// attach auth ticket if available
				const char* authTicket = std::getenv("CYPRESS_AUTH_TICKET");
				if (authTicket && authTicket[0] != '\0')
					relayMsg["ticket"] = std::string(authTicket);

				m_sideChannel.Send(relayMsg);

				// start game UDP relay bridge (tunnels game UDP over TCP for blocked networks)
				int relayPort = port > 0 ? port : Cypress::SIDE_CHANNEL_DEFAULT_PORT;
				StartGameRelay(serverIP, relayPort, std::string(proxyKey));
			}

			nlohmann::json authMsg = {
				{"type", "auth"},
				{"name", m_playerName ? std::string(m_playerName) : ""},
				{"hwid", m_hwid},
				{"components", m_fingerprint.toJson()},
				{"game", CYPRESS_GAME_NAME}
			};

			// include identity jwt if available
			char jwtBuf[4096] = {};
			DWORD jwtLen = GetEnvironmentVariableA("CYPRESS_IDENTITY_JWT", jwtBuf, sizeof(jwtBuf));
			if (jwtLen > 0)
			{
				authMsg["jwt"] = std::string(jwtBuf, jwtLen);
				CYPRESS_LOGMESSAGE(LogLevel::Info, "SideChannel: Sending auth with JWT ({} chars)", jwtLen);
			}
			else
			{
				CYPRESS_LOGMESSAGE(LogLevel::Warning, "SideChannel: No CYPRESS_IDENTITY_JWT env var (err={})", GetLastError());
			}

			// mod token is pushed via stdin after launch, triggers modTokenUpdate flow

			m_sideChannel.SendAuth(authMsg);
		}
	}

	void Client::DisconnectSideChannel()
	{
		StopGameRelay();
		m_sideChannel.Disconnect();
	}

	void Client::StartClientListener()
	{
		int port = GetSideChannelPort();
		m_clientListener.SetClient(&m_sideChannel);
		if (m_clientListener.Start(port))
		{
			// Write discovery file so launcher can find us
			WriteDiscoveryFile(m_clientListener.GetPort(), CYPRESS_GAME_NAME, false);
		}
	}

	void Client::StopClientListener()
	{
		m_clientListener.Stop();
		DeleteDiscoveryFile();
	}

#ifdef CYPRESS_BFN
	void Client::AddPrimaryUser()
	{
		if (m_primaryUser == nullptr) return;

		using tAddPrimaryUser = void(*)(void*, void**, unsigned int);
		auto addPrimaryUser = reinterpret_cast<tAddPrimaryUser>(OFFSET_FB_CLIENTPLAYERSELECTENTITY_ADDPERMANENTUSER);

		addPrimaryUser(nullptr, &m_primaryUser, 0);

		m_addedPrimaryUser = true;
	}
#endif

	void Client::StartGameRelay(const std::string& relayHost, int relayPort, const std::string& proxyKey)
	{
		if (m_gameRelayRunning) return;

		// create local UDP bridge
		m_gameRelayBridgeSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_gameRelayBridgeSock == INVALID_SOCKET)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "GameRelay: failed to create bridge socket");
			return;
		}

		sockaddr_in bridgeAddr = {};
		bridgeAddr.sin_family = AF_INET;
		bridgeAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		bridgeAddr.sin_port = 0;
		if (bind(m_gameRelayBridgeSock, (sockaddr*)&bridgeAddr, sizeof(bridgeAddr)) != 0)
		{
			closesocket(m_gameRelayBridgeSock);
			m_gameRelayBridgeSock = INVALID_SOCKET;
			return;
		}

		sockaddr_in bound = {};
		int boundLen = sizeof(bound);
		getsockname(m_gameRelayBridgeSock, (sockaddr*)&bound, &boundLen);
		m_gameRelayBridgePort = ntohs(bound.sin_port);

		// redirect SocketManager to our bridge
		char portBuf[16];
		snprintf(portBuf, sizeof(portBuf), "%d", m_gameRelayBridgePort);
		_putenv_s("CYPRESS_PROXY_ADDRESS", "127.0.0.1");
		_putenv_s("CYPRESS_PROXY_PORT", portBuf);

		CYPRESS_LOGMESSAGE(LogLevel::Info, "GameRelay: bridge on 127.0.0.1:{}", m_gameRelayBridgePort);

		// refresh cached proxy on existing sockets
		for (auto* sock : m_socketManager->m_sockets)
			sock->RefreshProxyAddress();

		// connect TCP to relay for game data
		m_gameRelaySock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_gameRelaySock == INVALID_SOCKET)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "GameRelay: failed to create TCP socket");
			closesocket(m_gameRelayBridgeSock);
			m_gameRelayBridgeSock = INVALID_SOCKET;
			return;
		}

		int nodelay = 1;
		setsockopt(m_gameRelaySock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

		sockaddr_in relayAddr = {};
		relayAddr.sin_family = AF_INET;
		relayAddr.sin_port = htons(relayPort);

		if (inet_pton(AF_INET, relayHost.c_str(), &relayAddr.sin_addr) != 1)
		{
			addrinfo hints = {}, *result = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			if (getaddrinfo(relayHost.c_str(), nullptr, &hints, &result) != 0 || !result)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Error, "GameRelay: cannot resolve {}", relayHost);
				closesocket(m_gameRelaySock);
				m_gameRelaySock = INVALID_SOCKET;
				closesocket(m_gameRelayBridgeSock);
				m_gameRelayBridgeSock = INVALID_SOCKET;
				return;
			}
			relayAddr.sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
			freeaddrinfo(result);
		}

		if (connect(m_gameRelaySock, (sockaddr*)&relayAddr, sizeof(relayAddr)) == SOCKET_ERROR)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "GameRelay: connect failed ({})", WSAGetLastError());
			closesocket(m_gameRelaySock);
			m_gameRelaySock = INVALID_SOCKET;
			closesocket(m_gameRelayBridgeSock);
			m_gameRelayBridgeSock = INVALID_SOCKET;
			return;
		}

		// send handshake
		nlohmann::json handshake = {{"type", "gameRelay"}, {"key", proxyKey}};
		const char* authTicket = std::getenv("CYPRESS_AUTH_TICKET");
		if (authTicket && authTicket[0] != '\0')
			handshake["ticket"] = std::string(authTicket);

		std::string line = handshake.dump() + "\n";
		::send(m_gameRelaySock, line.c_str(), (int)line.size(), 0);

		m_gameRelayRunning = true;
		m_gameRelayBridgeThread = std::thread(&Client::GameRelayBridgeLoop, this);
		m_gameRelayRecvThread = std::thread(&Client::GameRelayRecvLoop, this);

		CYPRESS_LOGMESSAGE(LogLevel::Info, "GameRelay: connected to {}:{}", relayHost, relayPort);
	}

	void Client::StopGameRelay()
	{
		if (!m_gameRelayRunning) return;
		m_gameRelayRunning = false;

		if (m_gameRelaySock != INVALID_SOCKET) { closesocket(m_gameRelaySock); m_gameRelaySock = INVALID_SOCKET; }
		if (m_gameRelayBridgeSock != INVALID_SOCKET) { closesocket(m_gameRelayBridgeSock); m_gameRelayBridgeSock = INVALID_SOCKET; }

		if (m_gameRelayBridgeThread.joinable()) m_gameRelayBridgeThread.join();
		if (m_gameRelayRecvThread.joinable()) m_gameRelayRecvThread.join();
	}

	void Client::GameRelayBridgeLoop()
	{
		// reads UDP from SocketManager via bridge, sends framed to relay TCP
		char buf[65536];
		while (m_gameRelayRunning)
		{
			sockaddr_in from = {};
			int fromLen = sizeof(from);
			int n = recvfrom(m_gameRelayBridgeSock, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
			if (n <= 0)
			{
				if (!m_gameRelayRunning) break;
				continue;
			}

			// learn SocketManager's address from first packet
			if (!m_haveGameAddr)
			{
				m_gameRelayGameAddr = from;
				m_haveGameAddr = true;
				char ipBuf[INET_ADDRSTRLEN] = {};
				inet_ntop(AF_INET, &from.sin_addr, ipBuf, sizeof(ipBuf));
				CYPRESS_LOGMESSAGE(LogLevel::Info, "GameRelayBridge: learned game addr {}:{}", ipBuf, ntohs(from.sin_port));
			}

			// send [2B length][data] to relay
			uint8_t hdr[2];
			hdr[0] = (n >> 8) & 0xFF;
			hdr[1] = n & 0xFF;
			std::lock_guard<std::mutex> lock(m_gameRelaySendMutex);
			::send(m_gameRelaySock, (char*)hdr, 2, 0);
			::send(m_gameRelaySock, buf, n, 0);
		}
	}

	void Client::GameRelayRecvLoop()
	{
		// reads framed game data from relay TCP, sends to SocketManager via bridge
		while (m_gameRelayRunning)
		{
			uint8_t hdr[2];
			int total = 0;
			while (total < 2)
			{
				int n = recv(m_gameRelaySock, (char*)hdr + total, 2 - total, 0);
				if (n <= 0) { m_gameRelayRunning = false; return; }
				total += n;
			}

			int pktLen = (hdr[0] << 8) | hdr[1];
			if (pktLen <= 0 || pktLen > 4096) { m_gameRelayRunning = false; return; }

			std::vector<char> pkt(pktLen);
			total = 0;
			while (total < pktLen)
			{
				int n = recv(m_gameRelaySock, pkt.data() + total, pktLen - total, 0);
				if (n <= 0) { m_gameRelayRunning = false; return; }
				total += n;
			}

			// send to SocketManager if we know its address
			if (m_haveGameAddr)
			{
				int sent = sendto(m_gameRelayBridgeSock, pkt.data(), pktLen, 0,
					(sockaddr*)&m_gameRelayGameAddr, sizeof(m_gameRelayGameAddr));
				static int relayRecvCount = 0;
				if (++relayRecvCount <= 10)
				{
					char ipBuf[INET_ADDRSTRLEN] = {};
					inet_ntop(AF_INET, &m_gameRelayGameAddr.sin_addr, ipBuf, sizeof(ipBuf));
					CYPRESS_LOGMESSAGE(LogLevel::Info, "GameRelayRecv #{}: {} bytes -> {}:{}, sent={}",
						relayRecvCount, pktLen, ipBuf, ntohs(m_gameRelayGameAddr.sin_port), sent);
				}
			}
			else
			{
				static int dropCount = 0;
				if (++dropCount <= 5)
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "GameRelayRecv: dropped {} bytes, no game addr yet", pktLen);
			}
		}
	}
}
