#include "pch.h"
#ifdef CYPRESS_BFN
#include <sstream>
#endif

#include "fbMainHooks.h"
#include <Cypress/Core/Program.h>
#ifdef CYPRESS_GW1
#include <fb/Engine/SettingsManager.h>
#endif
#ifdef CYPRESS_BFN
#include <fb/Engine/ExecutionContext.h>
#endif

#ifdef CYPRESS_GW1
DEFINE_HOOK(
	fb_Main_initSettings,
	__fastcall,
	void,

	void* thisPtr
)
{
	Orig_fb_Main_initSettings(thisPtr);

	// Set backend to peer mode for dedicated server (prevents null online subsystem crashes)
	// Must write directly to struct like GardenGate does, SettingsManager::set() doesn't work for enums
	if (g_program->IsServer())
	{
		fb::SettingsManager* sm = fb::SettingsManager::GetInstance();
		if (sm)
		{
			// OnlineSettings inherits SystemSettings (0x20 bytes)
			// Backend (BackendType enum, int32) is at offset 0x20
			// PeerBackend is at offset 0x24
			// ServerAllowAnyReputation (bool), need to find offset
			void* onlineSettings = sm->getContainer<void>("Online");
			if (onlineSettings)
			{
				*(int32_t*)((uintptr_t)onlineSettings + 0x20) = 2; // Backend_Peer = 2
				*(int32_t*)((uintptr_t)onlineSettings + 0x24) = 2; // PeerBackend = Backend_Peer
				CYPRESS_LOGMESSAGE(LogLevel::Info, "Set Online.Backend = Backend_Peer (direct write)");
			}

			void* serverSettings = sm->getContainer<void>("Server");
			if (serverSettings)
			{
				// ServerSettings::IsRanked, need correct offset
				// For now just log
			}

			sm->set("SyncedBFSettings.AllUnlocksUnlocked", "true");
			CYPRESS_LOGMESSAGE(LogLevel::Info, "Applied GW1 dedicated server settings");
		}
	}
}
#elif defined(CYPRESS_BFN)
DEFINE_HOOK(
	fb_Main_initSettings,
	__fastcall,
	void,

	void* thisPtr
)
{
	auto& options = *reinterpret_cast<eastl::vector<const char*>*>(OFFSET_ECDATA_START);

	std::stringstream cmdLine;
	cmdLine << "commandLine = {";

	if (!options.empty())
	{
		for (int o = 0; o < options.size(); o++)
		{
			const auto option = options.at(o);

			if (*option == '-')
			{
				std::string l_option = option + 1;
				const char* value = nullptr;

				std::transform(l_option.begin(), l_option.end(), l_option.begin(), tolower);

				cmdLine << "[ [==[" << l_option << "]==] ] = ";

				if (o + 1 < options.size())
				{
					value = options.at(o + 1);

					if (*value == '-')
						value = nullptr;
				}

				if (value)
					cmdLine << "[==[" << value << "]==], ";
				else
					cmdLine << "true, ";
			}

			cmdLine << "[" << o << "] = [==[" << option << "]==], ";
		}
	}

	cmdLine << "}";
	fb::ScriptContext::context()->executeString(cmdLine.str().c_str());

	CYPRESS_LOGMESSAGE(LogLevel::Info, "Applied commandline");

	Orig_fb_Main_initSettings(thisPtr);
}
#endif

DEFINE_HOOK(
	fb_Environment_getHostIdentifier,
	__cdecl,
	const char*
)
{
	if (!g_program->IsServer())
	{
		return g_program->GetClient()->GetPlayerName();
	}
	return Orig_fb_Environment_getHostIdentifier();
}

