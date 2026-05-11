#pragma once
#include <fb/Engine/SettingsManager.h>

namespace Cypress
{
	/// <summary>
	/// Used to access Frostbite's settings containers.
	/// Looks up the container by name using the engine's SettingsManager.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	template<class T>
	class Settings
	{
	public:
		Settings(const char* identifier)
		{
			m_container = fb::SettingsManager::GetInstance()->getContainer<T>(identifier);
		}

		inline T* operator->() { return m_container; }
		inline const T* operator->() const { return m_container; }
		inline operator T* () { return m_container; }
		inline operator const T* () const { return m_container; }

	private:
		T* m_container;
	};
}