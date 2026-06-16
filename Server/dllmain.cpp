#include "pch.h"

#ifdef CYPRESS_CFB27

#include <Unknwn.h>

#pragma comment(lib, "Ws2_32.lib")

typedef HRESULT(*DirectInput8Create_t)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);
static DirectInput8Create_t DirectInput8CreateOriginal;

static void InitDirectInput8Exports()
{
    char dinputDLLName[MAX_PATH + 32];
    GetSystemDirectoryA(dinputDLLName, MAX_PATH);
    strcat_s(dinputDLLName, "\\dinput8.dll");

    HMODULE dinputModule = LoadLibraryA(dinputDLLName);
    if (!dinputModule)
        dinputModule = LoadLibraryA("dinput8_org.dll");
    if (!dinputModule)
        return;

    DirectInput8CreateOriginal = (DirectInput8Create_t)GetProcAddress(dinputModule, "DirectInput8Create");
}

extern "C"
{
    HRESULT __declspec(dllexport) DirectInput8Create(
        HINSTANCE hinst,
        DWORD dwVersion,
        REFIID riidltf,
        LPVOID* ppvOut,
        LPUNKNOWN punkOuter
    )
    {
        if (!DirectInput8CreateOriginal)
            InitDirectInput8Exports();
        if (DirectInput8CreateOriginal)
            return DirectInput8CreateOriginal(hinst, dwVersion, riidltf, ppvOut, punkOuter);
        return S_FALSE;
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD dwCallReason, LPVOID)
{
    if (dwCallReason == DLL_PROCESS_ATTACH)
        InitDirectInput8Exports();
    return TRUE;
}

#else

#include <include/Cypress/Core/Program.h>

#pragma comment(lib, "Ws2_32.lib")

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  dwCallReason,
                       LPVOID lpReserved
                     )
{
    if (dwCallReason == DLL_PROCESS_ATTACH)
    {
        g_program = new Cypress::Program(hModule);
    }
    else if (dwCallReason == DLL_PROCESS_DETACH)
    {
        delete g_program;
    }
    return TRUE;
}

#endif
