#include "pch.h"

#include <Unknwn.h>
#include <cassert>
#include <format>
#include <Cypress/Core/Program.h>
#include <Cypress/Core/VersionInfo.h>
#include <Cypress/Core/Logging.h>
#include <Cypress/Core/Assert.h>
#include <Cypress/Core/Config.h>
#include <Cypress/Core/Console/ConsoleFunctions.h>
#include <fb/Engine/SettingsManager.h>
#ifdef CYPRESS_BFN
#include <fb/Engine/Console.h>
#include <fb/Engine/SettingsManager.h>
#else
#include <GameHooks/fbMainHooks.h>
#endif
#ifdef CYPRESS_GW1
#include <GameModules/GW1Module.h>
#elif defined(CYPRESS_GW2)
#include <GameModules/GW2Module.h>
#elif defined(CYPRESS_BFN)
#include <GameModules/BFNModule.h>
#endif

#include <fb/Engine/ExecutionContext.h>
#include <fb/Engine/Server.h>
#include <fb/Main.h>
#include <fb/Engine/ServerConnection.h>
#include <fb/Engine/ServerPlayerManager.h>
#include <fb/Engine/ServerPlayer.h>
#include <HWID.h>
#ifdef CYPRESS_GW2
#include <Anticheat/Anticheat.h>
#endif

Cypress::Program* g_program = nullptr;

#ifdef CYPRESS_BFN
namespace Cypress
{
	std::vector<Cypress::ConsoleFunction> g_consoleFunctions;
}
#endif

#pragma region DirectInput8 Hijacking

typedef HRESULT(*DirectInput8Create_t)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);
static DirectInput8Create_t DirectInput8CreateOriginal;

extern "C"
{
	HRESULT __declspec(dllexport) DirectInput8Create(
		HINSTANCE hinst,
		DWORD dwVersion,
		REFIID riidltf,
		LPVOID* ppvOut,
		LPUNKNOWN punkOuter
	)
	{
		if (DirectInput8CreateOriginal)
			return DirectInput8CreateOriginal(hinst, dwVersion, riidltf, ppvOut, punkOuter);

		return S_FALSE;
	}
}

void InitDirectInput8Exports()
{
	char dinputDLLName[MAX_PATH + 32];
	GetSystemDirectoryA(dinputDLLName, MAX_PATH);
	strcat_s(dinputDLLName, "\\dinput8.dll");

	HMODULE dinputModule = LoadLibraryA(dinputDLLName);
	if (!dinputModule)
		dinputModule = LoadLibraryA("dinput8_org.dll");
	if (!dinputModule)
	{
		printf("Failed to load dinput8 library.\n (Attempted system dinput8.dll and dinput8_org.dll).\n");
		return;
	}
	DirectInput8CreateOriginal = (DirectInput8Create_t)GetProcAddress(dinputModule, "DirectInput8Create");
}

#pragma endregion

#if(HAS_DEDICATED_SERVER)

DECLARE_HOOK(
	CreateMutexA,
	__stdcall,
	HANDLE,

	LPSECURITY_ATTRIBUTES lpMutexAttributes,
	BOOL bInitialOwner,
	LPCSTR lpName
);

DEFINE_HOOK(
	CreateMutexA,
	__stdcall,
	HANDLE,

	LPSECURITY_ATTRIBUTES lpMutexAttributes,
	BOOL bInitialOwner,
	LPCSTR lpName
)
{
	if (lpName
		&& g_program->AllowMultipleInstances()
		&& strcmp(lpName, "Global\\{5C009BF0-B353-4fc3-A37D-CC14511238AC}_Instance_Mutex") == 0)
	{
		return nullptr;
	}

	return Orig_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
}

#endif

// prevent AllocConsole from messing up our stdout pipe in embedded mode

static BOOL(WINAPI* Orig_AllocConsole)() = nullptr;

