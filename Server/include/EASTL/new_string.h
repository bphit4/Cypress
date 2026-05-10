#pragma once
#include <string>
#include "eastl_arena_allocator.h"

#ifdef CYPRESS_BFN
namespace eastl
{
    //BFN uses an updated basic_string
    class new_string {
    private:
        char* m_str;
        int m_size;
        short m_unk1;
        uint8_t m_unk2;
        uint8_t m_unk3;
        fb::eastl_arena_allocator m_allocator;
    public:

        new_string() : m_str(nullptr), m_size(0), m_unk1(0), m_unk2(0), m_unk3(0xF) {}

        new_string(const char* in) : m_str(nullptr), m_size(0), m_unk1(0), m_unk2(0), m_unk3(0xF) {
            init(in);
        }

        ~new_string() {
            this->clear();
        }

        bool isInline() const { return (m_unk3 & 0x80) == 0; }

        int size() const {
            if (isInline())
                return strlen(reinterpret_cast<const char*>(this));

            return m_size;
        }

        bool empty() const { return size() == 0; }

        const char* c_str() const {
            if (isInline())
                return reinterpret_cast<const char*>(this);
            return m_str;
        }

        void clear() {
            if (!isInline())
                m_allocator.deallocate(m_str, strlen(m_str) + 1);
        }

        void init(const char* str)
        {
            using tEASTLStringSet = void(*)(new_string*, const char*, const char* end);
            auto eastlStringSet = reinterpret_cast<tEASTLStringSet>(0x140306420);

            eastlStringSet(this, str, str + strlen(str));
        }

        void set(const char* str)
        {
            new_string tmp(str);

            using tCopyFrom = __int64(*)(new_string* left, const char* right, uint32_t end);
            auto copyFrom = reinterpret_cast<tCopyFrom>(0x14041B490);

            copyFrom(this, tmp.c_str(), (uint32_t)(tmp.c_str() + tmp.size()));
        }

        void operator=(const char* str) { set(str); }
    };
}
#endif
