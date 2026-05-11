#include <pch.h>
#include <Cypress/Presence/GW2PresenceManager.h>
#include <fb/Engine/ClientGameContext.h>

namespace Cypress
{
    GW2PresenceManager::GW2PresenceManager()
    {
        auto* clientCtx = fb::ClientGameContext::GetInstance();

        clientCtx->m_messageManager.registerMessageListener(fnvHashConstexpr( "Presence" ), this);
        clientCtx->m_messageManager.registerMessageListener(fnvHashConstexpr( "Client" ), this);
    }

    void GW2PresenceManager::Initialize()
    {
        InitializePresenceState();
        LoadSaveFile();
    }

    void GW2PresenceManager::InitializePresenceState()
    {
        void* presenceState = *(void**)0x142B69630;
        CYPRESS_ASSERT( presenceState != nullptr, "Trying to create presence manager before engine's presence state has been created" );

        // change presencestate's state to Online so we get bytevault update requests from our client
        uint8_t* raw = (uint8_t*)presenceState;

        uint64_t base = *(uint64_t*)(raw + 0xDB9 * sizeof(uint64_t));

        // 0 = local player id
        uint32_t* statePtr = (uint32_t*)(base + 0xED8ull * 0 + 4);

        *statePtr = 2;
    }

    void GW2PresenceManager::onMessage( fb::Message& inMessage )
    {
        if (inMessage.m_category == fnvHashConstexpr( "Client" ))
        {
            switch (inMessage.m_type)
            {
                case fnvHashConstexpr( "ClientLevelUnloadedMessage" ):
                    {
                        CYPRESS_LOGMESSAGE( LogLevel::Debug, "Flushing save file" );
                        std::ofstream saveout("cypsave.json");
                        saveout << m_saveFile.dump(2);
                        saveout.close();

                        break;
                    }
            }
        }
        else if (inMessage.m_category == fnvHashConstexpr( "Presence" ))
        {
         switch (inMessage.m_type)
        {
            case fnvHashConstexpr( "PresencePVZCustomizationGetBytevaultRecordResultMessageBase" ):
                {
                    auto& msg = reinterpret_cast<fb::PresencePVZCustomizationGetBytevaultRecordResultMessageBase&>(inMessage);

                    fb::PVZClientUtil* cu = *(fb::PVZClientUtil**)0x142B69318;

                    auto cuIt = cu->PlayerRecords.find( 0 );
                    auto& presenceStruct = cuIt->second.Info;

                    auto categoryIt = m_saveFile.find( "C" );
                    if (categoryIt != m_saveFile.end() && categoryIt->is_object())
                    {
                        for (auto& [key, entry] : categoryIt->items())
                        {
                            auto recordIt = presenceStruct.RecordMap.find( key.c_str() );
                            if (recordIt == presenceStruct.RecordMap.end())
                                continue;

                            auto& record = *recordIt->second;
                            int type = entry["t"].get<int>();
                            record.Type = (fb::PVZRecordInfoType)type;

                            switch (type)
                            {
                                case 1:
                                case fb::Bool:    record.SetBool(entry["v"].get<bool>()); break;
                                case fb::Integer: record.Int = entry["v"].get<int>(); break;
                                case fb::Float:   record.Float = entry["v"].get<float>(); break;
                                default: CYPRESS_ASSERT( false, "Unknown json type" );
                            }

                            record.UnkBool = true;
                        }
                    }

                    // Tell PVZClientUtil to update its mappings
                    void* msgMem = alloca(sizeof(fb::PresencePVZUpdateClientUtilManagerRecordMessageBase));
                    CallFunc<void, void*, const char*, void*, int>( 0x140C125A0,
                        msgMem, "C", &presenceStruct.RecordMap, 0);
                    fb::ClientGameContext::GetInstance()->m_messageManager.dispatchMessage( (fb::Message*)msgMem );
                    CallFunc<void, void*>( 0x140C12550, msgMem );

                    // Now tell the customization system to update
                    CallFunc<void, void*, int>( 0x140CAD770, cu, 0 );
                }
            /*case fnvHashConstexpr( "PresencePVZUpdateClientUtilManagerRecordMessageBase" ):
                {
                    auto& requestMsg = reinterpret_cast<fb::PresencePVZUpdateClientUtilManagerRecordMessageBase&>(inMessage);
                    CYPRESS_LOGMESSAGE( LogLevel::Debug, "ClientUtil update request for: {}", requestMsg.subcategory.c_str() );

                    break;
                }*/
            case fnvHashConstexpr( "PresencePVZGetByteVaultSubRecordMessageBase" ):
                {
                    auto& requestMsg = reinterpret_cast<fb::PresencePVZGetByteVaultSubRecordMessageBase&>(inMessage);
                    CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Requesting save category: {}", requestMsg.subcategory.c_str());

                    auto categoryIt = m_saveFile.find( requestMsg.subcategory.c_str() );

                    fb::ClientBytevaultManager* bym = *(fb::ClientBytevaultManager**)0x142B6A218;

                    // [] function i think
                    fb::PlayerRecordInfo* presenceStruct = CallFunc<fb::PlayerRecordInfo*, void*, const eastl::string&>(
                        0x140E4DC20,
                        &bym->ptrToThing->map, requestMsg.subcategory);

                    if (categoryIt != m_saveFile.end() && categoryIt->is_object())
                    {
                        for (auto& [key, entry] : categoryIt->items())
                        {
                            auto recordIt = presenceStruct->RecordMap.find( key.c_str() );
                            if (recordIt == presenceStruct->RecordMap.end())
                                continue;

                            auto& record = *recordIt->second;
                            int type = entry["t"].get<int>();
                            record.Type = (fb::PVZRecordInfoType)type;

                            switch (type)
                            {
                                case 1:
                                case fb::Bool:    record.SetBool(entry["v"].get<bool>()); break;
                                case fb::Integer: record.Int = entry["v"].get<int>(); break;
                                case fb::Float:   record.Float = entry["v"].get<float>(); break;
                                default: CYPRESS_ASSERT( false, "Unknown json type" );
                            }

                            record.UnkBool = true;
                        }
                    }

                    fb::PresencePVZGetByteVaultSubRecordResultMessageBase resultMsg(requestMsg.subcategory, &presenceStruct->RecordMap);
                    ptrset<uintptr_t>(&resultMsg, 0x0, 0x14230FB48);
                    resultMsg.m_localPlayerId = 0;

                    fb::ClientGameContext* ctx = fb::ClientGameContext::GetInstance();
                    //ctx->m_messageManager.dispatchMessage( &resultMsg );

                    break;
                }
            case fnvHashConstexpr( "PresencePVZUpdateByteVaultRecordMessageBase" ):
                {
                    auto& updateMsg = reinterpret_cast<fb::PresencePVZUpdateByteVaultRecordMessage&>(inMessage);
                    CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Updating save category: {}", updateMsg.subcategory.c_str());

                    auto& subcat = m_saveFile[updateMsg.subcategory.c_str()];
                    for (const auto& it : updateMsg.records)
                    {
                        int valueType = it.second->Type;

                        subcat[it.first.c_str()]["t"] = valueType;
                        switch (it.second->Type)
                        {
                            case 1:
                            case fb::Bool:    subcat[it.first.c_str()]["v"] = it.second->GetBool(); break;
                            case fb::Integer: subcat[it.first.c_str()]["v"] = it.second->Int; break;
                            case fb::Float:   subcat[it.first.c_str()]["v"] = it.second->Float; break;
                            default: CYPRESS_ASSERT(false, "Unknown value type"); break;
                        }
                    }

                    break;
                }
        }
        }
    }
}
