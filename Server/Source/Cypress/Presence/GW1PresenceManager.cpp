#include <pch.h>
#include <Cypress/Presence/GW1PresenceManager.h>
#include <fb/Engine/ClientGameContext.h>

namespace Cypress
{
    GW1PresenceManager::GW1PresenceManager()
    {
        auto* clientCtx = fb::ClientGameContext::GetInstance();

        clientCtx->m_messageManager.registerMessageListener(fnvHashConstexpr( "Presence" ), this);
    }

    void GW1PresenceManager::Initialize()
    {
        InitializePresenceState();
        LoadSaveFile();
    }

    void GW1PresenceManager::InitializePresenceState()
    {
        void* presenceState = *(void**)0x142044038;
        CYPRESS_ASSERT( presenceState != nullptr, "Trying to create presence manager before engine's presence state has been created" );

        // change presencestate's state to Online so we get bytevault update requests from our client
#ifdef CYPRESS_GW1
        ptrset<int>(presenceState, 0xC4, 2);
#elif CYPRESS_GW2
        uint8_t* raw = (uint8_t*)presenceState;

        uint64_t base = *(uint64_t*)(raw + 0xDB9 * sizeof(uint64_t));

        // 0 = local player id
        uint32_t* statePtr = (uint32_t*)(base + 0xED8ull * 0 + 4);

        *statePtr = 2;
#else
#endif
    }

    void GW1PresenceManager::onMessage( fb::Message& inMessage )
    {
        switch (inMessage.m_type)
        {
            case fnvHashConstexpr( "PresencePVZGetByteVaultSubRecordMessageBase" ):
                {
                    auto& requestMsg = reinterpret_cast<fb::PresencePVZGetByteVaultSubRecordMessageBase&>(inMessage);
                    CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Requesting save category: {}", requestMsg.subcategory.c_str());

                    auto categoryIt = m_saveFile.find( requestMsg.subcategory.c_str() );
                    if (categoryIt != m_saveFile.end() && categoryIt->is_object())
                    {
                        eastl::map<eastl::string, fb::PVZRecordInfo> records;
                        for (auto& [key, entry] : categoryIt->items())
                        {
                            auto it = records.insert( key.c_str() );
                            auto& jvalue = it.first->second;

                            int type = entry["t"].get<int>();
                            jvalue.Type = (fb::PVZRecordInfoType)type;

                            switch (type)
                            {
                                case fb::Bool:    jvalue.SetBool(entry["v"].get<bool>()); break;
                                case fb::Integer: jvalue.Int = entry["v"].get<int>(); break;
                                case fb::Float:   jvalue.Float = entry["v"].get<float>(); break;
                                default: CYPRESS_ASSERT( false, "Unknown json type" );
                            }

                            jvalue.UnkBool = true;
                        }

                        auto* clientCtx = fb::ClientGameContext::GetInstance();

                        fb::Message* resultMsg = CallFunc<fb::Message*, const char*, void*>(
                            0x140BF1C50,
                        requestMsg.subcategory.c_str(), &records
                        );

                        clientCtx->m_messageManager.queueMessage( resultMsg );
                    }

                    break;
                }
            case fnvHashConstexpr( "PresencePVZUpdateByteVaultRecordMessageBase" ):
                {
                    auto& updateMsg = reinterpret_cast<fb::PresencePVZUpdateByteVaultRecordMessage&>(inMessage);
                    CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Updating save category: {}", updateMsg.subcategory.c_str());

                    auto& subcat = m_saveFile[updateMsg.subcategory.c_str()];
                    for (const auto& it : updateMsg.records)
                    {
                        int valueType = it.second.Type;

                        subcat[it.first.c_str()]["t"] = valueType;
                        switch (it.second.Type)
                        {
                            case fb::Bool:    subcat[it.first.c_str()]["v"] = it.second.GetBool(); break;
                            case fb::Integer: subcat[it.first.c_str()]["v"] = it.second.Int; break;
                            case fb::Float:   subcat[it.first.c_str()]["v"] = it.second.Float; break;
                            default: CYPRESS_ASSERT(false, "Unknown value type"); break;
                        }
                    }

                    std::ofstream saveout("cypsave.json");
                    saveout << m_saveFile.dump(2);
                    saveout.close();

                    break;
                }
        }
    }
}
