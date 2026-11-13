#include "pch.h"
#include <windows.h>
#include "d3d8.h"
#include <cstdint>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx8.h"

#pragma comment(linker, "/export:Direct3DCreate8=_Proxy_Direct3DCreate8@4")
#pragma comment(linker, "/export:ValidatePixelShader=_Proxy_ValidatePixelShader@16")
#pragma comment(linker, "/export:ValidateVertexShader=_Proxy_ValidateVertexShader@20")
#pragma comment(linker, "/export:DebugSetMute=_Proxy_DebugSetMute@0")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// 0. CRASH HANDLER & DEBUG CONSOLE
// ============================================================================

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionInfo) {
    std::cout << "\n=========================================\n";
    std::cout << "[!] CRASH DETECTED!\n";
    std::cout << "[!] Exception Code: 0x" << std::hex << exceptionInfo->ExceptionRecord->ExceptionCode << "\n";
    std::cout << "[!] Fault Address:  0x" << std::hex << exceptionInfo->ExceptionRecord->ExceptionAddress << "\n";
    std::cout << "=========================================\n";
    std::cout << "The game has crashed. Please copy this output before closing.\n";

    MessageBoxA(NULL, "Startopia has crashed!\n\nPlease check the console window for the error address, then click OK to close.", "Trainer Crash Handler", MB_ICONERROR | MB_OK);

    return EXCEPTION_CONTINUE_SEARCH;
}

void InitializeDebugConsole() {
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);

    // Catch crashes so the console doesn't instantly vanish
    SetUnhandledExceptionFilter(CrashHandler);

    std::cout << "[+] Startopia Trainer Debug Console Initialized." << std::endl;
    std::cout << "[+] Crash Handler Active." << std::endl;
}

// ============================================================================
// 1. REAL D3D8.DLL FORWARDING EXPORTS
// ============================================================================

HMODULE g_hRealD3D8 = nullptr;

typedef IDirect3D8* (WINAPI* Direct3DCreate8Fn)(UINT SDKVersion);
typedef HRESULT(WINAPI* ValidatePixelShaderFn)(const DWORD* pPixelShader, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** ppErrors);
typedef HRESULT(WINAPI* ValidateVertexShaderFn)(const DWORD* pVertexShader, const DWORD* pDeclaration, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** ppErrors);
typedef void(WINAPI* DebugSetMuteFn)(void);

Direct3DCreate8Fn      oDirect3DCreate8 = nullptr;
ValidatePixelShaderFn  oValidatePixelShader = nullptr;
ValidateVertexShaderFn oValidateVertexShader = nullptr;
DebugSetMuteFn         oDebugSetMute = nullptr;

void LoadRealD3D8() {
    if (g_hRealD3D8) return;

    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    strcat_s(sysPath, "\\d3d8.dll");

    g_hRealD3D8 = LoadLibraryA(sysPath);
    if (g_hRealD3D8) {
        oDirect3DCreate8 = (Direct3DCreate8Fn)GetProcAddress(g_hRealD3D8, "Direct3DCreate8");
        oValidatePixelShader = (ValidatePixelShaderFn)GetProcAddress(g_hRealD3D8, "ValidatePixelShader");
        oValidateVertexShader = (ValidateVertexShaderFn)GetProcAddress(g_hRealD3D8, "ValidateVertexShader");
        oDebugSetMute = (DebugSetMuteFn)GetProcAddress(g_hRealD3D8, "DebugSetMute");
    }
}

