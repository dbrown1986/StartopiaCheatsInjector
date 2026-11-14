#include "pch.h"
#include <windows.h>
#include <gl/GL.h>
#include <cstdint>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl2.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// 1. ENGINE SPAWNER STATE
// ============================================================================

void* volatile g_pEngineContext = nullptr;
uintptr_t g_jmpBackParser = 0;

constexpr uintptr_t ADDR_PARSER_HOOK = 0x004ec270;
constexpr uintptr_t ADDR_SPAWN_SINGLE = 0x00435100;
constexpr uintptr_t ADDR_SPAWN_MULTI = 0x00435320;
int* g_pGameModeFlag = (int*)0x009b5588;

typedef uint32_t(__fastcall* TrueSpawnFn)(int param_1, uint32_t* param_2, uint32_t param_3, int param_4, void* param_5, void* param_6);

struct SpawnRequest {
    bool pending = false;
    int raceID = 5;
    int deck = 0;
    int x = 6;
    int y = 6;
    int angle = 1024;
    int count = 1;
};
SpawnRequest g_SpawnReq;

void InstallJmp(uintptr_t targetAddress, void* hookFunction, int length) {
    DWORD oldProtect;
    VirtualProtect((void*)targetAddress, length, PAGE_EXECUTE_READWRITE, &oldProtect);
    uintptr_t relativeAddress = ((uintptr_t)hookFunction - targetAddress) - 5;
    *(BYTE*)targetAddress = 0xE9;
    *(uintptr_t*)(targetAddress + 1) = relativeAddress;
    for (int i = 5; i < length; i++) *(BYTE*)(targetAddress + i) = 0x90;
    VirtualProtect((void*)targetAddress, length, oldProtect, &oldProtect);
}

__declspec(naked) void ParserContextHook() {
    __asm {
        pushad
        pushfd
        mov eax, edx
        mov dword ptr[g_pEngineContext], eax
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

    for (int i = 0; i < g_SpawnReq.count; ++i) {
        spawnFn(0, &packedCoords, (uint32_t)g_SpawnReq.raceID, g_SpawnReq.angle, g_pEngineContext, nullptr);
    }
}

// ============================================================================
// 2. OPENGL INTERCEPTOR
// ============================================================================

typedef BOOL(WINAPI* wglSwapBuffersFn)(HDC hdc);
wglSwapBuffersFn oWglSwapBuffers = nullptr;
WNDPROC oWndProc = nullptr;

bool g_bImGuiInitialized = false;
bool g_bShowMenu = true;

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_F12) {
        g_bShowMenu = !g_bShowMenu;
        return 0;
    }
    if (g_bShowMenu && g_bImGuiInitialized) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) return true;
        if (ImGui::GetIO().WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) return true;
    }
    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

BOOL WINAPI HookedWglSwapBuffers(HDC hdc) {
    if (wglGetCurrentContext() == nullptr) {
        return oWglSwapBuffers(hdc);
    }

    if (!g_bImGuiInitialized) {
        HWND hWindow = WindowFromDC(hdc);
        oWndProc = (WNDPROC)SetWindowLongPtr(hWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDrawCursor = true;
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hWindow);
        ImGui_ImplOpenGL2_Init();
        g_bImGuiInitialized = true;
    }

    ProcessQueuedSpawns();

    if (g_bImGuiInitialized && g_bShowMenu) {
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(340, 290), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Startopia WineD3D Trainer [F12]", &g_bShowMenu)) {
            if (g_pEngineContext == nullptr) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Waiting for Context...");
            }
            else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Engine Hooked (0x%p)", g_pEngineContext);
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
            if (ImGui::Button("Spawn Entity", ImVec2(-1, 32))) g_SpawnReq.pending = true;
            ImGui::EndDisabled();
        }
        ImGui::End();
        ImGui::Render();

        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    }

    return oWglSwapBuffers(hdc);
}

// ============================================================================
// 3. INJECTION THREAD
// ============================================================================

DWORD WINAPI InitThread(LPVOID lpReserved) {
    g_jmpBackParser = ADDR_PARSER_HOOK + 6;
    InstallJmp(ADDR_PARSER_HOOK, ParserContextHook, 6);

    HMODULE hOpenGL = GetModuleHandleA("opengl32.dll");
    if (!hOpenGL) hOpenGL = LoadLibraryA("opengl32.dll");

    if (hOpenGL) {
        oWglSwapBuffers = (wglSwapBuffersFn)GetProcAddress(hOpenGL, "wglSwapBuffers");
        if (oWglSwapBuffers) {
            DWORD oldProtect;
            VirtualProtect((void*)oWglSwapBuffers, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
            uintptr_t relAddr = ((uintptr_t)HookedWglSwapBuffers - (uintptr_t)oWglSwapBuffers) - 5;
            *(BYTE*)oWglSwapBuffers = 0xE9;
            *(uintptr_t*)((uintptr_t)oWglSwapBuffers + 1) = relAddr;
            VirtualProtect((void*)oWglSwapBuffers, 5, oldProtect, &oldProtect);
        }
    }
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
    }
    return TRUE;
}