DEFINE_HOOK(
	fb_Console_writeConsole,
	__cdecl,
	void,

	const char* tag,
	const char* buffer,
	unsigned int size
)
{
	// we only care about messages tagged with ingame
	if (g_program->IsServer() && strcmp(tag, "ingame") == 0)
	{
		std::string_view msgView(buffer, size);
		CYPRESS_LOGTOSERVER(LogLevel::Info, "{}", msgView);
	}
	Orig_fb_Console_writeConsole(tag, buffer, size);
}

#ifdef CYPRESS_BFN
DEFINE_HOOK(
	fb_realMain,
	__cdecl,
	__int64,

	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	const char* lpCmdLine,
	__int64 a4,
	__int64 a5
)
{
	g_program->InitConfig();

	CYPRESS_LOGMESSAGE(LogLevel::Info, "Initializing {} (Cypress version {})", CYPRESS_GAME_NAME, GetCypressVersion().c_str());
	return Orig_fb_realMain(hInstance, hPrevInstance, lpCmdLine, a4, a5);
}

DEFINE_HOOK(
	fb_main_createWindow,
	__fastcall,
	void,

	void* thisPtr
)
{
	if (g_program->IsServer())
	{
		HBRUSH brush = GetSysColorBrush(COLOR_WINDOW);
		ptrset(thisPtr, 0x170, brush);
	}

	Orig_fb_main_createWindow(thisPtr);

	if (g_program->IsServer() && !g_program->IsEmbedded())
	{
		g_program->GetServer()->InitThinClientWindow();
	}

	// Hide the server window when running embedded, all output goes through the launcher
	if (g_program->IsServer() && g_program->IsEmbedded())
	{
		HWND* pWnd = (HWND*)0x14421BA88;
		if (pWnd && *pWnd)
			ShowWindow(*pWnd, SW_HIDE);
	}
}
#else
DEFINE_HOOK(
	fb_realMain,
	__cdecl,
	__int64,

	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	const char* lpCmdLine
)
{
	g_program->InitConfig();

	CYPRESS_LOGMESSAGE(LogLevel::Info, "Initializing {} (Cypress version {})", CYPRESS_GAME_NAME, GetCypressVersion().c_str());
	return Orig_fb_realMain(hInstance, hPrevInstance, lpCmdLine);
}

DEFINE_HOOK(
	fb_PVZMain_Ctor,
	__fastcall,
	__int64,

	void* a1,
	bool isDedicatedServer
)
{
	isDedicatedServer = g_program->IsServer();
	return Orig_fb_PVZMain_Ctor(a1, isDedicatedServer);
}

DEFINE_HOOK(
	fb_getServerBackendType,
	__fastcall,
	int,

	void* a1
)
{
	int origBackendType = Orig_fb_getServerBackendType(a1);
	if (g_program->IsServer())
		return 2; // Backend_Peer
	if (origBackendType == 0)
		return 3; // Backend_Local

	return origBackendType;
}

DEFINE_HOOK(
	fb_main_createWindow,
	__fastcall,
	__int64,

	HINSTANCE hInstance,
	bool dedicatedServer,
	bool consoleClient,
	DWORD gameLoopThreadId
)
{
	dedicatedServer = g_program->IsServer();
	// Suppress engine console window when running in embedded mode
	if (g_program->IsEmbedded())
		consoleClient = false;
	__int64 result = Orig_fb_main_createWindow(hInstance, dedicatedServer, consoleClient, gameLoopThreadId);

	// Hide the server window when running embedded, all output goes through the launcher
	if (g_program->IsServer() && g_program->IsEmbedded())
	{
		HWND* pWnd = (HWND*)OFFSET_MAINWND;
		if (pWnd && *pWnd)
			ShowWindow(*pWnd, SW_HIDE);
	}
	return result;
}

DEFINE_HOOK(
	fb_isInteractionAllowed,
	__fastcall,
	bool,

	void* a1,
	unsigned int a2
)
{
	return true;
}

DEFINE_HOOK(
	fb_tickersAllowedToShow,
	__fastcall,
	bool,

	void* a1
)
{
	return true;
}
#endif