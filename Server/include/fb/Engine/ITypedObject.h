#pragma once
#include "fb/Engine/TypeInfo.h"

namespace fb
{
	class ITypedObject
	{
	public:
		virtual ClassInfo* getType() = 0;
	};
}