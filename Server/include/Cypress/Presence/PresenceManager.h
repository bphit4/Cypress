#pragma once
#include <nlohmann/json.hpp>

namespace Cypress
{
    class PresenceManager
    {
    public:
        static void Create();
        virtual void Initialize() = 0;

    protected:
        virtual void InitializePresenceState() = 0;
        virtual bool LoadSaveFile();

        nlohmann::json m_saveFile;
    };

    extern PresenceManager* g_presenceManager;
}