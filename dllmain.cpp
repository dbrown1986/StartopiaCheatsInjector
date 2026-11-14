#include <windows.h>
#include <ddraw.h>
#include <iostream>
#include <cstdint>

// 1. Export the legacy DirectDraw functions so the game doesn't crash on load
#pragma comment(linker, "/export:DirectDrawCreate=c:\\windows\\system32\\ddraw.DirectDrawCreate")
#pragma comment(linker, "/export:DirectDrawEnumerateA=c:\\windows\\system32\\ddraw.DirectDrawEnumerateA")
#pragma comment(linker, "/export:DirectDrawEnumerateW=c:\\windows\\system32\\ddraw.DirectDrawEnumerateW")
#pragma comment(linker, "/export:DirectDrawEnumerateExA=c:\\windows\\system32\\ddraw.DirectDrawEnumerateExA")
#pragma comment(linker, "/export:DirectDrawEnumerateExW=c:\\windows\\system32\\ddraw.DirectDrawEnumerateExW")
#pragma comment(linker, "/export:DllCanUnloadNow=c:\\windows\\system32\\ddraw.DllCanUnloadNow")
#pragma comment(linker, "/export:DllGetClassObject=c:\\windows\\system32\\ddraw.DllGetClassObject")

// Define the function signatures for the real DirectDraw APIs
typedef HRESULT(WINAPI* DirectDrawCreateExFn)(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);
typedef HRESULT(WINAPI* CreateSurfaceFn)(IDirectDraw7* pDD, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE7* lplpDDSurface, IUnknown* pUnkOuter);
typedef HRESULT(WINAPI* FlipFn)(IDirectDrawSurface7* pSurface, LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags);

// Globals to hold original functions
DirectDrawCreateExFn oDirectDrawCreateEx = nullptr;
CreateSurfaceFn      oCreateSurface = nullptr;
FlipFn               oFlip = nullptr;

// A simple byte-patching helper for VTable hooks
void PatchVTable(void** vtable, int index, void* hookFn, void** origFn) {
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    *origFn = vtable[index];
    vtable[index] = hookFn;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

// ============================================================================
// 4. THE FLIP HOOK (The Render Loop)
// ============================================================================
HRESULT WINAPI HookedFlip(IDirectDrawSurface7* pSurface, LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags) {

    // TODO: This is where we will inject the custom ImGui_ImplDX7_RenderDrawData
    // and draw directly to the pSurface memory buffer.

    // Call the original Flip to actually push the frame to the monitor
    return oFlip(pSurface, lpDDSurfaceTargetOverride, dwFlags);
}

// ============================================================================
// 3. THE CREATE SURFACE HOOK
// ============================================================================
HRESULT WINAPI HookedCreateSurface(IDirectDraw7* pDD, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE7* lplpDDSurface, IUnknown* pUnkOuter) {

    // Let the real DX7 engine create the surface
    HRESULT hr = oCreateSurface(pDD, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);

    // If it successfully created the PRIMARY display surface, hook its Flip function
    if (SUCCEEDED(hr) && lplpDDSurface && *lplpDDSurface) {
        if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) {

            void** surfaceVTable = *reinterpret_cast<void***>(*lplpDDSurface);

            // Flip is index 11 in the IDirectDrawSurface7 VTable
            if (!oFlip) {
                PatchVTable(surfaceVTable, 11, (void*)HookedFlip, (void**)&oFlip);
                std::cout << "[+] DX7 Primary Surface Intercepted. Flip Hooked!" << std::endl;
            }
        }
    }
    return hr;
}

// ============================================================================
// 2. THE DIRECTDRAWCREATEEX EXPORT PROXY
// ============================================================================
extern "C" HRESULT WINAPI DirectDrawCreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter) {

    // Call the real Windows ddraw.dll
    HRESULT hr = oDirectDrawCreateEx(lpGuid, lplpDD, iid, pUnkOuter);

    // If Startopia successfully creates the DX7 Engine, hijack its VTable
    if (SUCCEEDED(hr) && lplpDD && *lplpDD) {
        void** ddrawVTable = *reinterpret_cast<void***>(*lplpDD);

        // CreateSurface is index 6 in the IDirectDraw7 VTable
        if (!oCreateSurface) {
            PatchVTable(ddrawVTable, 6, (void*)HookedCreateSurface, (void**)&oCreateSurface);
            std::cout << "[+] DX7 Engine Initialized. CreateSurface Hooked!" << std::endl;
        }
    }
    return hr;
}

// ============================================================================
// 1. INITIALIZATION
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Load the real DirectDraw library from System32
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, MAX_PATH, "\\ddraw.dll");

        HMODULE hRealDDraw = LoadLibraryA(sysPath);
        if (hRealDDraw) {
            oDirectDrawCreateEx = (DirectDrawCreateExFn)GetProcAddress(hRealDDraw, "DirectDrawCreateEx");

            // Optional: Spawn debug console to track the hooks
            AllocConsole();
            FILE* fDummy;
            freopen_s(&fDummy, "CONOUT$", "w", stdout);
            std::cout << "[+] ddraw.dll Proxy Loaded. Awaiting Engine Start..." << std::endl;
        }
    }
    return TRUE;
}