static BOOL WINAPI Hk_AllocConsole()
{
	if (g_program && g_program->IsEmbedded())
		return TRUE; // pretend it succeeded
	return Orig_AllocConsole();
}

BOOL WINAPI ConsoleCloseHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
	case CTRL_CLOSE_EVENT: return TRUE;
	default: return FALSE;
	}
}

namespace Cypress
{
	Program::Program(HMODULE inModule)
		: m_hModule(inModule)
		, m_client(nullptr)
#if(HAS_DEDICATED_SERVER)
		, m_server(nullptr)
#endif
		, m_initialized(false)
		, m_wsaStartupInitialized(false)
		, m_allowMultipleInstances(false)
		, m_embeddedMode(false)
#ifndef CYPRESS_BFN
		, m_consoleEnabled(false)
		, m_headlessMode(false)
		, m_logFileEnabled(false)
#endif
	{
		InitDirectInput8Exports();

		MH_STATUS mhInitStatus = MH_Initialize();
		CYPRESS_ASSERT(mhInitStatus == MH_OK, "MinHook failed to initialize");

		char embeddedBuf[8] = {};
		m_embeddedMode = GetEnvironmentVariableA("CYPRESS_EMBEDDED", embeddedBuf, sizeof(embeddedBuf)) > 0
			&& strcmp(embeddedBuf, "1") == 0;

		if (m_embeddedMode)
		{
			MH_CreateHook(&AllocConsole, &Hk_AllocConsole, reinterpret_cast<LPVOID*>(&Orig_AllocConsole));
			MH_EnableHook(&AllocConsole);
		}

		if (!m_embeddedMode)
		{
			AllocConsole();
			FILE* dummy;
			freopen_s(&dummy, "CONOUT$", "w", stdout);

			HANDLE stdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
			DWORD dwMode;
			GetConsoleMode(stdHandle, &dwMode);
			dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
			SetConsoleMode(stdHandle, dwMode);

			char consoleTitle[512];
			snprintf(consoleTitle, sizeof(consoleTitle), "%s Cypress Client - %s v%s", CYPRESS_GAME_NAME, CYPRESS_BUILD_CONFIG, GetCypressVersion().c_str());
			SetConsoleTitleA(consoleTitle);

			HWND consoleWnd = GetConsoleWindow();
			LONG style = GetWindowLongA(consoleWnd, GWL_STYLE);
			style &= ~WS_SYSMENU;

			SetWindowLongA(consoleWnd, GWL_STYLE, style);
			SetWindowPos(consoleWnd, NULL, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

			SetConsoleCtrlHandler(ConsoleCloseHandler, TRUE);
		}
		
		Cypress_InitFileLog();

#ifdef CYPRESS_GW1
		m_gameModule = new GW1Module();
#elif defined(CYPRESS_GW2)
		m_gameModule = new GW2Module();
#elif defined(CYPRESS_BFN)
		m_gameModule = new BFNModule();
#endif

#if(HAS_DEDICATED_SERVER)
		INIT_HOOK(CreateMutexA, CreateMutexA);
#endif

		m_gameModule->InitGameHooks();
		CYPRESS_LOGMESSAGE(LogLevel::Debug, "Initialized Game Hooks");
		m_gameModule->InitMemPatches();
		CYPRESS_LOGMESSAGE(LogLevel::Debug, "Initialized Patches");
	}

	Program::~Program()
	{
		Cypress_CloseFileLog();
	}

	void Program::InitConfig()
	{
		CYPRESS_LOGMESSAGE(LogLevel::Info, "Initializing Config");

		if (fb::ExecutionContext::argc() <= 1)
		{
			char launchArgsBuf[8192];
			if (GetEnvironmentVariableA("GW_LAUNCH_ARGS", launchArgsBuf, sizeof(launchArgsBuf)))
			{
				fb::ExecutionContext::addOptions(false, launchArgsBuf);
				CYPRESS_LOGMESSAGE(LogLevel::Info, "Added options from Cypress launcher: {}", launchArgsBuf);
			}
		}

		CYPRESS_ASSERT(fb::ExecutionContext::argc() >= 3, "No commandline arguments provided! Are you using the launcher?");

		m_allowMultipleInstances = fb::ExecutionContext::getOptionValue("allowMultipleInstances");
		
#if(HAS_DEDICATED_SERVER)
		if (fb::ExecutionContext::getOptionValue("server"))
		{
			m_server = new Server();
			m_server->SetServerLogEnabled(fb::ExecutionContext::getOptionValue("enableServerLog") != nullptr);
			m_server->m_usingPlaylist = fb::ExecutionContext::getOptionValue("usePlaylist") != nullptr;

			if (m_server->m_usingPlaylist)
			{
				const char* playlistFilename = fb::ExecutionContext::getOptionValue("playlistFilename");
				CYPRESS_ASSERT(playlistFilename != nullptr, "Playlist filename must be provided with the -playlistFilename argument!");
				CYPRESS_ASSERT(m_server->m_playlist.LoadFromFile(playlistFilename), "Playlist %s could not be loaded!", playlistFilename);
			}

			// Replay any Cypress commands that arrived before server was ready
			FlushPendingCommands();
		}
		if (!m_server)
#else
		bool tryingToRunAsServer = fb::ExecutionContext::getOptionValue("server") != nullptr;
		CYPRESS_ASSERT(tryingToRunAsServer == false, "This build cannot be run as a server!");
#endif
		{
			m_client = new Client();
			m_client->m_playerName = fb::ExecutionContext::getOptionValue("playerName");

			CYPRESS_ASSERT(m_client->m_playerName != nullptr, "A username must be provided with the -playerName argument!");

			m_client->m_fingerprint = GenerateHardwareFingerprint();
			m_client->m_hwid = GenerateHWID(m_client->m_playerName);
		}
#ifndef CYPRESS_BFN
		m_consoleEnabled = fb::ExecutionContext::getOptionValue("console");
		m_headlessMode = fb::ExecutionContext::getOptionValue("headless");
		m_logFileEnabled = fb::ExecutionContext::getOptionValue("enableLog");

		// we don't really need this right now
		// restore fb::Main::setClientTitle in ClientCallbacks
		//__int64 clt = 0x14012F490;
		//MemPatch(0x1421E2050, (byte*)&clt, 8);
#endif

#if(HAS_DEDICATED_SERVER)
		if (IsServer())
		{
			char serverConsoleTitle[512];
			snprintf(serverConsoleTitle, sizeof(serverConsoleTitle), "%s Cypress Server - %s v%s", CYPRESS_GAME_NAME, CYPRESS_BUILD_CONFIG, GetCypressVersion().c_str());
			if (!m_embeddedMode)
				SetConsoleTitleA(serverConsoleTitle);

			m_gameModule->InitDedicatedServerPatches(m_server);
		}
#endif
		// Start stdin reader for both server and client in embedded mode
		if (m_embeddedMode)
			StartStdinReader();

		// Start client listener for launcher attachment (both embedded and standalone)
		if (IsClient())
			m_client->StartClientListener();
	}

	bool Program::InitWSA()
	{
		CYPRESS_ASSERT(!m_wsaStartupInitialized, "WSA has already been initialized!");

		WSADATA wsaData;
		int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData); //equals to WSAStartup(0x202u, &WSAData);
		if (iResult)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "WSAStartup failed! Result: {}", iResult);
			return false;
		}
		if (wsaData.wVersion != 514)
		{
			CYPRESS_LOGMESSAGE(LogLevel::Error, "Wrong version of sockets detected (expected: 514, got: {})", wsaData.wVersion);
			WSACleanup();
			return false;
		}
		CYPRESS_LOGMESSAGE(LogLevel::Info, "WSAStartup Success!");
		return true;
	}

	void Program::StartStdinReader()
	{
		m_stdinThread = std::thread([this]()
		{
			HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
			if (hStdin == INVALID_HANDLE_VALUE || hStdin == NULL)
				return;

			char buf[4096];
			DWORD bytesRead;
			std::string lineBuf;
			while (true)
			{
				if (!ReadFile(hStdin, buf, sizeof(buf) - 1, &bytesRead, NULL) || bytesRead == 0)
					break;

				buf[bytesRead] = '\0';
				lineBuf += buf;

				// Process complete lines
				size_t pos;
				while ((pos = lineBuf.find('\n')) != std::string::npos)
				{
					std::string cmd = lineBuf.substr(0, pos);
					lineBuf.erase(0, pos + 1);
					// Strip trailing \r
					while (!cmd.empty() && cmd.back() == '\r')
						cmd.pop_back();
					if (cmd.empty()) continue;

				// Handle Cypress-internal commands (e.g. Cypress.AddMod, Cypress.RemoveMod)
#if(HAS_DEDICATED_SERVER)
				if (cmd.starts_with("Cypress.") && !IsClient())
				{
					if (!IsServer())
					{
						// Server not initialized yet, buffer for later
						std::lock_guard<std::mutex> lock(m_pendingCmdsMutex);
						m_pendingCypressCommands.push_back(cmd);
						continue;
					}
					ProcessCypressCommand(cmd);
					continue;
				}
#endif

				// Handle client-side moderator commands (forwarded to server via side-channel)
				if (IsClient() && cmd.starts_with("Cypress."))
				{
					auto* sc = m_client->GetSideChannel();

					// mod token can be pushed at any time (login while in-game)
					if (cmd.starts_with("Cypress.SetModToken "))
					{
						std::string token = cmd.substr(20);
						// always store token so it's available when side channel connects later
						sc->SetModToken(token);
						if (!token.empty() && sc->IsConnected())
						{
							// already connected, trigger challenge now
							sc->Send({ {"type", "modTokenUpdate"} });
							CYPRESS_LOGMESSAGE(LogLevel::Info, "Stored mod token and requested challenge");
						}
						else if (!token.empty())
						{
							CYPRESS_LOGMESSAGE(LogLevel::Info, "Stored mod token (side channel not connected yet, will claim on auth)");
						}
						continue;
					}

					if (!sc->IsConnected() || !sc->IsModerator())
					{
						CYPRESS_LOGMESSAGE(LogLevel::Warning, "Not connected to side-channel or not a moderator");
						continue;
					}

					if (cmd.starts_with("Cypress.ModKick "))
					{
						std::string rest = cmd.substr(16);
						std::string player, reason;
						if (!rest.empty() && rest[0] == '"')
						{
							auto endQuote = rest.find('"', 1);
							player = (endQuote != std::string::npos) ? rest.substr(1, endQuote - 1) : rest.substr(1);
							reason = (endQuote != std::string::npos && endQuote + 2 < rest.size()) ? rest.substr(endQuote + 2) : "Kicked by moderator";
						}
						else
						{
							auto sp = rest.find(' ');
							player = (sp != std::string::npos) ? rest.substr(0, sp) : rest;
							reason = (sp != std::string::npos) ? rest.substr(sp + 1) : "Kicked by moderator";
						}
						sc->Send({ {"type", "modKick"}, {"player", player}, {"reason", reason} });
					}
					else if (cmd.starts_with("Cypress.ModBan "))
					{
						std::string rest = cmd.substr(15);
						std::string player, reason;
						if (!rest.empty() && rest[0] == '"')
						{
							auto endQuote = rest.find('"', 1);
							player = (endQuote != std::string::npos) ? rest.substr(1, endQuote - 1) : rest.substr(1);
							reason = (endQuote != std::string::npos && endQuote + 2 < rest.size()) ? rest.substr(endQuote + 2) : "Banned by moderator";
						}
						else
						{
							auto sp = rest.find(' ');
							player = (sp != std::string::npos) ? rest.substr(0, sp) : rest;
							reason = (sp != std::string::npos) ? rest.substr(sp + 1) : "Banned by moderator";
						}
						sc->Send({ {"type", "modBan"}, {"player", player}, {"reason", reason} });
					}
					else if (cmd.starts_with("Cypress.ModCommand "))
					{
						std::string serverCmd = cmd.substr(19);
						if (!serverCmd.empty())
							sc->Send({ {"type", "modCommand"}, {"cmd", serverCmd} });
					}
					else if (cmd.starts_with("Cypress.ModSetting "))
					{
						std::string setting = cmd.substr(19);
						auto spacePos = setting.find(' ');
						if (spacePos != std::string::npos)
						{
							std::string key = setting.substr(0, spacePos);
							std::string val = setting.substr(spacePos + 1);
							sc->Send({ {"type", "modSetting"}, {"key", key}, {"value", val} });
						}
					}
					else if (cmd.starts_with("Cypress.ModFreecam "))
					{
						std::string target = cmd.substr(19);
						if (!target.empty() && target[0] == '"')
						{
							auto endQuote = target.find('"', 1);
							target = (endQuote != std::string::npos) ? target.substr(1, endQuote - 1) : target.substr(1);
						}
						if (!target.empty())
							sc->Send({ {"type", "modFreecam"}, {"player", target} });
					}
					continue;
				}

#if(HAS_DEDICATED_SERVER)
				if (IsServer())
				{
					CYPRESS_LOGTOSERVER(LogLevel::Info, "> {}", cmd);
#ifdef CYPRESS_BFN
					if (!Cypress::HandleCommand(cmd))
					{
						fb::Console::enqueueCommand(std::format("ingame|{}", cmd).c_str());
					}
#else
					// GW1/GW2: enqueue via Frostbite console at the correct offset
					void* callback[2]{ nullptr, nullptr };
					using tEnqueueCommand = void(*)(const char*, void**);
					auto enqueue = reinterpret_cast<tEnqueueCommand>(OFFSET_CONSOLE__ENQUEUECOMMAND);
					std::string fmtCmd = std::format("ingame|{}", cmd);
					enqueue(fmtCmd.c_str(), callback);
#endif
				}
#endif
				} // end while processing lines
			}
		});
		m_stdinThread.detach();
	}

