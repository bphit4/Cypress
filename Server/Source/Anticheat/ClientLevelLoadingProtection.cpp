#include "pch.h"
#ifdef CYPRESS_GW2
#include "ClientLevelLoadingProtection.h"
#include <Cypress/Core/Program.h>

DEFINE_HOOK(
	fb_PVZServerLevelManager_onMessage,
	__fastcall,
	void,

	void* thisPtr,
	fb::Message* message
)
{
	if (!g_program->GetServer()->GetAnticheat()->GetPreventClientLevelLoading() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
		return Orig_fb_PVZServerLevelManager_onMessage(thisPtr, message);

	if (message->is("PVZNetworkLoadLevelMessage"))
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Warning, "Got PVZNetworkLoadLevelMessage");
		return;
	}

	Orig_fb_PVZServerLevelManager_onMessage(thisPtr, message);
}
#endif