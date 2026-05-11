#include "pch.h"
#ifdef CYPRESS_GW2
#include "ServerSettingEntityListener.h"

#include <fb/TypeInfo/SettingEntityData.h>

#include "Cypress/Core/Program.h"
#include "fb/Engine/NetworkableMessage.h"
#include <fb/Engine/ServerPlayer.h>

DEFINE_HOOK(
	fb_ServerSettingEntity_onMessage,
	__fastcall,
	void,

	void* thisPtr,
	fb::Message* message,
	__int64 a3, //unused
	__int64 a4, //unused
	int a5 //unused
)
{
	if (message->is("NetworkSettingsSyncFromClientMessage"))
	{
		if (!g_program->GetServer()->GetAnticheat()->GetPreventSyncSettingsFromClients() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
			return Orig_fb_ServerSettingEntity_onMessage(thisPtr, message, a3, a4, a5);

		fb::NetworkableMessage* msg = static_cast<fb::NetworkableMessage*>(message);

		void* unk = msg->m_serverConnection->validateLocalPlayer(msg->m_localPlayerId, false);

		if (!unk)
		{
			g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "[{}] Couldn't validate LocalPlayer!", msg->getType()->getName());
			return Orig_fb_ServerSettingEntity_onMessage(thisPtr, message, a3, a4, a5);
		}

		fb::ServerPlayer* serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);

		const char* playerName = serverPlayer ? serverPlayer->m_name : "Null player";

		fb::SettingEntityData* settingEntityData = ptrread<fb::SettingEntityData*>(thisPtr, -0x30);
		if (!settingEntityData)
		{
			g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Warning, "[{}] settingEntityData is nullptr", msg->getType()->getName());
			return Orig_fb_ServerSettingEntity_onMessage(thisPtr, message, a3, a4, a5);
		}

		std::string setting = settingEntityData->getSettingName();
		std::string value = settingEntityData->getSettingValue();

		if (g_settingBlacklist.count(setting))
		{
			g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "[{}] {} tried to change server setting: {} = {}", msg->getType()->getName(), playerName, setting, value);
			return;
		}

		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "[{}] {} changed a server setting: {} = {}", msg->getType()->getName(), playerName, setting, value);
	}

	Orig_fb_ServerSettingEntity_onMessage(thisPtr, message, a3, a4, a5);
}
#endif