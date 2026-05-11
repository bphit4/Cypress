#include "pch.h"
#ifdef CYPRESS_GW2
#include "ServerEventSyncListener.h"
#include <Cypress/Core/Program.h>

#include <EASTL/map.h>

#include <fb/Engine/EventSyncReachedClientMessage.h>
#include <fb/Engine/ServerGameContext.h>
#include <fb/Engine/ServerEventSyncEntity.h>
#include <fb/TypeInfo/EventSyncEntityData.h>
#include <fb/TypeInfo/LogicPrefabReferenceObjectData.h>

DEFINE_HOOK(fb_ServerEventSyncEntity_Listener_onMessage, __fastcall, void,
	fb::ServerEventSyncEntity::Listener* a1, 
	fb::EventSyncReachedClientMessage* message)
{
	if (!g_program->GetServer()->GetAnticheat()->GetPreventBlacklistedEventSyncs() || !g_program->GetServer()->GetAnticheat()->GetEnabled())
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);

	if (!message->is("EventSyncReachedClientMessage"))
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Warning, "Message is not EventSyncReachedClientMessage", message->getType()->getName());
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	void* unk = message->m_serverConnection->validateLocalPlayer(message->m_localPlayerId, false);
	if (!unk)
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "Couldn't validate LocalPlayer");
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	fb::ServerPlayer* m_serverPlayer = ptrread<fb::ServerPlayer*>(unk, 0xF8);
	if (m_serverPlayer == nullptr)
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "serverPlayer is nullptr");
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	fb::ServerEventSyncListenerIds ble;
	ble.m_owner = message->m_somePtr - 2;
	ble.m_bus = message->m_unk3_0x60;
	ble.m_data = message->m_entityUid;

	auto it = a1->m_entities.find(ble);
	if (it == a1->m_entities.end())
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "No Entity Found");
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	if (it->second->getType() != fb::ServerEventSyncEntity::c_TypeInfo)
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "Value is not a ServerEventSyncEntity {}!", reinterpret_cast<void*>(it->second));
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	fb::ServerEventSyncEntity* serverEventSync = it->second;
	fb::EventSyncEntityData* data = serverEventSync->m_data;

	fb::EntityBus* bus = serverEventSync->m_entityBus;
	if (serverEventSync->m_entityBus == nullptr)
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "EntityBus is nullptr");
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	fb::LogicPrefabReferenceObjectData* prefab = reinterpret_cast<fb::LogicPrefabReferenceObjectData*>(bus->m_data);
	if (prefab == nullptr)
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "prefab is nullptr");
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	fb::Asset* blueprint = prefab->m_blueprint;
	if (blueprint == nullptr)
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Debug, "blueprint is nullptr");
		return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
	}

	if (isEventSyncBlacklisted(blueprint->Name, data->m_flags))
	{
		g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "Prevented execution of blacklisted EventSync from {} ({} | flags: {}).", m_serverPlayer->m_name, blueprint->Name, data->m_flags);
		return;
	}

	g_program->GetServer()->GetAnticheat()->AC_LogMessage(LogLevel::Info, "Executed EventSync by {} ({} | flags: {}).", m_serverPlayer->m_name, blueprint->Name, data->m_flags);
	return Orig_fb_ServerEventSyncEntity_Listener_onMessage(a1, message);
}
#endif