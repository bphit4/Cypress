#include "pch.h"
#include "fbEnginePeerHooks.h"

#include <Cypress/Core/Program.h>
#include <Cypress/Core/Logging.h>
#include <Kyber/SocketManager.h>

#ifdef CYPRESS_BFN
DEFINE_HOOK(
	fb_EnginePeer_init,
	__fastcall,
	void,

	void* thisPtr,
	Kyber::SocketManager* socketManager,
	const char* address,
	int titleId,
	int versionId
)
{
	CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init address={}", address);

	if (!g_program->GetInitialized())
	{
		bool wsaInit = g_program->InitWSA();
		CYPRESS_ASSERT(wsaInit, "WSA failed to initialize!");
		g_program->SetInitialized(true);
	}

	char overrideAddrBuf[32] = {};
	if (strstr(address, ":251"))
	{
		char portBuf[16] = {};
		if (GetEnvironmentVariableA("CYPRESS_CLIENT_PORT", portBuf, sizeof(portBuf)) > 0 && portBuf[0] != '\0')
		{
			snprintf(overrideAddrBuf, sizeof(overrideAddrBuf), ":%s", portBuf);
			address = overrideAddrBuf;
			CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> overriding client port to {}", address);
		}
	}

#if(CAN_HOST_SERVER)
	if (strstr(address, ":252"))
	{
		char srvPortBuf[16] = {};
		if (GetEnvironmentVariableA("CYPRESS_SERVER_PORT", srvPortBuf, sizeof(srvPortBuf)) > 0 && srvPortBuf[0] != '\0')
		{
			// extract ip prefix ("ip:252xx" -> "ip") and reattach with assigned port
			const char* colon = strrchr(address, ':');
			if (colon)
			{
				size_t ipLen = colon - address;
				snprintf(overrideAddrBuf, sizeof(overrideAddrBuf), "%.*s:%s", (int)ipLen, address, srvPortBuf);
				address = overrideAddrBuf;
				CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> overriding server port to {}", address);
			}
		}
		CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> using Server (Clientbound) socket manager");
		socketManager = g_program->GetServer()->GetSocketManager();
	}
#endif
	if (strstr(address, ":251"))
	{
		CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> using Client (Serverbound) socket manager");
		socketManager = g_program->GetClient()->GetSocketManager();
	}

	Orig_fb_EnginePeer_init(thisPtr, socketManager, address, titleId, versionId);
}
#else

DEFINE_HOOK(
	fb_EnginePeer_init,
	__fastcall,
	void,

	void* thisPtr,
	void* crypto,
	Kyber::SocketManager* socketManager,
	const char* address,
	int titleId,
	int versionId
)
{
	CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init address={}", address);

	if (!g_program->GetInitialized())
	{
		bool wsaInit = g_program->InitWSA();
		CYPRESS_ASSERT(wsaInit, "WSA failed to initialize!");
		g_program->SetInitialized(true);
	}

	char overrideAddrBuf[32] = {};

#if(CAN_HOST_SERVER)
	if (strstr(address, ":252"))
	{
		char srvPortBuf[16] = {};
		if (GetEnvironmentVariableA("CYPRESS_SERVER_PORT", srvPortBuf, sizeof(srvPortBuf)) > 0 && srvPortBuf[0] != '\0')
		{
			const char* colon = strrchr(address, ':');
			if (colon)
			{
				size_t ipLen = colon - address;
				snprintf(overrideAddrBuf, sizeof(overrideAddrBuf), "%.*s:%s", (int)ipLen, address, srvPortBuf);
				address = overrideAddrBuf;
				CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> overriding server port to {}", address);
			}
		}
		CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> using Server (Clientbound) socket manager");
		socketManager = g_program->GetServer()->GetSocketManager();
	}
#endif
	if (strstr(address, ":251"))
	{
		char portBuf[16] = {};
		if (GetEnvironmentVariableA("CYPRESS_CLIENT_PORT", portBuf, sizeof(portBuf)) > 0 && portBuf[0] != '\0')
		{
			snprintf(overrideAddrBuf, sizeof(overrideAddrBuf), ":%s", portBuf);
			address = overrideAddrBuf;
			CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> overriding client port to {}", address);
		}

		CYPRESS_LOGMESSAGE(LogLevel::Info, "EnginePeer::init -> using Client (Serverbound) socket manager");
		socketManager = g_program->GetClient()->GetSocketManager();
	}

	Orig_fb_EnginePeer_init(thisPtr, crypto, socketManager, address, titleId, versionId);
}
#endif