#pragma once
#include <cstdint>
#include <MinHook.h>

template <typename T>
static T ptrread(void* ptr, size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) + offset);
}

template <typename T>
static void ptrset(void* ptr, size_t offset, T value)
{
    *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) + offset) = value;
}

static void MemSet(DWORD64 address, unsigned __int8 value, size_t size)
{
    DWORD dwOld = 0;
    VirtualProtect((PVOID*)(address), size, PAGE_EXECUTE_READWRITE, &dwOld);
    memset((void*)address, value, size);
    VirtualProtect((PVOID*)(address), size, dwOld, &dwOld);
}

static void MemPatch(DWORD64 address, unsigned __int8* pByte, int numberofbytestowrite)
{
    DWORD dwOld = 0;
    VirtualProtect((PVOID*)(address), numberofbytestowrite, PAGE_EXECUTE_READWRITE, &dwOld);
    memcpy((void*)address, (PBYTE*)pByte, numberofbytestowrite);
    VirtualProtect((PVOID*)(address), numberofbytestowrite, dwOld, &dwOld);
}

template <class T>
constexpr int SeeBits(T func)
{
    union
    {
        T ptr;
        int i;
    };
    ptr = func;

    return i;
}

template <class T>
constexpr int VFTableIndexOf(T func)
{
    return SeeBits(func) / sizeof(void*);
}

static void* GetVirtualFunc(void* obj, size_t index)
{
    void** vtable = *reinterpret_cast<void***>(obj);
    return vtable[index];
}

template <typename R, typename... Args>
R CallVirtualFunc(void* obj, size_t index, Args... args)
{
    void* func = GetVirtualFunc(obj, index);
    auto castedFunc = reinterpret_cast<R(*)(Args...)>(func);
    return castedFunc(args...);
}

template <typename R, typename... Args>
R CallFunc(uintptr_t address, Args... args)
{
    auto castedFunc = reinterpret_cast<R(*)(Args...)>(address);
    return castedFunc(args...);
}

// hook macros by NM
// DEFINE_HOOK and SETUP_HOOK require that the function body for the hook is defined manually after their use

#define DECLARE_HOOK(IN_FUNCTION_NAME, IN_CALL_CONVENTION, IN_RETURN_TYPE, ... /* Function arguments */) \
    typedef IN_RETURN_TYPE(IN_CALL_CONVENTION *PFN_##IN_FUNCTION_NAME)(__VA_ARGS__);     \
                                                                                         \
    extern PFN_##IN_FUNCTION_NAME Orig_##IN_FUNCTION_NAME;                               \
    extern FARPROC                OrigAddr_##IN_FUNCTION_NAME;                           \
                                                                                         \
    IN_RETURN_TYPE IN_CALL_CONVENTION Hk_##IN_FUNCTION_NAME(__VA_ARGS__);

#define DEFINE_HOOK(IN_FUNCTION_NAME, IN_CALL_CONVENTION, IN_RETURN_TYPE, ... /* Function arguments */) \
    FARPROC                OrigAddr_##IN_FUNCTION_NAME;                      \
    PFN_##IN_FUNCTION_NAME Orig_##IN_FUNCTION_NAME;                          \
                                                                             \
    IN_RETURN_TYPE IN_CALL_CONVENTION Hk_##IN_FUNCTION_NAME(__VA_ARGS__)

#define DISABLE_HOOK(IN_FUNCTION_NAME) MH_DisableHook(OrigAddr_##IN_FUNCTION_NAME);

#define ENABLE_HOOK(IN_FUNCTION_NAME)  MH_EnableHook(OrigAddr_##IN_FUNCTION_NAME);

#define INIT_HOOK(IN_FUNCTION_NAME, IN_ADDRESS) \
    OrigAddr_##IN_FUNCTION_NAME = reinterpret_cast<FARPROC>(IN_ADDRESS);                                                                \
                                                                                                                                        \
    MH_CreateHook(OrigAddr_##IN_FUNCTION_NAME, Hk_##IN_FUNCTION_NAME, reinterpret_cast<LPVOID *>(&Orig_##IN_FUNCTION_NAME));            \
                                                                                                                                        \
    ENABLE_HOOK(IN_FUNCTION_NAME);                                                                                                      \
    //FBML_LOGDEBUG("Hook initialized: {0} ({1:0>8X})", #IN_FUNCTION_NAME, reinterpret_cast<uintptr_t>(OrigAddr_##IN_FUNCTION_NAME));

#define REMOVE_HOOK(IN_FUNCTION_NAME) \
    DISABLE_HOOK(IN_FUNCTION_NAME);                                                                                                 \
                                                                                                                                    \
    MH_RemoveHook(OrigAddr_##IN_FUNCTION_NAME);                                                                                     \
                                                                                                                                    \
    Orig_##IN_FUNCTION_NAME = nullptr;                                                                                              \
                                                                                                                                    \
    //FBML_LOGDEBUG("Hook removed: {0} ({1:0>8X})", #IN_FUNCTION_NAME, reinterpret_cast<uintptr_t>(OrigAddr_##IN_FUNCTION_NAME));     \
    OrigAddr_##IN_FUNCTION_NAME = nullptr;

#define SETUP_HOOK(IN_FUNCTION_NAME, IN_CALL_CONVENTION, IN_RETURN_TYPE, ... /* Function arguments */) \
    typedef IN_RETURN_TYPE(IN_CALL_CONVENTION *PFN_##IN_FUNCTION_NAME)(__VA_ARGS__);     \
                                                                                         \
    DEFINE_HOOK(IN_FUNCTION_NAME, IN_CALL_CONVENTION, IN_RETURN_TYPE, __VA_ARGS__)
