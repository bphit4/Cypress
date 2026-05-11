#include "pch.h"
#include <EASTL/internal/config.h>
#include <stdio.h>

using namespace fb;

void* fb::eastl_arena_allocator::allocate(size_t n, int flags)
{
	auto arenaAlloc = reinterpret_cast<void* (*)(void*, __int64 size, __int64 alignment)>(FB_MEMORYARENA_ALLOC_ADDRESS);
	if (!this->mpArena)
	{
		auto findArena = reinterpret_cast<void* (*)(void*, bool)>(FB_MEMORYARENA_FINDARENAFOROBJECT_ADDRESS);
		this->mpArena = findArena(this, true);
	}
	void* allocMem = arenaAlloc(this->mpArena, n, sizeof(void*));
	return allocMem;
}

void* fb::eastl_arena_allocator::allocate(size_t n, size_t alignment, size_t offset, int flags)
{
	auto arenaAlloc = reinterpret_cast<void* (*)(void*, __int64 size, __int64 alignment)>(FB_MEMORYARENA_ALLOC_ADDRESS);
	if (!this->mpArena)
	{
		auto findArena = reinterpret_cast<void* (*)(void*, bool)>(FB_MEMORYARENA_FINDARENAFOROBJECT_ADDRESS);
		this->mpArena = findArena(this, true);
	}
	void* allocMem = arenaAlloc(this->mpArena, n, alignment);
	return allocMem;
}

void fb::eastl_arena_allocator::deallocate(void* p, size_t n)
{
	auto arenaFree = reinterpret_cast<void(*)(void*, void*)>(FB_MEMORYARENA_FREE_ADDRESS);
	arenaFree(this->mpArena, p);
}

eastl_arena_allocator gDefaultAllocator;
eastl_arena_allocator* gpDefaultAllocator = &gDefaultAllocator;

eastl_arena_allocator* fb::GetDefaultAllocator()
{
	return gpDefaultAllocator;
}

eastl_arena_allocator* fb::SetDefaultAllocator(eastl_arena_allocator* pAllocator)
{
	eastl_arena_allocator* const pPrevAllocator = gpDefaultAllocator;
	gpDefaultAllocator = pAllocator;
	return pPrevAllocator;
}
