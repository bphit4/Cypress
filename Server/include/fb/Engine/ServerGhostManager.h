#pragma once
#include <Cypress/Core/Config.h>

namespace fb
{
    class ServerGhostManager
    {
    public:
        int ghostCount()
        {
#ifdef CYPRESS_GW1
            return reinterpret_cast<int(*)(void*)>(0x1407F8430)(ptrread<void*>(this, 0x3B8));
#elif defined(CYPRESS_GW2)
            return (*(unsigned int*)(this + 0x468) - *(unsigned int*)(this + 0x460)) >> 3;
#endif
        }
    }; //Size: 0x0080
}