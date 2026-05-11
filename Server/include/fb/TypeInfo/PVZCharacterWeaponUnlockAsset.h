#ifndef PVZCHARACTERWEAPONUNLOCKASSET_H
#define PVZCHARACTERWEAPONUNLOCKASSET_H

#include "fb/TypeInfo/Asset.h"
#include <fb/Engine/Array.h>
#include <MemUtil.h>

namespace fb
{
	//lazy class
	class PVZCharacterWeaponUnlockAsset : public Asset
	{
	public:
		uint32_t getIdentifier()
		{
			return ptrread<uint32_t>( this, 0x28 );
		}

		fb::Array<fb::Asset*> getSelectableWeaponUpgrades() {
			void* WeaponUpgrades = ptrread<void*>(this, 0xE8);
			if (WeaponUpgrades == nullptr)
				return fb::Array<fb::Asset*>();

			fb::Array<fb::Asset*> SelectableUpgrades = ptrread<fb::Array<fb::Asset*>>(WeaponUpgrades, 0x10);
			return SelectableUpgrades;
		}

		fb::Array<fb::Asset*> getNonSelectableWeaponUpgrades() {
			void* WeaponUpgrades = ptrread<void*>(this, 0xE8);
			if (WeaponUpgrades == nullptr)
				return fb::Array<fb::Asset*>();

			fb::Array<fb::Asset*> NonSelectableUpgrades = ptrread<fb::Array<fb::Asset*>>(WeaponUpgrades, 0x18);
			return NonSelectableUpgrades;
		}
	};
} //namespace fb

#endif