#if(HAS_DEDICATED_SERVER)
	void Program::ProcessCypressCommand(const std::string& cmd)
	{
		if (cmd.starts_with("Cypress.AddMod "))
		{
			std::string accountId = cmd.substr(15);
			if (!accountId.empty())
			{
				m_server->GetSideChannel()->AddModerator(accountId);
				m_server->GetSideChannel()->SaveModerators("moderators.json");
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Added moderator account: {}...", accountId.substr(0, 8));
				if (m_embeddedMode)
					Cypress_EmitJsonPlayerEvent("modListChanged", -1, "", nullptr);
			}
		}
		else if (cmd.starts_with("Cypress.RemoveMod "))
		{
			std::string accountId = cmd.substr(18);
			if (!accountId.empty())
			{
				m_server->GetSideChannel()->RemoveModerator(accountId);
				m_server->GetSideChannel()->SaveModerators("moderators.json");
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Removed moderator account: {}...", accountId.substr(0, 8));
				if (m_embeddedMode)
					Cypress_EmitJsonPlayerEvent("modListChanged", -1, "", nullptr);
			}
		}
		else if (cmd.starts_with("Cypress.Freecam "))
		{
			std::string target = cmd.substr(16);
			if (!target.empty() && target[0] == '"')
			{
				auto endQuote = target.find('"', 1);
				target = (endQuote != std::string::npos) ? target.substr(1, endQuote - 1) : target.substr(1);
			}
			if (!target.empty())
			{
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Toggling freecam on {}", target);
				m_server->GetSideChannel()->SendTo(target, { {"type", "freecam"} });
			}
		}
		else if (cmd.starts_with("Cypress.SetSetting "))
		{
			// apply a game setting via SettingsManager (same path playlists use)
			std::string rest = cmd.substr(19);
			auto spacePos = rest.find(' ');
			if (spacePos != std::string::npos)
			{
				std::string key = rest.substr(0, spacePos);
				std::string val = rest.substr(spacePos + 1);
				fb::SettingsManager::GetInstance()->set(key.c_str(), val.c_str());
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Setting {} = {}", key, val);
			}
		}
#ifdef CYPRESS_GW2
		else if (cmd.starts_with("Cypress.SetAnticheat "))
		{
			std::string rest = cmd.substr(21);
			auto spacePos = rest.find(' ');
			if (spacePos != std::string::npos)
			{
				std::string key = rest.substr(0, spacePos);
				std::string val = rest.substr(spacePos + 1);
				bool bval = (val == "true" || val == "1");
				auto& ac = Anticheat::getInstance();
				if (key == "Enabled") ac.SetEnabled(bval);
				else if (key == "Verbose") ac.SetVerbose(bval);
				else if (key == "PreventServerCrash") ac.SetPreventServerCrash(bval);
				else if (key == "PreventSelfRevive") ac.SetPreventSelfRevive(bval);
				else if (key == "PreventPlayerSwap") ac.SetPreventPlayerSwap(bval);
				else if (key == "PreventClientBuffs") ac.SetPreventClientBuffs(bval);
				else if (key == "PreventBlacklistedEventSyncs") ac.SetPreventBlacklistedEventSyncs(bval);
				else if (key == "PreventInvalidLoadouts") ac.SetPreventInvalidLoadouts(bval);
				else if (key == "PreventClientLevelLoading") ac.SetPreventClientLevelLoading(bval);
				else if (key == "PreventSyncSettingsFromClients") ac.SetPreventSyncSettingsFromClients(bval);
				else if (key == "PreventAliveWeaponChange") ac.SetPreventAliveWeaponChange(bval);
				else { CYPRESS_LOGTOSERVER(LogLevel::Warning, "Unknown anticheat setting: {}", key); return; }
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Anticheat {} = {}", key, bval ? "true" : "false");
			}
		}
#endif
		else if (cmd.starts_with("Cypress.SetServerInfo "))
		{
			std::string jsonStr = cmd.substr(22);
			try
			{
				auto infoJson = nlohmann::json::parse(jsonStr);
				auto existing = m_server->GetSideChannel()->GetServerInfo();
				ServerInfo si;
				si.motd = infoJson.value("motd", "");
				si.icon = infoJson.value("icon", "");
				si.modded = infoJson.value("modded", false);
				si.modpackUrl = infoJson.value("modpackUrl", "");
				// keep existing level/mode if not in json
				si.level = infoJson.contains("level") ? infoJson.value("level", "") : existing.level;
				si.mode = infoJson.contains("mode") ? infoJson.value("mode", "") : existing.mode;
				m_server->GetSideChannel()->SetServerInfo(si);
				CYPRESS_LOGTOSERVER(LogLevel::Info, "Server info updated (motd: {})", si.motd);
			}
			catch (const std::exception& e)
			{
				CYPRESS_LOGTOSERVER(LogLevel::Warning, "Failed to parse server info: {}", e.what());
			}
		}
		else if (cmd.starts_with("Cypress.SetLogLevel "))
		{
			std::string level = cmd.substr(20);
			Cypress_SetLogLevel(Cypress_ParseLogLevel(level.c_str()));
			CYPRESS_LOGTOSERVER(LogLevel::Info, "Log level set to {}", Cypress_LogLevelToStr(g_cypressLogLevel));
		}
	}

	void Program::FlushPendingCommands()
	{
		std::vector<std::string> cmds;
		{
			std::lock_guard<std::mutex> lock(m_pendingCmdsMutex);
			cmds.swap(m_pendingCypressCommands);
		}
		for (auto& cmd : cmds)
		{
			ProcessCypressCommand(cmd);
		}
	}
#endif
}
