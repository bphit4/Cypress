#pragma once
#include "ITypedObject.h"
#include <cstdint>
#include <StringUtil.h>

namespace fb
{
	enum NetworkMessage : uint32_t
	{
		OnPlayerSpawnedMessage = 0xCAB70D78,
	};

	enum ClientWeaponMessage : uint32_t
	{
		PrimaryOutOfAmmoMessage = 0xA0325B03,
		ReloadBeginMessage = 0x929B8152,
	};

	enum ClientPVZCharacterMessage : uint32_t
	{
		DamagedEnemeyMessage = 0xBE722327,
	};

	enum UINetworkMessage : uint32_t
	{
		PlayerConnectMessage = 0xD3A50DC1,
		PlayerDisconnectMessage = 0xCDCD179F,
	};

	enum UIMessage : uint32_t
	{
		DamageMessage = 0x318336B9,
	};

	class Message : public ITypedObject
	{
	public:
		int m_category;
		int m_type;
		int m_localPlayerId;
		mutable int64_t m_dispatchTime;
		mutable const Message* m_next;
		mutable int m_postedAtProcessMessageCounter;
		mutable bool m_ownedByMessageManager;

		Message(int category, int type, int localPlayerId = 255)
			: m_category( category )
			, m_type(type)
			, m_localPlayerId( localPlayerId )
			, m_ownedByMessageManager( false )
		{}

		virtual ~Message() {}

		bool is(const char* messageName) {
			return m_type == fnvHash(messageName);
		}
	};
	static_assert(sizeof(Message) == 0x30);
}