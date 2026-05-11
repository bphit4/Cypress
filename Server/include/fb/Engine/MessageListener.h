#pragma once
#include <cstdint>
#include <StringUtil.h>
#include <fb/Engine/Message.h>

namespace fb
{
	class MessageListener
	{
	public:
		enum
		{
			kDefaultOrdering = 100
		};

		virtual void     onMessage(fb::Message& inMessage) = 0;
		virtual uint16_t ordering() const { return kDefaultOrdering; }
	};
}