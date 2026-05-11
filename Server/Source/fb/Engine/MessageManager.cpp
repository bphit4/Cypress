#include <pch.h>
#include <fb/Engine/MessageListener.h>
#include <fb/Engine/MessageManager.h>

namespace fb
{
    bool MessageManager::registerMessageListener(int category, MessageListener* listener, int localPlayerId)
    {
#ifdef CYPRESS_GW1
        bool registered = CallFunc<bool, MessageManagerImpl*, int, MessageListener*>(
            OFFSET_FB_REGISTERMESSAGELISTENER,
            m_impl, category, listener
            );
#else
        bool registered = CallFunc<bool, MessageManagerImpl*, int, MessageListener*, int>(
            OFFSET_FB_REGISTERMESSAGELISTENER,
            m_impl, category, listener, localPlayerId
            );
#endif
        CYPRESS_ASSERT(registered == true, "Failed to register message listener for {} messages!", category);

        CYPRESS_DEBUG_LOGMESSAGE(LogLevel::Debug, "Registered message listener for {} messages", category);
        return registered;
    }

    void MessageManager::queueMessage( Message* msg, float delay )
    {
        CallFunc<void, MessageManagerImpl*, Message*, float>( OFFSET_FB_MESSAGEMANAGER_QUEUEMESSAGE,
            m_impl, msg, delay );
    }

    void MessageManager::dispatchMessage( Message* msg )
    {
        CallFunc<void, MessageManagerImpl*, Message*>( OFFSET_FB_MESSAGEMANAGER_DISPATCHMESSAGE, m_impl, msg );
    }
}
