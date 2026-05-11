#pragma once
#include <MemUtil.h>
#include <EASTL/string.h>
#ifdef CYPRESS_BFN
#include <EASTL/new_string.h>
#endif
#include <fb/Engine/Client.h>
#include <fb/SecureReason.h>

DECLARE_HOOK(
	fb_Client_enterState,
	__fastcall,
	void,

	void* thisPtr,
	fb::ClientState state,
	fb::ClientState prevState
);

#ifndef CYPRESS_BFN
DECLARE_HOOK(
	fb_140DA9B90,
	__fastcall,
	void*,

	void* a1,
	void* a2,
	int localPlayerId
);
#endif

DECLARE_HOOK(
	fb_OnlineManager_connectToAddress,
	__fastcall,
	void,

	void* thisPtr,
	const char* ipAddr,
	const char* serverPassword
);

#ifdef CYPRESS_BFN
DECLARE_HOOK(
	fb_OnlineManager_onGotDisconnected,
	__fastcall,
	void,

	void* thisPtr,
	fb::SecureReason reason,
	eastl::new_string* reasonText
);

DECLARE_HOOK(
	fb_EAUser_ctor,
	__fastcall,
	void*,

	void* thisPtr,
	int localPlayerId,
	void* a3,
	void* controller
);
#else
DECLARE_HOOK(
	fb_OnlineManager_onGotDisconnected,
	__fastcall,
	void,

	void* thisPtr,
	fb::SecureReason reason,
	eastl::string& reasonText
);

DECLARE_HOOK(
	fb_PVZGetNumTutorialVideos,
	__fastcall,
	void,

	void* thisPtr,
	void* dataKey
);

DECLARE_HOOK(
	fb_ClientConnection_onDisconnected,
	__fastcall,
	void,

	void* thisPtr
);
#endif