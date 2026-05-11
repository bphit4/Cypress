#pragma once

#include <EABase/eabase.h>

#define FB_MEMORYARENA_ALLOC_ADDRESS CYPRESS_GW_SELECT(0x140389540, 0x140155630, 0x140416050)
#define FB_MEMORYARENA_FREE_ADDRESS CYPRESS_GW_SELECT(0x140389FC0, 0x140155740, 0x140417740)
#define FB_MEMORYARENA_FINDARENAFOROBJECT_ADDRESS CYPRESS_GW_SELECT(0x140389E70, 0x1401CA550, 0x14042FE10)

namespace fb
{

	class eastl_arena_allocator
	{
	public:
		eastl_arena_allocator(void* arena) : mpArena(arena) {}
		explicit eastl_arena_allocator(const char* pName = nullptr) : mpArena(nullptr) {}
		explicit eastl_arena_allocator(const char* group, const char* name) : mpArena(nullptr) {}
		eastl_arena_allocator(const eastl_arena_allocator& x) : mpArena(x.mpArena) {}
		eastl_arena_allocator(const eastl_arena_allocator& x, const char* pName) : mpArena(x.mpArena) {}

		eastl_arena_allocator& operator=(const eastl_arena_allocator& x)
		{
			mpArena = x.mpArena;
			return *this;
		}
		eastl_arena_allocator& operator=(eastl_arena_allocator&& x)
		{
			mpArena = x.mpArena;
			return *this;
		}

		bool operator==(const eastl_arena_allocator& x)
		{
			return this->mpArena == x.mpArena;
		}

		void* allocate(size_t n, int flags = 0);
		void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0);
		void deallocate(void* p, size_t n);

		const char* get_name() const
		{
			return "";
		}
		void set_name(const char* pName) {}

		const void* get_arena()
		{
			return mpArena;
		}
		void set_arena(void* pArena)
		{
			mpArena = pArena;
		}

	protected:
		void* mpArena;
	};

	eastl_arena_allocator* GetDefaultAllocator();
	eastl_arena_allocator* SetDefaultAllocator(eastl_arena_allocator* pAllocator);

} // namespace fb