extern "C" HRESULT WINAPI Proxy_ValidatePixelShader(const DWORD* pPixelShader, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** ppErrors) {
    LoadRealD3D8();
    return oValidatePixelShader ? oValidatePixelShader(pPixelShader, pCaps, ReturnErrors, ppErrors) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_ValidateVertexShader(const DWORD* pVertexShader, const DWORD* pDeclaration, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** ppErrors) {
    LoadRealD3D8();
    return oValidateVertexShader ? oValidateVertexShader(pVertexShader, pDeclaration, pCaps, ReturnErrors, ppErrors) : E_FAIL;
}

extern "C" void WINAPI Proxy_DebugSetMute(void) {
    LoadRealD3D8();
    if (oDebugSetMute) oDebugSetMute();
}

// ============================================================================
// 2. ENGINE SPAWNER STATE & SIGNATURES
// ============================================================================

void* volatile g_pEngineContext = nullptr;
uintptr_t g_jmpBackParser = 0;

constexpr uintptr_t ADDR_PARSER_HOOK = 0x004ec270;
constexpr uintptr_t ADDR_SPAWN_SINGLE = 0x00435100;
constexpr uintptr_t ADDR_SPAWN_MULTI = 0x00435320;
int* g_pGameModeFlag = (int*)0x009b5588;

typedef uint32_t(__fastcall* TrueSpawnFn)(
    int param_1,
    uint32_t* param_2,
    uint32_t param_3,
    int param_4,
    void* param_5,
    void* param_6
    );

struct SpawnRequest {
    bool pending = false;
    int raceID = 5;     // 5 = Salt Hog
    int deck = 0;       // 0 = Sub Deck, 1 = Engineering, 2 = Bio Deck
    int x = 6;
    int y = 6;
    int angle = 1024;
    int count = 1;
};

SpawnRequest g_SpawnReq;

// ============================================================================
// 3. HOOK HELPERS & ENGINE PARSER HOOK
// ============================================================================

void PatchBytes(void* dst, const void* src, size_t size) {
    DWORD oldProtect;
    VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(dst, src, size);
    VirtualProtect(dst, size, oldProtect, &oldProtect);
}

void InstallJmp(uintptr_t targetAddress, void* hookFunction, int length) {
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

__declspec(naked) void ParserContextHook() {
    __asm {
        pushad
        pushfd
        mov g_pEngineContext, edx
        popfd
        popad

        push ebp
        mov ebp, esp
        sub esp, 0x78
        jmp[g_jmpBackParser]
    }
}

void ProcessQueuedSpawns() {
    if (!g_SpawnReq.pending || g_pEngineContext == nullptr) return;
    g_SpawnReq.pending = false;

    uint32_t packedCoords = (((g_SpawnReq.deck & 3) << 5) | (g_SpawnReq.y & 0x1F)) << 8 | (g_SpawnReq.x & 0xFF);
    uintptr_t targetFn = (*g_pGameModeFlag == 0) ? ADDR_SPAWN_SINGLE : ADDR_SPAWN_MULTI;
    TrueSpawnFn spawnFn = (TrueSpawnFn)targetFn;

    std::cout << "[+] Executing spawn on main thread (Race: " << g_SpawnReq.raceID << ", Count: " << g_SpawnReq.count << ")...\n";

    for (int i = 0; i < g_SpawnReq.count; ++i) {
        spawnFn(0, &packedCoords, (uint32_t)g_SpawnReq.raceID, g_SpawnReq.angle, g_pEngineContext, nullptr);
    }
}

// ============================================================================
// 4. DIRECTX 8 & WNDPROC HOOKS
// ============================================================================

typedef HRESULT(APIENTRY* CreateDeviceFn)(IDirect3D8* pD3D, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface);
typedef HRESULT(APIENTRY* EndSceneFn)(IDirect3DDevice8* pDevice);
typedef HRESULT(APIENTRY* ResetFn)(IDirect3DDevice8* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);

CreateDeviceFn oCreateDevice = nullptr;
EndSceneFn     oEndScene = nullptr;
ResetFn        oReset = nullptr;
WNDPROC        oWndProc = nullptr;

BYTE g_origCreateDeviceBytes[5] = { 0 };
BYTE g_origEndSceneBytes[5] = { 0 };
BYTE g_origResetBytes[5] = { 0 };

bool g_bDeviceHooked = false;
bool g_bImGuiInitialized = false;
bool g_bShowMenu = true;

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Changed toggle to F12 to prevent camera control conflict
    if (uMsg == WM_KEYDOWN && wParam == VK_F12) {
        g_bShowMenu = !g_bShowMenu;
        std::cout << "[*] Toggled UI: " << (g_bShowMenu ? "ON" : "OFF") << std::endl;
        return 0;
    }

    if (g_bShowMenu && g_bImGuiInitialized) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
            return true;
        }
        if (ImGui::GetIO().WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) {
            return true;
        }
    }

    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

HRESULT APIENTRY HookedReset(IDirect3DDevice8* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (g_bImGuiInitialized) {
        ImGui_ImplDX8_InvalidateDeviceObjects();
    }

    PatchBytes((void*)oReset, g_origResetBytes, 5);
    HRESULT hr = oReset(pDevice, pPresentationParameters);
    InstallJmp((uintptr_t)oReset, HookedReset, 5);

    if (SUCCEEDED(hr) && g_bImGuiInitialized) {
        ImGui_ImplDX8_CreateDeviceObjects();
    }
    return hr;
}

