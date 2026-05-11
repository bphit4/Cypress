#pragma once
#include <Cypress/Presence/PresenceManager.h>
#include <fb/Engine/MessageListener.h>
#include <EASTL/string.h>
#include <EASTL/map.h>
#include <EASTL/fixed_map.h>
#include <fb/Engine/Message.h>
#include <fb/Engine/TypeInfo.h>

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
        union
        {
            bool Bool;
            float Float;
            int Int;
        };
        PVZRecordInfoType Type;
        bool UnkBool;

        PVZRecordInfo()
            : Int(0)
            , Type(Unknown)
            , UnkBool(false)
        {}

        bool GetBool() const
        {
            return Bool;
        }

        void SetBool(bool value)
        {
            Bool = value;
        }
    };
#ifdef CYPRESS_GW2
    static_assert(sizeof(PVZRecordInfo) == 0x30);
#endif

    struct PresencePVZUpdateByteVaultRecordMessage : public Message
    {
        eastl::string subcategory;
        eastl::map<eastl::string, PVZRecordInfo*>& records;
    };

    struct PresencePVZGetByteVaultSubRecordMessageBase : public Message
    {
        eastl::string subcategory;
    };

    struct PresencePVZGetByteVaultSubRecordResultMessageBase : public Message
    {
        PresencePVZGetByteVaultSubRecordResultMessageBase( const eastl::string& subcategory, eastl::map<eastl::string, PVZRecordInfo*>* recordResults)
            : Message( 0x9C775C5C, 0xF98CE8B8 )
            , subcategory(subcategory)
            , records(recordResults)
        {}

        ~PresencePVZGetByteVaultSubRecordResultMessageBase()
        {
            int bla = 0;
        }
        ClassInfo* getType() override {return nullptr;}

        eastl::string subcategory;
        eastl::map<eastl::string, PVZRecordInfo*>* records;
    };

    struct PresencePVZUpdateClientUtilManagerRecordMessageBase : public Message
    {
        eastl::string subcategory;
        eastl::map<eastl::string, PVZRecordInfo*>* DestRecordMap;
    };

    struct PresencePVZCustomizationGetBytevaultRecordResultMessageBase : public Message
    {
        eastl::string subcategory;
        int unkInt;
        bool unkBool;
    };

    class RecordValueHandler
    {
    public:
        int* ValuePtr;

        virtual void SetValue(const char* str);
    };

    struct PlayerRecordInfo
    {
        void* vftable;
        eastl::map<eastl::string, RecordValueHandler&> RecordHandlers;
        void* UnkPtr2;
        fb::PVZRecordInfo Records[0x5000];
        int UsedRecordCount;
        eastl::map<eastl::string, fb::PVZRecordInfo*> RecordMap;
        void* CustomizationInfo;
        char pad2[0x628];
        eastl::map<int, void*> CustomizationUnlockInfoMap;
        char pad3[0x18];
    };

    struct PlayerRecordEntry
    {
        int LocalPlayerIdMask;
        PlayerRecordInfo Info;
    };

    struct PVZClientUtil
    {
        void* vftable;
        // indexed by local player id
        eastl::fixed_map<int, PlayerRecordEntry, 2> PlayerRecords;
        // same as above but with a different value type
        eastl::map<int, int> unkmap;
    };

    struct Thing
    {
        char pad[0x38];
        eastl::map<eastl::string, PlayerRecordEntry*> map;
    };

    struct ClientBytevaultManager
    {
        char pad[0x30];
        Thing* ptrToThing;
    };
}

namespace Cypress
{
    class GW2PresenceManager : public PresenceManager, public fb::MessageListener
    {
    public:
        GW2PresenceManager();

        void Initialize() override;

    protected:
        void InitializePresenceState() override;

    private:
        void onMessage(fb::Message& inMessage) override;
        uint16_t ordering() const override { return 100; }
    };
}
