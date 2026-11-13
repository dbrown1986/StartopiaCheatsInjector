#include "pch.h"
#include <windows.h>
#include <iostream>
#include <cstdint>

// ============================================================================
// 1. ENGINE POINTERS & EXACT __fastcall SIGNATURE
// ============================================================================

void* volatile g_pEngineContext = nullptr;
volatile bool  g_bSpawnRequested = false;

uintptr_t g_jmpBackParser = 0;
uintptr_t g_jmpBackMainLoop = 0;

constexpr uintptr_t ADDR_PARSER_HOOK = 0x004ec270;
constexpr uintptr_t ADDR_SPAWN_SINGLE = 0x00435100;
constexpr uintptr_t ADDR_SPAWN_MULTI = 0x00435320;
int* g_pGameModeFlag = (int*)0x009b5588;

// Exact signature from Ghidra:
// uint __fastcall FUN_00435100(int param_1, undefined4 *param_2, undefined4 param_3, int param_4, int param_5, void *param_6)
// ECX = param_1 (0 = coords mode)
// EDX = param_2 (&packedCoords)
// Stack: param_3 (entityTypeID), param_4 (angle), param_5 (pContext), param_6 (nullptr)
typedef uint32_t(__fastcall* TrueSpawnFn)(
    int param_1,
    uint32_t* param_2,
    uint32_t param_3,
    int param_4,
    void* param_5,
    void* param_6
    );


// ============================================================================
// 2. HOOK INSTALLER
// ============================================================================

void InstallHook(uintptr_t targetAddress, void* hookFunction, int length) {
    DWORD oldProtect;
    VirtualProtect((void*)targetAddress, length, PAGE_EXECUTE_READWRITE, &oldProtect);

    uintptr_t relativeAddress = ((uintptr_t)hookFunction - targetAddress) - 5;
    *(BYTE*)targetAddress = 0xE9;
    *(uintptr_t*)(targetAddress + 1) = relativeAddress;

    for (int i = 5; i < length; i++) {
        *(BYTE*)(targetAddress + i) = 0x90;
    }

    VirtualProtect((void*)targetAddress, length, oldProtect, &oldProtect);
}


// ============================================================================
// 3. MAIN-THREAD SPAWN EXECUTOR (Called safely from game thread)
// ============================================================================

void __stdcall ExecuteSpawnOnMainThread() {
    if (!g_bSpawnRequested || g_pEngineContext == nullptr) {
        return;
    }
    g_bSpawnRequested = false;

    int deck = 0;       // 0 = sub_deck
    int x = 6;
    int y = 6;
    int angle = 1024;
    uint32_t saltHogID = 5; // PT_SALT_HOG (or try 17 if IDs are ordered top-down)

    // Exact coordinate packing from FUN_004ec270
    uint32_t packedCoords = (((deck & 3) << 5) | (y & 0x1F)) << 8 | (x & 0xFF);

    uintptr_t targetFn = (*g_pGameModeFlag == 0) ? ADDR_SPAWN_SINGLE : ADDR_SPAWN_MULTI;
    TrueSpawnFn spawnFn = (TrueSpawnFn)targetFn;

    // Pass nullptr for param_6 so FUN_00435100 builds its own clean attribute struct
    uint32_t result = spawnFn(0, &packedCoords, saltHogID, angle, g_pEngineContext, nullptr);

    std::cout << "[+] Main-thread spawn executed! Return value: " << result << std::endl;
}


// ============================================================================
// 4. NAKED HOOK AT 0x004ec270 (Captures EDX & Runs Queued Spawns on Main Thread)
// ============================================================================

__declspec(naked) void ParserAndMainThreadHook() {
    __asm {
        pushad
        pushfd

        // Capture param_2 from EDX whenever place_peep runs
        mov g_pEngineContext, edx

        popfd
        popad

        // Original 6-byte prologue at 0x004ec270
        push ebp
        mov ebp, esp
        sub esp, 0x78

        jmp[g_jmpBackParser]
    }
}

// Separate per-frame hook on PeekMessageA (always runs on the game's main UI/sim thread!)
typedef BOOL(WINAPI* PeekMessageAFn)(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
PeekMessageAFn oPeekMessageA = nullptr;
BYTE g_origPeekBytes[5] = { 0 };

BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    // Runs on the game's main thread every frame!
    if (g_bSpawnRequested && g_pEngineContext != nullptr) {
        ExecuteSpawnOnMainThread();
    }

    // Temporarily unhook, call original PeekMessageA, and re-hook
    DWORD oldProtect;
    VirtualProtect((void*)oPeekMessageA, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy((void*)oPeekMessageA, g_origPeekBytes, 5);
    BOOL res = oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    uintptr_t rel = ((uintptr_t)HookedPeekMessageA - (uintptr_t)oPeekMessageA) - 5;
    *(BYTE*)oPeekMessageA = 0xE9;
    *(uintptr_t*)((uintptr_t)oPeekMessageA + 1) = rel;
    VirtualProtect((void*)oPeekMessageA, 5, oldProtect, &oldProtect);

    return res;
}


// ============================================================================
// 5. BACKGROUND HOTKEY THREAD
// ============================================================================

DWORD WINAPI MainThread(LPVOID lpReserved) {
    Sleep(2000);
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    std::cout << "[+] Startopia Trainer Injected!" << std::endl;

    // 1. Hook FUN_004ec270 to capture EDX (param_2)
    g_jmpBackParser = ADDR_PARSER_HOOK + 6;
    InstallHook(ADDR_PARSER_HOOK, ParserAndMainThreadHook, 6);
    std::cout << "[+] Parser Hook Installed at 0x" << std::hex << ADDR_PARSER_HOOK << std::dec << std::endl;

    // 2. Hook User32!PeekMessageA so our spawn runs safely on the game's main thread
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        oPeekMessageA = (PeekMessageAFn)GetProcAddress(hUser32, "PeekMessageA");
        if (oPeekMessageA) {
            memcpy(g_origPeekBytes, (void*)oPeekMessageA, 5);
            InstallHook((uintptr_t)oPeekMessageA, HookedPeekMessageA, 5);
            std::cout << "[+] Main-Thread Dispatcher Hooked via PeekMessageA!" << std::endl;
        }
    }

    std::cout << "[!] Waiting for a script to execute place_peep..." << std::endl;

    bool contextLogged = false;

    while (true) {
        if (!contextLogged && g_pEngineContext != nullptr) {
            contextLogged = true;
            std::cout << "[+] Engine Context Captured: 0x" << std::hex << (uintptr_t)g_pEngineContext << std::dec << std::endl;
            std::cout << "[!] Ready! Press F1 to spawn a Salt Hog." << std::endl;
        }

        if (GetAsyncKeyState(VK_F1) & 1) {
            if (g_pEngineContext != nullptr) {
                std::cout << "[!] F1 Pressed -> Queuing spawn on main thread..." << std::endl;
                g_bSpawnRequested = true;
            }
            else {
                std::cout << "[-] Engine Context still NULL. Trigger a place_peep script first!" << std::endl;
            }
        }
        Sleep(50);
    }
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}