HRESULT APIENTRY HookedEndScene(IDirect3DDevice8* pDevice) {
    if (!g_bImGuiInitialized) {
        D3DDEVICE_CREATION_PARAMETERS params;
        if (SUCCEEDED(pDevice->GetCreationParameters(&params))) {
            oWndProc = (WNDPROC)SetWindowLongPtrA(params.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.MouseDrawCursor = true;
            ImGui::StyleColorsDark();

            ImGui_ImplWin32_Init(params.hFocusWindow);
            ImGui_ImplDX8_Init(pDevice);
            g_bImGuiInitialized = true;
            std::cout << "[+] ImGui Overlay Initialized Successfully!" << std::endl;
        }
    }

    ProcessQueuedSpawns();

    if (g_bImGuiInitialized && g_bShowMenu) {
        DWORD prevZEnable = 0, prevAlphaBlend = 0, prevSrcBlend = 0, prevDestBlend = 0, prevLighting = 0;
        pDevice->GetRenderState(D3DRS_ZENABLE, &prevZEnable);
        pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevAlphaBlend);
        pDevice->GetRenderState(D3DRS_SRCBLEND, &prevSrcBlend);
        pDevice->GetRenderState(D3DRS_DESTBLEND, &prevDestBlend);
        pDevice->GetRenderState(D3DRS_LIGHTING, &prevLighting);

        ImGui_ImplDX8_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(340, 290), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Startopia Internal Trainer [F12]", &g_bShowMenu)) {
            if (g_pEngineContext == nullptr) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: Waiting for script context...");
                ImGui::TextWrapped("Trigger any place_peep script once to capture the engine context.");
            }
            else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Status: Engine Hooked (0x%p)", g_pEngineContext);
            }

            ImGui::Separator();

            const char* deckNames[] = { "0 - Sub Deck", "1 - Engineering Deck", "2 - Bio Deck" };
            ImGui::Combo("Deck", &g_SpawnReq.deck, deckNames, IM_ARRAYSIZE(deckNames));
            ImGui::SliderInt("Coord X", &g_SpawnReq.x, 1, 31);
            ImGui::SliderInt("Coord Y", &g_SpawnReq.y, 1, 31);
            ImGui::InputInt("Peep Race ID", &g_SpawnReq.raceID);
            ImGui::SliderInt("Count", &g_SpawnReq.count, 1, 10);

            ImGui::Spacing();
            ImGui::BeginDisabled(g_pEngineContext == nullptr);
            if (ImGui::Button("Spawn Peep", ImVec2(-1, 32))) {
                g_SpawnReq.pending = true;
            }
            ImGui::EndDisabled();
        }
        ImGui::End();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX8_RenderDrawData(ImGui::GetDrawData());

        pDevice->SetRenderState(D3DRS_ZENABLE, prevZEnable);
        pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, prevAlphaBlend);
        pDevice->SetRenderState(D3DRS_SRCBLEND, prevSrcBlend);
        pDevice->SetRenderState(D3DRS_DESTBLEND, prevDestBlend);
        pDevice->SetRenderState(D3DRS_LIGHTING, prevLighting);
    }

    PatchBytes((void*)oEndScene, g_origEndSceneBytes, 5);
    HRESULT hr = oEndScene(pDevice);
    InstallJmp((uintptr_t)oEndScene, HookedEndScene, 5);
    return hr;
}

HRESULT APIENTRY HookedCreateDevice(
    IDirect3D8* pD3D,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice8** ppReturnedDeviceInterface)
{
    PatchBytes((void*)oCreateDevice, g_origCreateDeviceBytes, 5);
    HRESULT hr = oCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    InstallJmp((uintptr_t)oCreateDevice, HookedCreateDevice, 5);

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface && !g_bDeviceHooked) {
        g_bDeviceHooked = true;
        void** deviceVTable = *reinterpret_cast<void***>(*ppReturnedDeviceInterface);

        oReset = (ResetFn)deviceVTable[14];
        oEndScene = (EndSceneFn)deviceVTable[35];

        memcpy(g_origResetBytes, (void*)oReset, 5);
        memcpy(g_origEndSceneBytes, (void*)oEndScene, 5);

        InstallJmp((uintptr_t)oReset, HookedReset, 5);
        InstallJmp((uintptr_t)oEndScene, HookedEndScene, 5);

        std::cout << "[+] DirectX 8 Device Hooked!" << std::endl;
    }

    return hr;
}

// ============================================================================
// 5. ENTRYPOINT: PROXY Direct3DCreate8
// ============================================================================

bool g_bTrainerInitialized = false;

extern "C" IDirect3D8* WINAPI Proxy_Direct3DCreate8(UINT SDKVersion) {
    LoadRealD3D8();
    if (!oDirect3DCreate8) return nullptr;

    if (!g_bTrainerInitialized) {
        g_bTrainerInitialized = true;
        InitializeDebugConsole();

        g_jmpBackParser = ADDR_PARSER_HOOK + 6;
        InstallJmp(ADDR_PARSER_HOOK, ParserContextHook, 6);
        std::cout << "[+] Game Context Hook Installed." << std::endl;
    }

    IDirect3D8* pD3D = oDirect3DCreate8(SDKVersion);
    if (pD3D && !oCreateDevice) {
        void** d3dVTable = *reinterpret_cast<void***>(pD3D);
        oCreateDevice = (CreateDeviceFn)d3dVTable[15];
        memcpy(g_origCreateDeviceBytes, (void*)oCreateDevice, 5);
        InstallJmp((uintptr_t)oCreateDevice, HookedCreateDevice, 5);
    }

    return pD3D;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Do NOT call AllocConsole() here. It is safely called inside Proxy_Direct3DCreate8.
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        if (g_hRealD3D8) {
            FreeLibrary(g_hRealD3D8);
        }
    }
    return TRUE;
}