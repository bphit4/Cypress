#pragma once
#include <Cypress/Presence/PresenceManager.h>
#include <fb/Engine/MessageListener.h>
#include <fb/Engine/Message.h>
#include <EASTL/string.h>
#include <EASTL/map.h>

namespace fb
{
    enum PVZRecordInfoType
    {
        Bool = 0,
        Integer = 2,
        Float = 3,
        Unknown = 4
    };

    struct PVZRecordInfo
    {
        eastl::string String;
        float Float;
        int Int;
        PVZRecordInfoType Type;
        bool UnkBool;
        bool Unk5;
        char pad[24];

        PVZRecordInfo()
            : Float(0.0f)
            , Int(0)
            , Type(Unknown)
            , UnkBool(false)
            , Unk5(false)
            , pad()
        {}

        bool GetBool() const
        {
            return Int;
        }

        void SetBool(bool value)
        {
            Int = value;
        }
    };
#ifdef CYPRESS_GW1
    static_assert( sizeof( eastl::pair<eastl::string, PVZRecordInfo> ) == 0x68 );
#endif

    struct PresencePVZUpdateByteVaultRecordMessage : public Message
    {
        eastl::string subcategory;
        eastl::map<eastl::string, PVZRecordInfo> records;
    };

    struct PresencePVZGetByteVaultSubRecordMessageBase : public Message
    {
        eastl::string subcategory;
    };

    struct PresencePVZGetByteVaultSubRecordResultMessageBase : public Message
    {
        eastl::string subcategory;
        eastl::map<eastl::string, PVZRecordInfo> records;
    };
}

namespace Cypress
{
    class GW1PresenceManager : public PresenceManager, public fb::MessageListener
    {
    public:
        GW1PresenceManager();

        void Initialize() override;

    protected:
        void InitializePresenceState() override;

    private:
        void onMessage(fb::Message& inMessage) override;
        uint16_t ordering() const override { return 100; }
    };
}
