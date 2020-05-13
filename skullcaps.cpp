
#define _CRT_SECURE_NO_WARNINGS

#define PAD_RIGHT 1
#define PAD_ZERO 2
#define PRINT_BUF_LEN 12

#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstdio>

HMODULE game;

#define GAME_OFFSET(off) (uintptr_t(game) + off + 0x1000)
#define HOOK(org, loc, other) { org = *(LPVOID*)GAME_OFFSET(loc); *(LPVOID*)GAME_OFFSET(loc) = other; }

#pragma comment(linker, "/EXPORT:DirectInputCreateA=_DirectInputCreateA@16")
HMODULE proxy = NULL;
uintptr_t _DirectInputCreateA = NULL;
extern "C" HRESULT __stdcall DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, PVOID ppDI, LPUNKNOWN punkOuter)
{
    proxy = LoadLibraryExA("C:\\Windows\\SysWOW64\\dinput.dll", NULL, NULL);

    if (_DirectInputCreateA == NULL && proxy)
        _DirectInputCreateA = (uintptr_t) GetProcAddress(proxy, "DirectInputCreateA");

    if (_DirectInputCreateA)
       return ((decltype(&DirectInputCreateA))_DirectInputCreateA)(hinst, dwVersion, ppDI, punkOuter);

    return NULL;
}

LPVOID _GetSystemMetrics;
int __stdcall GetSystemMetrics(int nIndex)
{
    // fake resolution of 640x480.
    if (nIndex == 0)
        return 640;

    if (nIndex == 1)
        return 480;

    ((decltype(&GetSystemMetrics))_GetSystemMetrics)(nIndex);
}

uint8_t counter = 0;
HWND wnd = NULL;
LPVOID _SetWindowPos;
BOOL __stdcall SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    // game startup...
    if (counter < 0xF)
    {
        // game colors are stuck in 32 bit mode while the game expects 16 bit.
        // ghetto fix this by spamming minimize / restore.
        if (counter++ < 2)
        {
            ((bool(_cdecl*)(bool))GAME_OFFSET(0x5cc30))(true);
            ((bool(_cdecl*)(bool))GAME_OFFSET(0x5c570))(true);
        }

        // don't ask...
        X = ((decltype(&GetSystemMetrics))_GetSystemMetrics)(0) / 2 - cx / 2;
        Y = ((decltype(&GetSystemMetrics))_GetSystemMetrics)(1) / 2 - cy / 2;
    }
    else
        uFlags |= SWP_NOMOVE;

    // don't ask about this either...
    cx += 1;
    cy += 23;

    return ((decltype(&SetWindowPos))_SetWindowPos)(wnd = hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

void __cdecl fn()
{
    if (*(bool*)GAME_OFFSET(0x12878a) || !wnd)
        return;

    RECT rc;
    GetClientRect(wnd, &rc);
    MapWindowPoints(wnd, NULL, (LPPOINT)&rc, 2);

    struct
    {
        int32_t x, y, w, h;
    } str{ 0, 0, 640, 480 };
    
    PDWORD* surface = (PDWORD*)GAME_OFFSET(0xc17e8);

    // a vtable with stdcalls in it, great stuff microsoft...
    if (!((bool(__stdcall*)(PDWORD))*(PDWORD*)(uintptr_t(*surface[0]) + 96))(surface[0])
        && !((bool(__stdcall*)(PDWORD))*(PDWORD*)(uintptr_t(*surface[1]) + 96))(surface[1]))
        ((void(__stdcall*)(PDWORD, DWORD, DWORD, PDWORD, PVOID, signed int))*(PDWORD*)(uintptr_t(*surface[0]) + 28))
            (surface[0], rc.left, rc.top, surface[1], &str, sizeof(str));

    // we just killed vsync, so pause the thread after each frame to keep the 'physics' in check.
    Sleep(1000 / 75);
}

LPVOID _WndProc = NULL;
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // stop recreation of directx context when focus is lost...
    // this causes some dumb render bugs because of our window displacment.
    if (uMsg == WM_ACTIVATEAPP)
    {
        *(bool*)GAME_OFFSET(0xc1204) = wParam;
        return NULL;
    }

    return ((decltype(&WndProc))_WndProc)(hwnd, uMsg, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DWORD old;
        MODULEINFO info;
        game = GetModuleHandle(NULL);
        GetModuleInformation(INVALID_HANDLE_VALUE, game, &info, sizeof(MODULEINFO));
        VirtualProtect(game, info.SizeOfImage, PAGE_EXECUTE_READWRITE, &old);

        // patch in window mode.
        *(uint8_t*)GAME_OFFSET(0x5c674) = 8; // NORMAL
        *(uint32_t*)GAME_OFFSET(0x5c6e2) = 0x90902DEB;
        *(uint32_t*)GAME_OFFSET(0x5c28c) |= WS_CAPTION;
        
        // skip some garbage.
        *(uint32_t*)GAME_OFFSET(0x47020) = 0x909090C3;

        // place hooks and fuck the function return predictor.
        HOOK(_GetSystemMetrics, 0x129560, GetSystemMetrics);
        HOOK(_SetWindowPos, 0x1295d8, SetWindowPos);
        HOOK(_WndProc, 0x5c1d3, WndProc);

        char jmp[] = "\x68\xEF\xBE\xAD\xDE\xC3";
        *(LPVOID*)(&jmp[1]) = fn;
        memcpy((PVOID)GAME_OFFSET(0x5e5c0), jmp, 6);
    }

    return TRUE;
}
