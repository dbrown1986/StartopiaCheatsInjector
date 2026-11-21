#include <windows.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <ddraw.h>
#include <Psapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include "MinHook.h"

// Dear ImGui Headers (External DX11 + Win32 Overlay)
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Psapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ==============================================================================
// version.dll Proxy Exports (Keeps the host game stable)
// ==============================================================================
#pragma comment(linker, "/EXPORT:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/EXPORT:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/EXPORT:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/EXPORT:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/EXPORT:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/EXPORT:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/EXPORT:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/EXPORT:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/EXPORT:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

// ==============================================================================
// Global Configuration & Runtime State
// ==============================================================================
struct ModConfig {
    bool toggleConsole = false;
    bool useImGUI = true;
    int  hotkeyVK = VK_F12;
    char hotkeyName[32] = "F12";
    bool outputCrashLog = true;
    bool showMenuOnStart = true;
    bool forceWindowed = true;
};

ModConfig g_config;

DWORD   g_baseAddress = 0;
FILE* g_logFile = nullptr;
HWND    g_gameHwnd = nullptr;
WNDPROC pOriginalGameWndProc = nullptr;
HWND    g_menuHwnd = nullptr;
HWND    g_tabHwnd = nullptr;
HFONT   g_uiFont = nullptr;
bool    g_menuVisible = true;
bool    g_alwaysOnTop = true;
bool    g_freeNewSegments = false;
static RECT g_savedMenuRect = { 35, 35, 580, 540 };

#define GAME_QUEUE_TIMER_ID 0x5A70
#define BORDERLESS_STYLE (WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE)

void ProcessCommandQueueOnMainThread();

void LogMsg(const char* fmt, ...)
{
    if (!g_config.toggleConsole && (!g_config.outputCrashLog || g_logFile == nullptr)) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);

    char msgBuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    char finalBuf[1200];
    snprintf(finalBuf, sizeof(finalBuf), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        msgBuf);

    if (g_config.toggleConsole) {
        printf("%s", finalBuf);
    }
    if (g_config.outputCrashLog && g_logFile) {
        fprintf(g_logFile, "%s", finalBuf);
        fflush(g_logFile);
    }
}

void SetMenuVisibility(bool visible)
{
    g_menuVisible = visible;
    if (!g_menuHwnd) return;

    if (!visible) {
        RECT rc{};
        if (GetWindowRect(g_menuHwnd, &rc) && rc.left > -10000) {
            g_savedMenuRect = rc;
        }
        SetWindowPos(g_menuHwnd, nullptr, -32000, -32000, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }
    else {
        SetWindowPos(g_menuHwnd, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            g_savedMenuRect.left, g_savedMenuRect.top, 0, 0,
            SWP_NOSIZE | SWP_SHOWWINDOW);
    }
}

// ==============================================================================
// Subclassed Game Main-Thread WndProc (Command Queue + Borderless Frame + Cursor)
// ==============================================================================
typedef LONG(WINAPI* tSetWindowLongA)(HWND hWnd, int nIndex, LONG dwNewLong);
tSetWindowLongA pOriginalSetWindowLongA = nullptr;

LONG WINAPI hkSetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
    if (g_config.forceWindowed && nIndex == GWL_STYLE && hWnd != nullptr && hWnd == g_gameHwnd) {
        dwNewLong = (LONG)BORDERLESS_STYLE;
    }
    return pOriginalSetWindowLongA(hWnd, nIndex, dwNewLong);
}

LRESULT CALLBACK SubclassGameWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TIMER && wParam == GAME_QUEUE_TIMER_ID) {
        ProcessCommandQueueOnMainThread();
    }
    else if (g_config.forceWindowed) {
        if (uMsg == WM_ERASEBKGND) {
            return 1;
        }
        if (uMsg == WM_STYLECHANGING && wParam == GWL_STYLE) {
            STYLESTRUCT* ss = (STYLESTRUCT*)lParam;
            if (ss) {
                ss->styleNew = (DWORD)BORDERLESS_STYLE;
            }
            return 0;
        }
        if (uMsg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        if (uMsg == WM_CLOSE) {
            PostQuitMessage(0);
            ExitProcess(0);
        }
    }
    return CallWindowProcA(pOriginalGameWndProc, hwnd, uMsg, wParam, lParam);
}

// ==============================================================================
// Self-Contained DirectDraw7 Windowed Mode + Flip->Blt Emulation (All Builds)
// ==============================================================================
BYTE* g_pRetailFlag = nullptr;
DWORD* g_pWindowedFlag = nullptr;
HWND   g_ddHwnd = nullptr;
DWORD  g_reqWidth = 1024;
DWORD  g_reqHeight = 768;

IDirectDraw7* g_pDD7 = nullptr;
IDirectDrawSurface7* g_pFakePrimary = nullptr;
IDirectDrawSurface7* g_pFakeBackBuffer = nullptr;

typedef LPSTR(WINAPI* tGetCommandLineA)(void);
tGetCommandLineA pOriginalGetCommandLineA = nullptr;
static char g_patchedCmdLine[2048]{};
static bool g_cmdLinePatched = false;

typedef HRESULT(WINAPI* tDirectDrawCreateEx)(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);
tDirectDrawCreateEx pOriginalDirectDrawCreateEx = nullptr;

typedef HRESULT(STDMETHODCALLTYPE* tDD7_CreateSurface)(IDirectDraw7* pDD, LPDDSURFACEDESC2 lpDesc, LPDIRECTDRAWSURFACE7* lplpSurf, IUnknown* pUnk);
typedef HRESULT(STDMETHODCALLTYPE* tDD7_GetDisplayMode)(IDirectDraw7* pDD, LPDDSURFACEDESC2 lpDesc);
typedef HRESULT(STDMETHODCALLTYPE* tDD7_SetCooperativeLevel)(IDirectDraw7* pDD, HWND hWnd, DWORD dwFlags);
typedef HRESULT(STDMETHODCALLTYPE* tDD7_SetDisplayMode)(IDirectDraw7* pDD, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags);

typedef HRESULT(STDMETHODCALLTYPE* tDDS7_Blt)(IDirectDrawSurface7* pSurf, LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_BltFast)(IDirectDrawSurface7* pSurf, DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_Flip)(IDirectDrawSurface7* pSurf, LPDIRECTDRAWSURFACE7 lpTargetOverride, DWORD dwFlags);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_GetAttachedSurface)(IDirectDrawSurface7* pSurf, LPDDSCAPS2 lpCaps, LPDIRECTDRAWSURFACE7* lplpAttached);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_GetDC)(IDirectDrawSurface7* pSurf, HDC* lphDC);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_Lock)(IDirectDrawSurface7* pSurf, LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_ReleaseDC)(IDirectDrawSurface7* pSurf, HDC hDC);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_Restore)(IDirectDrawSurface7* pSurf);
typedef HRESULT(STDMETHODCALLTYPE* tDDS7_Unlock)(IDirectDrawSurface7* pSurf, LPRECT lpRect);

tDD7_CreateSurface       pOriginalDD7CreateSurface = nullptr;
tDD7_GetDisplayMode      pOriginalDD7GetDisplayMode = nullptr;
tDD7_SetCooperativeLevel pOriginalDD7SetCooperativeLevel = nullptr;
tDD7_SetDisplayMode      pOriginalDD7SetDisplayMode = nullptr;

tDDS7_Blt                pOriginalDDS7Blt = nullptr;
tDDS7_BltFast            pOriginalDDS7BltFast = nullptr;
tDDS7_Flip               pOriginalDDS7Flip = nullptr;
tDDS7_GetAttachedSurface pOriginalDDS7GetAttachedSurface = nullptr;
tDDS7_GetDC              pOriginalDDS7GetDC = nullptr;
tDDS7_Lock               pOriginalDDS7Lock = nullptr;
tDDS7_ReleaseDC          pOriginalDDS7ReleaseDC = nullptr;
tDDS7_Restore            pOriginalDDS7Restore = nullptr;
tDDS7_Unlock             pOriginalDDS7Unlock = nullptr;

static bool g_dd7VtableHooked = false;
static bool g_dds7VtableHooked = false;

BYTE* FindPatternInModule(HMODULE hMod, const BYTE* pattern, const char* mask)
{
    MODULEINFO modInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(modInfo)))
        return nullptr;

    BYTE* base = (BYTE*)modInfo.lpBaseOfDll;
    size_t size = modInfo.SizeOfImage;
    size_t patLen = strlen(mask);

    for (size_t i = 0; i + patLen < size; i++) {
        bool match = true;
        for (size_t j = 0; j < patLen; j++) {
            if (mask[j] == 'x' && base[i + j] != pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) return base + i;
    }
    return nullptr;
}

bool HookModuleIAT(HMODULE hMod, const char* targetDll, const char* funcName, void* detour, void** originalOut)
{
    if (!hMod) return false;
    BYTE* base = (BYTE*)hMod;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return false;

    auto* impDesc = (IMAGE_IMPORT_DESCRIPTOR*)(base + importRva);
    for (; impDesc->Name; impDesc++) {
        const char* dllName = (const char*)(base + impDesc->Name);
        if (_stricmp(dllName, targetDll) != 0) continue;

        auto* origThunk = (IMAGE_THUNK_DATA*)(base + impDesc->OriginalFirstThunk);
        auto* iatThunk = (IMAGE_THUNK_DATA*)(base + impDesc->FirstThunk);

        for (; origThunk->u1.AddressOfData; origThunk++, iatThunk++) {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
            auto* impByName = (IMAGE_IMPORT_BY_NAME*)(base + origThunk->u1.AddressOfData);
            if (strcmp((const char*)impByName->Name, funcName) == 0) {
                DWORD oldProtect;
                VirtualProtect(&iatThunk->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &oldProtect);
                if (originalOut) *originalOut = (void*)iatThunk->u1.Function;
                iatThunk->u1.Function = (ULONG_PTR)detour;
                VirtualProtect(&iatThunk->u1.Function, sizeof(ULONG_PTR), oldProtect, &oldProtect);
                return true;
            }
        }
    }
    return false;
}

LPSTR WINAPI hkGetCommandLineA()
{
    LPSTR orig = pOriginalGetCommandLineA ? pOriginalGetCommandLineA() : GetCommandLineA();
    if (!orig || !g_config.forceWindowed) return orig;

    if (!g_cmdLinePatched) {
        strncpy_s(g_patchedCmdLine, orig, sizeof(g_patchedCmdLine) - 16);
        bool hasWindowArg = false;
        for (const char* p = g_patchedCmdLine; *p; ++p) {
            if ((*p == '/' || *p == '-') &&
                (p[1] == 'r' || p[1] == 'R' || p[1] == 'f' || p[1] == 'F')) {
                hasWindowArg = true;
                break;
            }
        }
        if (!hasWindowArg) {
            strcat_s(g_patchedCmdLine, " /r");
        }
        g_cmdLinePatched = true;
    }
    return g_patchedCmdLine;
}

RECT NormalizeDestRectForBackBuffer(const RECT& rawRect)
{
    RECT out = rawRect;
    HWND targetHwnd = g_ddHwnd ? g_ddHwnd : g_gameHwnd;
    if (targetHwnd && IsWindow(targetHwnd)) {
        POINT ptTopLeft = { 0, 0 };
        ClientToScreen(targetHwnd, &ptTopLeft);
        if ((ptTopLeft.x > 0 || ptTopLeft.y > 0) &&
            out.left >= ptTopLeft.x && out.top >= ptTopLeft.y &&
            (out.right > (LONG)g_reqWidth || out.bottom > (LONG)g_reqHeight)) {
            out.left -= ptTopLeft.x;
            out.top -= ptTopLeft.y;
            out.right -= ptTopLeft.x;
            out.bottom -= ptTopLeft.y;
        }
    }
    return out;
}

HRESULT STDMETHODCALLTYPE hkDDS7_Blt(
    IDirectDrawSurface7* pSurf, LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface,
    LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary &&
        g_pFakeBackBuffer != nullptr && lpDDSrcSurface != g_pFakeBackBuffer) {
        if (lpDestRect != nullptr) {
            RECT localDest = NormalizeDestRectForBackBuffer(*lpDestRect);
            return pOriginalDDS7Blt(g_pFakeBackBuffer, &localDest, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
        }
        return pOriginalDDS7Blt(g_pFakeBackBuffer, nullptr, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
    }
    return pOriginalDDS7Blt(pSurf, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
}

HRESULT STDMETHODCALLTYPE hkDDS7_BltFast(
    IDirectDrawSurface7* pSurf, DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface,
    LPRECT lpSrcRect, DWORD dwTrans)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        return pOriginalDDS7BltFast(g_pFakeBackBuffer, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
    }
    return pOriginalDDS7BltFast(pSurf, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
}

HRESULT STDMETHODCALLTYPE hkDDS7_Lock(
    IDirectDrawSurface7* pSurf, LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        if (lpDestRect != nullptr) {
            RECT localDest = NormalizeDestRectForBackBuffer(*lpDestRect);
            return pOriginalDDS7Lock(g_pFakeBackBuffer, &localDest, lpDDSurfaceDesc, dwFlags, hEvent);
        }
        return pOriginalDDS7Lock(g_pFakeBackBuffer, nullptr, lpDDSurfaceDesc, dwFlags, hEvent);
    }
    return pOriginalDDS7Lock(pSurf, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
}

HRESULT STDMETHODCALLTYPE hkDDS7_Unlock(IDirectDrawSurface7* pSurf, LPRECT lpRect)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        return pOriginalDDS7Unlock(g_pFakeBackBuffer, nullptr);
    }
    return pOriginalDDS7Unlock(pSurf, lpRect);
}

HRESULT STDMETHODCALLTYPE hkDDS7_GetDC(IDirectDrawSurface7* pSurf, HDC* lphDC)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        return pOriginalDDS7GetDC(g_pFakeBackBuffer, lphDC);
    }
    return pOriginalDDS7GetDC(pSurf, lphDC);
}

HRESULT STDMETHODCALLTYPE hkDDS7_ReleaseDC(IDirectDrawSurface7* pSurf, HDC hDC)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        return pOriginalDDS7ReleaseDC(g_pFakeBackBuffer, hDC);
    }
    return pOriginalDDS7ReleaseDC(pSurf, hDC);
}

HRESULT STDMETHODCALLTYPE hkDDS7_Flip(IDirectDrawSurface7* pSurf, LPDIRECTDRAWSURFACE7 lpTargetOverride, DWORD dwFlags)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        HWND targetHwnd = g_ddHwnd ? g_ddHwnd : (g_gameHwnd ? g_gameHwnd : FindWindowA("Startopia", nullptr));
        if (targetHwnd && IsWindow(targetHwnd)) {
            RECT rcClient{};
            GetClientRect(targetHwnd, &rcClient);
            POINT ptTopLeft = { rcClient.left, rcClient.top };
            POINT ptBotRight = { rcClient.right, rcClient.bottom };
            ClientToScreen(targetHwnd, &ptTopLeft);
            ClientToScreen(targetHwnd, &ptBotRight);
            RECT rcDest = { ptTopLeft.x, ptTopLeft.y, ptBotRight.x, ptBotRight.y };

            if (rcDest.right > rcDest.left && rcDest.bottom > rcDest.top) {
                if (g_pDD7 && (dwFlags & DDFLIP_NOVSYNC) == 0) {
                    g_pDD7->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, nullptr);
                }
                return pOriginalDDS7Blt(g_pFakePrimary, &rcDest, g_pFakeBackBuffer, nullptr, DDBLT_WAIT, nullptr);
            }
        }
        return DD_OK;
    }
    return pOriginalDDS7Flip(pSurf, lpTargetOverride, dwFlags);
}

HRESULT STDMETHODCALLTYPE hkDDS7_GetAttachedSurface(
    IDirectDrawSurface7* pSurf, LPDDSCAPS2 lpCaps, LPDIRECTDRAWSURFACE7* lplpAttached)
{
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr && lplpAttached != nullptr) {
        if (lpCaps && (lpCaps->dwCaps & DDSCAPS_BACKBUFFER)) {
            g_pFakeBackBuffer->AddRef();
            *lplpAttached = g_pFakeBackBuffer;
            return DD_OK;
        }
    }
    return pOriginalDDS7GetAttachedSurface(pSurf, lpCaps, lplpAttached);
}

HRESULT STDMETHODCALLTYPE hkDDS7_Restore(IDirectDrawSurface7* pSurf)
{
    HRESULT hr = pOriginalDDS7Restore(pSurf);
    if (g_config.forceWindowed && pSurf == g_pFakePrimary && g_pFakeBackBuffer != nullptr) {
        pOriginalDDS7Restore(g_pFakeBackBuffer);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hkDD7_CreateSurface(
    IDirectDraw7* pDD, LPDDSURFACEDESC2 lpDesc, LPDIRECTDRAWSURFACE7* lplpSurf, IUnknown* pUnk)
{
    if (g_config.forceWindowed && lpDesc && (lpDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)) {
        DDSURFACEDESC2 primDesc = *lpDesc;
        bool wantedBackBuffer = (primDesc.ddsCaps.dwCaps & (DDSCAPS_FLIP | DDSCAPS_COMPLEX)) != 0 ||
            ((primDesc.dwFlags & DDSD_BACKBUFFERCOUNT) && primDesc.dwBackBufferCount > 0);

        primDesc.dwFlags &= ~DDSD_BACKBUFFERCOUNT;
        primDesc.dwBackBufferCount = 0;
        primDesc.ddsCaps.dwCaps &= ~(DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE);

        HRESULT hr = pOriginalDD7CreateSurface(pDD, &primDesc, lplpSurf, pUnk);
        if (FAILED(hr) || !lplpSurf || !*lplpSurf) {
            LogMsg("[DX7-HOOK] Windowed Primary CreateSurface failed: 0x%08X\n", hr);
            return hr;
        }

        g_pFakePrimary = *lplpSurf;

        HWND targetHwnd = g_ddHwnd ? g_ddHwnd : (g_gameHwnd ? g_gameHwnd : FindWindowA("Startopia", nullptr));
        if (targetHwnd) {
            LPDIRECTDRAWCLIPPER pClipper = nullptr;
            if (SUCCEEDED(pDD->CreateClipper(0, &pClipper, nullptr)) && pClipper) {
                pClipper->SetHWnd(0, targetHwnd);
                g_pFakePrimary->SetClipper(pClipper);
                pClipper->Release();
            }
        }

        if (wantedBackBuffer) {
            if (g_pFakeBackBuffer) {
                g_pFakeBackBuffer->Release();
                g_pFakeBackBuffer = nullptr;
            }

            DDSURFACEDESC2 bbDesc{};
            bbDesc.dwSize = sizeof(DDSURFACEDESC2);
            bbDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            bbDesc.dwWidth = (g_reqWidth > 0) ? g_reqWidth : 800;
            bbDesc.dwHeight = (g_reqHeight > 0) ? g_reqHeight : 600;
            bbDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;

            HRESULT bbHr = pOriginalDD7CreateSurface(pDD, &bbDesc, &g_pFakeBackBuffer, nullptr);
            if (FAILED(bbHr)) {
                bbDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
                bbHr = pOriginalDD7CreateSurface(pDD, &bbDesc, &g_pFakeBackBuffer, nullptr);
            }
            LogMsg("[DX7-HOOK] Created Windowed Primary + Offscreen 3D BackBuffer (%ux%u, hr=0x%08X)\n",
                bbDesc.dwWidth, bbDesc.dwHeight, bbHr);
        }

        if (!g_dds7VtableHooked && g_pFakePrimary) {
            void** surfVtable = *(void***)g_pFakePrimary;
            if (surfVtable && surfVtable[5] && surfVtable[7] && surfVtable[11] && surfVtable[12] &&
                surfVtable[17] && surfVtable[25] && surfVtable[26] && surfVtable[27] && surfVtable[32]) {
                if (MH_CreateHook(surfVtable[5], (void*)&hkDDS7_Blt, (LPVOID*)&pOriginalDDS7Blt) == MH_OK &&
                    MH_CreateHook(surfVtable[7], (void*)&hkDDS7_BltFast, (LPVOID*)&pOriginalDDS7BltFast) == MH_OK &&
                    MH_CreateHook(surfVtable[11], (void*)&hkDDS7_Flip, (LPVOID*)&pOriginalDDS7Flip) == MH_OK &&
                    MH_CreateHook(surfVtable[12], (void*)&hkDDS7_GetAttachedSurface, (LPVOID*)&pOriginalDDS7GetAttachedSurface) == MH_OK &&
                    MH_CreateHook(surfVtable[17], (void*)&hkDDS7_GetDC, (LPVOID*)&pOriginalDDS7GetDC) == MH_OK &&
                    MH_CreateHook(surfVtable[25], (void*)&hkDDS7_Lock, (LPVOID*)&pOriginalDDS7Lock) == MH_OK &&
                    MH_CreateHook(surfVtable[26], (void*)&hkDDS7_ReleaseDC, (LPVOID*)&pOriginalDDS7ReleaseDC) == MH_OK &&
                    MH_CreateHook(surfVtable[27], (void*)&hkDDS7_Restore, (LPVOID*)&pOriginalDDS7Restore) == MH_OK &&
                    MH_CreateHook(surfVtable[32], (void*)&hkDDS7_Unlock, (LPVOID*)&pOriginalDDS7Unlock) == MH_OK) {
                    MH_EnableHook(surfVtable[5]);
                    MH_EnableHook(surfVtable[7]);
                    MH_EnableHook(surfVtable[11]);
                    MH_EnableHook(surfVtable[12]);
                    MH_EnableHook(surfVtable[17]);
                    MH_EnableHook(surfVtable[25]);
                    MH_EnableHook(surfVtable[26]);
                    MH_EnableHook(surfVtable[27]);
                    MH_EnableHook(surfVtable[32]);
                    g_dds7VtableHooked = true;
                }
            }
        }
        return DD_OK;
    }

    return pOriginalDD7CreateSurface(pDD, lpDesc, lplpSurf, pUnk);
}

HRESULT STDMETHODCALLTYPE hkDD7_GetDisplayMode(IDirectDraw7* pDD, LPDDSURFACEDESC2 lpDesc)
{
    HRESULT hr = pOriginalDD7GetDisplayMode(pDD, lpDesc);
    if (g_config.forceWindowed && SUCCEEDED(hr) && lpDesc) {
        lpDesc->dwFlags |= DDSD_PIXELFORMAT;
        lpDesc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        lpDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
        lpDesc->ddpfPixelFormat.dwRGBBitCount = 32;
        lpDesc->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
        lpDesc->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
        lpDesc->ddpfPixelFormat.dwBBitMask = 0x000000FF;
        lpDesc->ddpfPixelFormat.dwRGBAlphaBitMask = 0x00000000;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hkDD7_SetCooperativeLevel(IDirectDraw7* pDD, HWND hWnd, DWORD dwFlags)
{
    if (hWnd != nullptr && IsWindow(hWnd)) {
        g_ddHwnd = hWnd;
    }

    if (g_config.forceWindowed) {
        if (g_pRetailFlag)   *g_pRetailFlag = 0;
        if (g_pWindowedFlag) *g_pWindowedFlag = 1;

        dwFlags &= ~(DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWREBOOT);
        dwFlags |= DDSCL_NORMAL;

        if (hWnd != nullptr && IsWindow(hWnd)) {
            if (g_gameHwnd == nullptr) {
                g_gameHwnd = hWnd;
            }
            if (pOriginalGameWndProc == nullptr) {
                pOriginalGameWndProc = (WNDPROC)SetWindowLongA(g_gameHwnd, GWL_WNDPROC, (LONG)SubclassGameWndProc);
                SetTimer(g_gameHwnd, GAME_QUEUE_TIMER_ID, 100, nullptr);
            }

            RECT rc{};
            GetClientRect(hWnd, &rc);
            int clientW = (rc.right - rc.left > 0) ? (rc.right - rc.left) : 800;
            int clientH = (rc.bottom - rc.top > 0) ? (rc.bottom - rc.top) : 600;

            DWORD newStyle = BORDERLESS_STYLE;
            if (pOriginalSetWindowLongA) {
                pOriginalSetWindowLongA(hWnd, GWL_STYLE, (LONG)newStyle);
            }
            else {
                SetWindowLongA(hWnd, GWL_STYLE, (LONG)newStyle);
            }

            int posX = (GetSystemMetrics(SM_CXSCREEN) - clientW) / 2;
            int posY = (GetSystemMetrics(SM_CYSCREEN) - clientH) / 2;
            if (posX < 0) posX = 0;
            if (posY < 0) posY = 0;

            SetWindowPos(hWnd, HWND_NOTOPMOST, posX, posY, clientW, clientH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            ClipCursor(nullptr);
        }
        LogMsg("[DX7-HOOK] SetCooperativeLevel -> DDSCL_NORMAL (Borderless HWND=0x%p)\n", hWnd);
    }

    return pOriginalDD7SetCooperativeLevel(pDD, hWnd, dwFlags);
}

HRESULT STDMETHODCALLTYPE hkDD7_SetDisplayMode(
    IDirectDraw7* pDD, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags)
{
    if (dwWidth > 0)  g_reqWidth = dwWidth;
    if (dwHeight > 0) g_reqHeight = dwHeight;

    if (g_config.forceWindowed) {
        LogMsg("[DX7-HOOK] Intercepted SetDisplayMode(%ux%ux%u @ %uHz)\n",
            dwWidth, dwHeight, dwBPP, dwRefreshRate);

        HWND targetHwnd = g_ddHwnd ? g_ddHwnd : (g_gameHwnd ? g_gameHwnd : FindWindowA("Startopia", nullptr));
        if (targetHwnd && IsWindow(targetHwnd) && dwWidth > 0 && dwHeight > 0) {
            DWORD style = BORDERLESS_STYLE;
            if (pOriginalSetWindowLongA) {
                pOriginalSetWindowLongA(targetHwnd, GWL_STYLE, (LONG)style);
            }
            int posX = (GetSystemMetrics(SM_CXSCREEN) - (int)dwWidth) / 2;
            int posY = (GetSystemMetrics(SM_CYSCREEN) - (int)dwHeight) / 2;
            if (posX < 0) posX = 0;
            if (posY < 0) posY = 0;
            SetWindowPos(targetHwnd, nullptr, posX, posY, (int)dwWidth, (int)dwHeight, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return DD_OK;
    }

    return pOriginalDD7SetDisplayMode(pDD, dwWidth, dwHeight, dwBPP, dwRefreshRate, dwFlags);
}

HRESULT WINAPI hkDirectDrawCreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter)
{
    HRESULT hr = pOriginalDirectDrawCreateEx(lpGuid, lplpDD, iid, pUnkOuter);
    if (SUCCEEDED(hr) && lplpDD && *lplpDD) {
        g_pDD7 = (IDirectDraw7*)(*lplpDD);
        if (!g_dd7VtableHooked) {
            void** vtable = *(void***)(*lplpDD);
            if (vtable && vtable[6] && vtable[12] && vtable[20] && vtable[21]) {
                if (MH_CreateHook(vtable[6], (void*)&hkDD7_CreateSurface, (LPVOID*)&pOriginalDD7CreateSurface) == MH_OK &&
                    MH_CreateHook(vtable[12], (void*)&hkDD7_GetDisplayMode, (LPVOID*)&pOriginalDD7GetDisplayMode) == MH_OK &&
                    MH_CreateHook(vtable[20], (void*)&hkDD7_SetCooperativeLevel, (LPVOID*)&pOriginalDD7SetCooperativeLevel) == MH_OK &&
                    MH_CreateHook(vtable[21], (void*)&hkDD7_SetDisplayMode, (LPVOID*)&pOriginalDD7SetDisplayMode) == MH_OK) {
                    MH_EnableHook(vtable[6]);
                    MH_EnableHook(vtable[12]);
                    MH_EnableHook(vtable[20]);
                    MH_EnableHook(vtable[21]);
                    g_dd7VtableHooked = true;
                    LogMsg("[DX7-HOOK] Hooked IDirectDraw7 vtable (CreateSurface, GetDisplayMode, SetCooperativeLevel, SetDisplayMode)!\n");
                }
            }
        }
    }
    return hr;
}

void InstallWindowedModeEnforcer()
{
    HMODULE hExe = GetModuleHandleA(nullptr);
    if (!hExe) return;

    const BYTE sig[] = {
        0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0xFF,
        0x7F, 0x00,
        0xA0, 0x00, 0x00, 0x00, 0x00,
        0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x84, 0xC0,
        0x74
    };
    const char mask[] = "xx????x?x????xx????xxxxxxx";

    BYTE* match = FindPatternInModule(hExe, sig, mask);
    if (match) {
        g_pRetailFlag = *(BYTE**)(match + 10);
        g_pWindowedFlag = *(DWORD**)(match + 16);

        DWORD oldProtect;
        if (g_pRetailFlag) {
            VirtualProtect(g_pRetailFlag, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
            *g_pRetailFlag = 0;
            VirtualProtect(g_pRetailFlag, 1, oldProtect, &oldProtect);
        }
        if (g_pWindowedFlag) {
            VirtualProtect(g_pWindowedFlag, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect);
            *g_pWindowedFlag = 1;
            VirtualProtect(g_pWindowedFlag, sizeof(DWORD), oldProtect, &oldProtect);
        }
    }

    HookModuleIAT(hExe, "KERNEL32.dll", "GetCommandLineA", (void*)&hkGetCommandLineA, (void**)&pOriginalGetCommandLineA);
    HookModuleIAT(hExe, "USER32.dll", "SetWindowLongA", (void*)&hkSetWindowLongA, (void**)&pOriginalSetWindowLongA);
    HookModuleIAT(hExe, "DDRAW.dll", "DirectDrawCreateEx", (void*)&hkDirectDrawCreateEx, (void**)&pOriginalDirectDrawCreateEx);

    if (MH_Initialize() == MH_OK || MH_Initialize() == MH_ERROR_ALREADY_INITIALIZED) {
        if (pOriginalDirectDrawCreateEx == nullptr) {
            MH_CreateHookApi(L"ddraw.dll", "DirectDrawCreateEx", (void*)&hkDirectDrawCreateEx, (LPVOID*)&pOriginalDirectDrawCreateEx);
        }
        MH_EnableHook(MH_ALL_HOOKS);
    }
}

// ==============================================================================
// Multi-Version Offset Table (Steam 1.01, 1.01.1-b3 Non-Debug, 1.01.1-b3_debug)
// ==============================================================================
struct GameOffsets {
    const char* versionLabel;
    DWORD deductEnergy;
    DWORD scriptAddEnergy;
    DWORD discoverTech;
    DWORD spawnPeepSP;
    DWORD spawnPeepMP;
    DWORD spawnPortable;
    DWORD getPlayerSector;
    DWORD packCoords;        // 0 if packed inline via bitshift
    DWORD speciesTable;      // 0 if validated natively by SpawnPeep
    DWORD flagMpMode;
    DWORD playerPtr;
    DWORD buildSlongsList;
    DWORD buildFloatsList;
};

// Profile 0: Startopia Steam 1.01 (TimeDateStamp: 0x3B727110)
static const GameOffsets OFFSETS_STEAM_1_01 = {
    "Steam 1.01 (0x3B727110)",
    0x00000000, // deductEnergy (disabled)
    0x000A0F60, // FUN_004a0f60
    0x00104940, // FUN_00504940
    0x00035100, // FUN_00435100
    0x00035320, // FUN_00435320
    0x00002C90, // FUN_00402c90
    0x000D82F0, // FUN_004d82f0
    0x00000000, // Packed inline via bitshift
    0x00000000, // Validated natively inside SpawnPeep
    0x005B5588, // DAT_009b5588
    0x005B4CD4, // DAT_009b4cd4
    0x003F7D18, // DAT_007f7d18
    0x003F7D14  // DAT_007f7d14
};

// Profile 1: Startopia 1.01.1-b3 (Non-Debug)
static const GameOffsets OFFSETS_1_01_1_B3 = {
    "1.01.1-b3 (Non-Debug)",
    0x00000000, // deductEnergy (disabled)
    0x001AB390, // FUN_005ab390
    0x0029AEA0, // FUN_0069aea0
    0x00229B80, // FUN_00629b80
    0x00229CA0, // FUN_00629ca0
    0x0018B9B0, // FUN_0058b9b0
    0x00237F10, // FUN_00637f10
    0x0005EE30, // FUN_0045ee30
    0x00000000, // Validated natively inside FUN_0062a2e0
    0x007F0C4C, // DAT_00bf0c4c
    0x007E7C2C, // DAT_00be7c2c
    0x0067632C, // DAT_00a7632c
    0x00676330  // DAT_00a76330
};

// Profile 2: Startopia 1.01.1-b3_debug
static const GameOffsets OFFSETS_1_01_1_B3_DEBUG = {
    "1.01.1-b3_debug",
    0x001AEA80, // FUN_005aea80
    0x001AEEC0, // FUN_005aeec0
    0x002A0430, // FUN_006a0430
    0x0022E2B0, // FUN_0062e2b0
    0x0022E3D0, // FUN_0062e3d0
    0x0018F9D0, // FUN_0058f9d0
    0x0023C680, // FUN_0063c680
    0x00061280, // FUN_00461280
    0x004D9AF8, // DAT_008d9af8
    0x007FE1AC, // DAT_00bfe1ac
    0x007F5170, // DAT_00bf5170
    0x0068387C, // DAT_00a8387c
    0x00683880  // DAT_00a83880
};

GameOffsets g_offsets = OFFSETS_STEAM_1_01;

// ==============================================================================
// Runtime Version Detector (Selects Steam 1.01, 1.01.1-b3, or 1.01.1-b3_debug)
// ==============================================================================
bool CheckBytesAtRVA(DWORD rva, const BYTE* expected, size_t len)
{
    __try {
        const BYTE* ptr = (const BYTE*)(g_baseAddress + rva);
        return memcmp(ptr, expected, len) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void DetectGameVersion()
{
    DWORD timeStamp = 0;
    __try {
        auto* dos = (IMAGE_DOS_HEADER*)g_baseAddress;
        auto* nt = (IMAGE_NT_HEADERS*)(g_baseAddress + dos->e_lfanew);
        timeStamp = nt->FileHeader.TimeDateStamp;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        timeStamp = 0;
    }

    const BYTE sigSehPrologue[] = { 0x6A, 0xFF, 0x68 };
    bool isB3NonDebug = CheckBytesAtRVA(OFFSETS_1_01_1_B3.scriptAddEnergy, sigSehPrologue, sizeof(sigSehPrologue));
    bool isB3Debug = CheckBytesAtRVA(OFFSETS_1_01_1_B3_DEBUG.scriptAddEnergy, sigSehPrologue, sizeof(sigSehPrologue));

    if (timeStamp == 0x3B727110 || (!isB3NonDebug && !isB3Debug)) {
        g_offsets = OFFSETS_STEAM_1_01;
    }
    else if (isB3NonDebug && !isB3Debug) {
        g_offsets = OFFSETS_1_01_1_B3;
    }
    else if (isB3Debug && !isB3NonDebug) {
        g_offsets = OFFSETS_1_01_1_B3_DEBUG;
    }
    else {
        const BYTE sigStdPrologue[] = { 0x55, 0x8B, 0xEC };
        if (CheckBytesAtRVA(OFFSETS_1_01_1_B3.packCoords, sigStdPrologue, sizeof(sigStdPrologue))) {
            g_offsets = OFFSETS_1_01_1_B3;
        }
        else {
            g_offsets = OFFSETS_1_01_1_B3_DEBUG;
        }
    }

    LogMsg("[VERSION] Detected Startopia build: %s (TimeStamp=0x%08X, AddEnergy=0x%08X, PlayerPtr=0x%08X)\n",
        g_offsets.versionLabel, timeStamp, g_offsets.scriptAddEnergy, g_offsets.playerPtr);
}

// ==============================================================================
// INI Configuration Settings (Auto-generated if missing)
// ==============================================================================
int ParseHotkeyString(const char* rawStr)
{
    if (!rawStr || !rawStr[0]) return VK_F12;
    std::string s;
    for (const char* p = rawStr; *p; ++p) {
        if (!isspace((unsigned char)*p)) {
            s.push_back((char)toupper((unsigned char)*p));
        }
    }
    if (s.empty()) return VK_F12;

    if (s == "F1")  return VK_F1;
    if (s == "F2")  return VK_F2;
    if (s == "F3")  return VK_F3;
    if (s == "F4")  return VK_F4;
    if (s == "F5")  return VK_F5;
    if (s == "F6")  return VK_F6;
    if (s == "F7")  return VK_F7;
    if (s == "F8")  return VK_F8;
    if (s == "F9")  return VK_F9;
    if (s == "F10") return VK_F10;
    if (s == "F11") return VK_F11;
    if (s == "F12") return VK_F12;
    if (s == "INSERT" || s == "INS")    return VK_INSERT;
    if (s == "DELETE" || s == "DEL")    return VK_DELETE;
    if (s == "HOME")                    return VK_HOME;
    if (s == "END")                     return VK_END;
    if (s == "PAGEUP" || s == "PGUP")   return VK_PRIOR;
    if (s == "PAGEDOWN" || s == "PGDN") return VK_NEXT;
    if (s == "TILDE" || s == "~" || s == "`") return VK_OEM_3;
    if (s == "TAB")                     return VK_TAB;
    if (s == "PAUSE")                   return VK_PAUSE;

    if (s.size() == 1 && isalnum((unsigned char)s[0])) {
        return (int)s[0];
    }
    if (s.rfind("0X", 0) == 0) {
        int val = (int)strtol(s.c_str(), nullptr, 16);
        if (val > 0 && val < 256) return val;
    }
    return VK_F12;
}

bool ParseIniBool(const char* val, bool defaultVal)
{
    if (!val || !val[0]) return defaultVal;
    if (_stricmp(val, "true") == 0 || strcmp(val, "1") == 0 || _stricmp(val, "yes") == 0) return true;
    if (_stricmp(val, "false") == 0 || strcmp(val, "0") == 0 || _stricmp(val, "no") == 0) return false;
    return defaultVal;
}

void LoadOrCreateIniConfig()
{
    char iniPath[MAX_PATH]{};
    GetFullPathNameA("StartopiaCheeseburger.ini", MAX_PATH, iniPath, nullptr);

    DWORD attrs = GetFileAttributesA(iniPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (fopen_s(&f, iniPath, "w") == 0 && f != nullptr) {
            fprintf(f,
                "; Startopia Cheeseburger Menu Configuration\n"
                "[General]\n"
                "ToggleConsole=false\n"
                "UseImGUI=true\n"
                "Hotkey=F12\n"
                "OutputCrashLog=true\n"
                "ShowMenuOnStart=true\n"
                "ForceWindowed=true\n");
            fclose(f);
        }
    }

    char buf[64]{};
    GetPrivateProfileStringA("General", "ToggleConsole", "false", buf, sizeof(buf), iniPath);
    g_config.toggleConsole = ParseIniBool(buf, false);

    GetPrivateProfileStringA("General", "UseImGUI", "true", buf, sizeof(buf), iniPath);
    g_config.useImGUI = ParseIniBool(buf, true);

    GetPrivateProfileStringA("General", "Hotkey", "F12", buf, sizeof(buf), iniPath);
    strncpy_s(g_config.hotkeyName, buf, sizeof(g_config.hotkeyName) - 1);
    g_config.hotkeyVK = ParseHotkeyString(buf);

    GetPrivateProfileStringA("General", "OutputCrashLog", "true", buf, sizeof(buf), iniPath);
    g_config.outputCrashLog = ParseIniBool(buf, true);

    GetPrivateProfileStringA("General", "ShowMenuOnStart", "true", buf, sizeof(buf), iniPath);
    g_config.showMenuOnStart = ParseIniBool(buf, true);

    GetPrivateProfileStringA("General", "ForceWindowed", "true", buf, sizeof(buf), iniPath);
    g_config.forceWindowed = ParseIniBool(buf, true);

    g_menuVisible = g_config.showMenuOnStart;
}

// ==============================================================================
// Verified Crate, Tech Theory, Room Hard-Plan, Furniture Plan & Peep Catalogs
// ==============================================================================
struct SubTypeEntry {
    unsigned int id;
    const char* label;
};

static const SubTypeEntry g_techTheoryCatalog[] = {
    { 42 , "Alien Artifact (@alien_artifact)" },
    { 76 , "Analysis Bin (@analysis_bin)" },
    { 94 , "Art: Endeavour (@art_endeavour)" },
    { 95 , "Art: Furies (@art_furies)" },
    { 88 , "Art: Gnmerich (@art_gnmerich)" },
    { 89 , "Art: Kategukhat (@art_kategukhat)" },
    { 90 , "Art: Legacy (@art_legacy)" },
    { 91 , "Art: Matter (@art_matter)" },
    { 92 , "Art: Mother (@art_mother)" },
    { 93 , "Art: Planetscape (@art_planetscape)" },
    { 96 , "Art: U-Planter (@art_uplanter)" },
    { 150, "Beer Bar (@beer_bar)" },
    { 108, "Beer Bar Counter (@beer_bar_bar)" },
    { 86 , "Beer Bar Table (@beer_bar_table)" },
    { 121, "Berths (@berths)" },
    { 40 , "Black Market Goods (@black_market_goods)" },
    { 153, "Brig (@brig)" },
    { 78 , "Bunk Bed (@bunk)" },
    { 126, "Cargo Bay (@cargo_bay)" },
    { 151, "Casino (@casino)" },
    { 98 , "Cell (@cell)" },
    { 128, "Charger (@charger)" },
    { 114, "Clock (@clock)" },
    { 149, "Cocktail Bar (@cocktail_bar)" },
    { 109, "Cocktail Bar Counter (@cocktail_bar_bar)" },
    { 97 , "Cocktail Bar Table (@cocktail_bar_table)" },
    { 119, "Collector (@collector)" },
    { 120, "Collector Node (@collector_node)" },
    { 131, "Def-Con (@def_con)" },
    { 132, "Disco (@disco)" },
    { 81 , "Dispenser (@dispenser)" },
    { 117, "Docking Port (@docking_port)" },
    { 124, "Dry Dock (@dry_dock)" },
    { 133, "Fighting Pit (@fighting_pit)" },
    { 35 , "Food Supply (@food_supply)" },
    { 157, "Fountain (@fountain)" },
    { 152, "Gallery (@gallery)" },
    { 154, "Gunner Turret (@gunner2)" },
    { 36 , "Hardware (@hardware)" },
    { 79 , "Health Charts (@health_charts)" },
    { 134, "Holosuite / Suites (@suites)" },
    { 33 , "Industrial (@industrial)" },
    { 73 , "Lab Bench (@lab_bench)" },
    { 123, "Laboratory (@laboratory)" },
    { 113, "Lava Lamp (@lavalamp)" },
    { 140, "Lavotron (@lavotron)" },
    { 87 , "Litter Bin (@litter_bin)" },
    { 106, "Love Bench (@love_bench)" },
    { 105, "Love Chair (@love_chair)" },
    { 155, "Love Nest (@love_nest)" },
    { 38 , "Luxury Item (@luxury_item)" },
    { 83 , "Machine That Pings (@machine_that_pings)" },
    { 82 , "Medi-Bed (@medi_bed)" },
    { 34 , "Medical Supply (@medical_supply)" },
    { 74 , "Microscope (@microscope)" },
    { 39 , "Ore (@ore)" },
    { 135, "Orophux (@orophux)" },
    { 115, "Plant Pot (@plantpot)" },
    { 99 , "Plaza Bench (@plaza_bench)" },
    { 103, "Plaza Fountain (@plaza_fountain)" },
    { 100, "Plaza Lamp (@plaza_lamp)" },
    { 101, "Plaza Monk (@plaza_monk)" },
    { 104, "Plaza Monument (@plaza_monument)" },
    { 102, "Plaza Tower (@plaza_tower)" },
    { 14 , "Police Scutter (@police_scutter)" },
    { 71 , "Projector (@projector)" },
    { 125, "Recycler (@recycler)" },
    { 75 , "Replicator (@replicator)" },
    { 148, "Rough Bar (@rough_bar)" },
    { 107, "Rough Bar Counter (@rough_bar_bar)" },
    { 85 , "Rough Bar Table (@rough_bar_table)" },
    { 130, "Satcom (@satcom)" },
    { 11 , "Scutter Mk I (@scutter_level1)" },
    { 12 , "Scutter Mk II (@scutter_level2)" },
    { 13 , "Scutter Mk III (@scutter_level3)" },
    { 146, "Shop: Army (@shop_army)" },
    { 144, "Shop: Curio (@shop_curio)" },
    { 147, "Shop: General (@shop_general)" },
    { 142, "Shop: Music (@shop_music)" },
    { 143, "Shop: Pooter (@shop_pooter)" },
    { 145, "Shop: Sport (@shop_sport)" },
    { 122, "Sick Bay (@sick_bay)" },
    { 110, "Slug Bar Counter (@slug_bar_bar)" },
    { 111, "Slug Bar Table (@slug_bar_table)" },
    { 141, "Slugpartments (@slugpartments)" },
    { 137, "Star Hotel 1 (@hotel_one)" },
    { 138, "Star Hotel 2 (@hotel_two)" },
    { 139, "Star Hotel 3 (@hotel_three)" },
    { 112, "TeeVee (@teevee)" },
    { 156, "Temple (@temple)" },
    { 129, "Vendor / Dine-O-Mat (@vendor)" },
    { 80 , "Waiting Chair (@waiting_chair)" },
    { 127, "Workshop / Factory (@workshop)" },
    { 72 , "Workstation (@workstation)" },
    { 84 , "X-Ray Machine (@x_ray_machine)" },
};
static const int TECH_THEORY_COUNT = sizeof(g_techTheoryCatalog) / sizeof(g_techTheoryCatalog[0]);

static const SubTypeEntry g_roomPlanCatalog[] = {
    { 5,  "Berth" },
    { 41, "Bio-Deck Fountain" },
    { 10, "Cargo Bay" },
    { 35, "Casino" },
    { 33, "Cocktail Bar" },
    { 4,  "Collector Node" },
    { 30, "Combat Store" },
    { 27, "Computer Shop" },
    { 14, "Comsensor" },
    { 28, "Curiosity Shop" },
    { 13, "Dine-O-Mat" },
    { 16, "Disco" },
    { 1,  "Docking Port" },
    { 3,  "Energy Collector" },
    { 11, "Factory" },
    { 17, "Fighting Pit [!]" },
    { 31, "General Store" },
    { 18, "Holodeck" },
    { 7,  "Laboratory" },
    { 24, "Lavotron" },
    { 29, "Leisure Store" },
    { 37, "Lockdown Brig" },
    { 39, "Love Nest" },
    { 26, "Music Shop" },
    { 19, "Orophux" },
    { 23, "Palace Galactica" },
    { 12, "Recharger" },
    { 34, "Recreational Bar" },
    { 9,  "Recycler" },
    { 32, "Rough Bar" },
    { 38, "Security Column" },
    { 15, "Security Control" },
    { 6,  "Sick Bay" },
    { 25, "Slugpartments" },
    { 22, "Space Inn" },
    { 21, "Star Motel" },
    { 8,  "Stardock" },
    { 36, "Viewing Gallery" },
    { 40, "Zedem Temple (Biodeck Only)" },
    { 20, "Zero-G Gyro [!]" },
};
static const int ROOM_PLAN_COUNT = sizeof(g_roomPlanCatalog) / sizeof(g_roomPlanCatalog[0]);

static const SubTypeEntry g_furniturePlanCatalog[] = {
    { 24, "Art: Endeavour (FURN_ART_ENDEAVOUR)" },
    { 25, "Art: Furies (FURN_ART_FURIES)" },
    { 18, "Art: Gnmerich (FURN_ART_GNMERICH)" },
    { 19, "Art: Kategukhat (FURN_ART_KATEGUKHAT)" },
    { 20, "Art: Legacy (FURN_ART_LEGACY)" },
    { 21, "Art: Matter (FURN_ART_MATTER)" },
    { 22, "Art: Mother (FURN_ART_MOTHER)" },
    { 23, "Art: Planetscape (FURN_ART_PLANETSCAPE)" },
    { 26, "Art: U-Planter (FURN_ART_UPLANTER)" },
    { 38, "Beer Bar Counter (FURN_BEERBAR)" },
    { 16, "Beer Bar Table (FURN_BEERBAR_TABLE)" },
    { 28, "Brig Cell (FURN_CELL)" },
    { 8,  "Bunk Bed (FURN_BUNK)" },
    { 44, "Clock (FURN_CLOCK)" },
    { 39, "Cocktail Bar Counter (FURN_COCKTAILBAR)" },
    { 27, "Cocktail Bar Table (FURN_COCKTAILBAR_TABLE)" },
    { 11, "Dispenser (FURN_DISPENSER)" },
    { 9,  "Health Charts (FURN_HEALTH_CHARTS)" },
    { 3,  "Lab Bench (FURN_LabBench)" },
    { 43, "Lava Lamp (FURN_LAVALAMP)" },
    { 17, "Litter Bin (FURN_LITTER_BIN)" },
    { 36, "Love Bench (FURN_LOVE_BENCH)" },
    { 35, "Love Chair (FURN_LOVE_CHAIR)" },
    { 13, "Machine That Pings (FURN_MACHINE_THAT_PINGS)" },
    { 12, "Medi-Bed (FURN_MEDI_BED)" },
    { 4,  "Microscope (FURN_Microscope)" },
    { 45, "Plant Pot (FURN_PLANTPOT)" },
    { 29, "Plaza Bench (FURN_PLAZA_BENCH)" },
    { 33, "Plaza Fountain (FURN_PLAZA_FOUNTAIN)" },
    { 30, "Plaza Lamp (FURN_PLAZA_LAMP)" },
    { 31, "Plaza Monk Statue (FURN_PLAZA_MONK)" },
    { 34, "Plaza Monument (FURN_PLAZA_MONUMENT)" },
    { 32, "Plaza Tower (FURN_PLAZA_TOWER)" },
    { 1,  "Projector (FURN_Projector)" },
    { 5,  "Replicator (FURN_Replicator)" },
    { 37, "Rough Bar Counter (FURN_ROUGHBAR)" },
    { 15, "Rough Bar Table (FURN_ROUGHBAR_TABLE)" },
    { 40, "Slug Bar Counter (FURN_SLUGBAR)" },
    { 41, "Slug Bar Table (FURN_SLUGBAR_TABLE)" },
    { 42, "TeeVee (FURN_TEEVEE)" },
    { 10, "Waiting Chair (FURN_WAITING_CHAIR)" },
    { 2,  "Workstation (FURN_Workstation)" },
    { 14, "X-Ray Machine (FURN_XRAY_MACHINE)" },
};
static const int FURN_PLAN_COUNT = sizeof(g_furniturePlanCatalog) / sizeof(g_furniturePlanCatalog[0]);

struct CrateCatalogEntry {
    unsigned int id;
    const char* label;
    int subTypeMode;
};

static const CrateCatalogEntry g_crateCatalog[] = {
    { 10, "Alien Artifact",         0 },
    { 8,  "Black Market Supplies",  0 },
    { 9,  "BOMB!",                  0 },
    { 17, "Damaged Goods",          0 },
    { 14, "Droid Crate",            2 },
    { 3,  "Food Crate",             0 },
    { 16, "Furniture Crate",        4 },
    { 15, "Hardplan Crate",         3 },
    { 4,  "Hardware Crate",         0 },
    { 1,  "Industrial Crate",       0 },
    { 11, "Junk",                   0 },
    { 12, "Litter",                 0 },
    { 6,  "Luxury Goods",           0 },
    { 2,  "Medical Supplies",       0 },
    { 7,  "Ore Crate",              0 },
    { 5,  "Technology Crate",       1 },
    { 13, "Turdite",                0 },
};
static const int CRATE_CATALOG_COUNT = sizeof(g_crateCatalog) / sizeof(g_crateCatalog[0]);

struct PeepCatalogEntry {
    unsigned int id;
    const char* label;
    bool canBeResident;
};

static const PeepCatalogEntry g_peepCatalog[] = {
    { 12, "Dahanese Siren (Female)",          true  },
    { 13, "Dahanese Siren (Male)",            true  },
    { 14, "Gem Slug",                         false },
    { 6,  "Gray",                             true  },
    { 8,  "Grekka Targ",                      true  },
    { 9,  "Karmarama",                        true  },
    { 7,  "Kasvagorian",                      true  },
    { 20, "Memau",                            false },
    { 15, "Penitent",                         false },
    { 5,  "Salt Hog",                         true  },
    { 1,  "Scutter Mk I (Maintenance Droid)", false },
    { 2,  "Scutter Mk II (Wheel)",            false },
    { 3,  "Scutter Mk III (Hover)",           false },
    { 4,  "Security Scutter",                 false },
    { 21, "Skrasher",                         false },
    { 19, "Space Vermin",                     false },
    { 22, "Spy",                              false },
    { 11, "Turakken",                         true  },
    { 10, "Zedem Monk",                       true  },
};
static const int PEEP_CATALOG_COUNT = sizeof(g_peepCatalog) / sizeof(g_peepCatalog[0]);

#pragma pack(push, 1)
struct PeepAttributesFull {
    unsigned int peepType;
    unsigned short flags;
    unsigned char moodData[54];
    unsigned int defaultFlag;
    unsigned char skill;
    unsigned char loyalty;
    unsigned char dedication;
    unsigned char infected;
    unsigned char reserved[4];
};
#pragma pack(pop)

// ==============================================================================
// Thread-Safe Main-Thread Command Queue
// ==============================================================================
enum CommandType {
    CMD_ADD_ENERGY,
    CMD_UNLOCK_ALL_TECH,
    CMD_SPAWN_PEEP,
    CMD_SPAWN_CRATE
};

struct ModCommand {
    CommandType type;
    int amount;
    unsigned int id;
    unsigned int extraData;
    int localX;
    int localZ;
    int deck;
    bool resident;
    unsigned char skill;
    unsigned char dedication;
    unsigned char loyalty;
};

std::queue<ModCommand> g_cmdQueue;
std::mutex g_cmdMutex;

void EnqueueCommand(const ModCommand& cmd)
{
    LogMsg("[CMD-QUEUE] Enqueued command type=%d (id=%u, amount=%d)\n", (int)cmd.type, cmd.id, cmd.amount);
    std::lock_guard<std::mutex> lock(g_cmdMutex);
    g_cmdQueue.push(cmd);
}

LONG WINAPI GlobalVectoredCrashHandler(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == 0x40010006 || code == EXCEPTION_BREAKPOINT) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    DWORD eip = ep->ContextRecord->Eip;
    DWORD ghidraAddr = (g_baseAddress != 0 && eip >= g_baseAddress) ? (eip - g_baseAddress + 0x00400000) : 0;

    LogMsg("\n[FATAL CRASH] =====================================\n");
    LogMsg("Exception Code : 0x%08X | Live EIP: 0x%08X | Ghidra: 0x%08X\n", code, eip, ghidraAddr);
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        LogMsg("Access Type    : %s memory at 0x%08X\n",
            ep->ExceptionRecord->ExceptionInformation[0] ? "WRITING" : "READING",
            (DWORD)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    LogMsg("===================================================\n");
    return EXCEPTION_CONTINUE_SEARCH;
}

void ApplyMenuAlwaysOnTop(bool alwaysOnTop)
{
    g_alwaysOnTop = alwaysOnTop;
    if (g_menuHwnd != nullptr && g_menuVisible) {
        SetWindowPos(
            g_menuHwnd,
            g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }
}

// ==============================================================================
// Engine Function Typedefs & Execution Helpers
// ==============================================================================
typedef unsigned int(__fastcall* tScriptAddEnergy)(void* pPlayer, int amount);
typedef void(__fastcall* tDiscoverTech)(void* pPlayer, unsigned int techId);
typedef int(__fastcall* tGetPlayerSector)(void* pPlayer, int unusedDeck);
typedef unsigned int* (__thiscall* tPackCoords)(unsigned int* outPacked, char x, unsigned int z, unsigned int deck);
typedef unsigned int(__fastcall* tSpawnPeepSP)(int mode, unsigned int* pPackedCoords, unsigned int peepType, unsigned short angle, void* pPlayer, void* pAttr);
typedef void(__fastcall* tSpawnPeepMP)(int mode, unsigned int* pPackedCoords, int peepType, unsigned int angle, void* pPlayer, int* pAttr);
typedef unsigned int(__fastcall* tSpawnPortable)(unsigned int* pPackedCoords, unsigned int cargoType, unsigned int angle, void* pPlayer, unsigned int extraData, int flag);

unsigned int GetSafePlayerTile(void* pPlayer, int localOffsetX, int localZ, int deck)
{
    auto GetSector = (tGetPlayerSector)(g_baseAddress + g_offsets.getPlayerSector);

    unsigned int minX = *(unsigned int*)((BYTE*)pPlayer + 0x1785 + deck * 4);
    unsigned int maxX = *(unsigned int*)((BYTE*)pPlayer + 0x1791 + deck * 4);
    int sector = GetSector(pPlayer, deck);

    unsigned char finalX = (unsigned char)((minX + localOffsetX) & 0xFF);
    if (minX == 0 && maxX == 0) {
        finalX = (unsigned char)((sector * 0x10 + localOffsetX) & 0xFF);
    }

    unsigned int packedCoord = 0;
    if (g_offsets.packCoords != 0) {
        auto PackCoords = (tPackCoords)(g_baseAddress + g_offsets.packCoords);
        PackCoords(&packedCoord, (char)finalX, (unsigned int)localZ, (unsigned int)deck);
    }
    else {
        // Inline bitfield format used by Steam 1.01:
        // ((deck & 3) << 5 | (z & 0x1F)) << 8 | (finalX & 0xFF)
        packedCoord = ((((unsigned int)deck & 3U) << 5 | ((unsigned int)localZ & 0x1FU)) << 8) |
            ((unsigned int)finalX & 0xFFU);
    }
    return packedCoord;
}

void ExecuteAddEnergy(void* pPlayer, int amount)
{
    auto ScriptAddEnergy = (tScriptAddEnergy)(g_baseAddress + g_offsets.scriptAddEnergy);
    LogMsg("[EXEC-ENERGY] Modifying player energy by %+d (pPlayer=0x%p)\n", amount, pPlayer);
    ScriptAddEnergy(pPlayer, amount);
}

typedef void(__thiscall* tDeductEnergy)(void* pThis, int amount, void* pPlayer);
tDeductEnergy pOriginalDeductEnergy = nullptr;

void __fastcall HookedDeductEnergy(void* pThis, void* /*edx*/, int amount, void* pPlayer)
{
    pOriginalDeductEnergy(pThis, amount, pPlayer);

    if (g_freeNewSegments && amount == 10000 && pPlayer != nullptr) {
        void* pLocalPlayer = *(void**)(g_baseAddress + g_offsets.playerPtr);
        if (pPlayer == pLocalPlayer) {
            LogMsg("[REFUND-SEG] Refunding %d energy for segment unlock (pPlayer=0x%p)\n", amount, pPlayer);
            ExecuteAddEnergy(pPlayer, amount);
        }
    }
}

void ExecuteUnlockAllTech(void* pPlayer)
{
    auto DiscoverTech = (tDiscoverTech)(g_baseAddress + g_offsets.discoverTech);
    LogMsg("[EXEC-TECH] Discovering @alltech (0xFFFFFFFF) for pPlayer=0x%p\n", pPlayer);
    DiscoverTech(pPlayer, 0xFFFFFFFF);
}

void ExecuteSpawnPeep(const ModCommand& cmd, void* pPlayer)
{
    if (cmd.id < 1 || cmd.id > 23) return;

    if (g_offsets.speciesTable != 0) {
        void** speciesTable = (void**)(g_baseAddress + g_offsets.speciesTable);
        if (speciesTable[cmd.id] == nullptr) {
            LogMsg("[PEEP-SKIP] Peep ID %u is NOT loaded in species table for this map.\n", cmd.id);
            return;
        }
    }

    int mpFlag = *(int*)(g_baseAddress + g_offsets.flagMpMode);
    unsigned int packedCoord = GetSafePlayerTile(pPlayer, cmd.localX, cmd.localZ, cmd.deck);

    PeepAttributesFull attr{};
    attr.peepType = cmd.id;
    attr.flags = cmd.resident ? 1 : 0;
    attr.defaultFlag = 1;
    attr.skill = cmd.skill;
    attr.loyalty = cmd.loyalty;
    attr.dedication = cmd.dedication;
    attr.infected = 0;

    LogMsg("[EXEC-PEEP] ID=%u | Pos=(%d,%d) Deck=%d | Res=%d | S/D/L=%u/%u/%u | Packed=0x%08X\n",
        cmd.id, cmd.localX, cmd.localZ, cmd.deck, cmd.resident ? 1 : 0,
        cmd.skill, cmd.dedication, cmd.loyalty, packedCoord);

    if (mpFlag == 0) {
        auto SpawnSP = (tSpawnPeepSP)(g_baseAddress + g_offsets.spawnPeepSP);
        SpawnSP(0, &packedCoord, cmd.id, 0, pPlayer, &attr);
    }
    else {
        auto SpawnMP = (tSpawnPeepMP)(g_baseAddress + g_offsets.spawnPeepMP);
        SpawnMP(0, &packedCoord, (int)cmd.id, 0, pPlayer, (int*)&attr);
    }
}

void ExecuteSpawnCrate(void* pPlayer, unsigned int cargoTypeId, unsigned int extraData, int localX, int localZ, int deck)
{
    if (cargoTypeId < 1 || cargoTypeId > 17) return;

    auto SpawnPortable = (tSpawnPortable)(g_baseAddress + g_offsets.spawnPortable);
    unsigned int packedCoord = GetSafePlayerTile(pPlayer, localX, localZ, deck);
    int flag = (cargoTypeId == 0xB || cargoTypeId == 0xC) ? 0 : 1;

    LogMsg("[EXEC-CRATE] CargoID=%u | Extra=%u | Tile=(%d,%d) Deck=%d | Packed=0x%08X\n",
        cargoTypeId, extraData, localX, localZ, deck, packedCoord);

    unsigned int res = SpawnPortable(&packedCoord, cargoTypeId, 0, pPlayer, extraData, flag);
    LogMsg("[EXEC-CRATE] SpawnPortable returned: %u\n", res);
}

// ==============================================================================
// Live BUILD SLONGS & BUILD FLOATS Discovery & Editor
// ==============================================================================
struct BuildVarEntry {
    std::string name;
    int category;
    bool isFloat;
    void* pValueAddr;
    int defaultInt;
    float defaultFloat;
    int minInt;
    int maxInt;
    float minFloat;
    float maxFloat;
};

static std::vector<BuildVarEntry> g_buildVars;
static int g_lastScannedBuildCount = -1;

static const char* g_buildCategoryNames[] = {
    "All Categories",
    "Segments & Station",
    "Factory & Cargo",
    "Rooms & Furniture",
    "Droids & Peeps",
    "Combat & AI",
    "Economy & Costs",
    "Environment & Misc"
};
static const int BUILD_CATEGORY_COUNT = 8;

int ClassifyBuildVarCategory(const char* name)
{
    if (strstr(name, "SEG") || strstr(name, "SEGMENT") || strstr(name, "STATION_YUMNESS") ||
        strstr(name, "ENERGY_HEALTHY") || strstr(name, "ENERGY_YELLOW")) {
        return 1;
    }
    if (strstr(name, "CARGO_") || strstr(name, "VENDOR_") || strstr(name, "SHOP_")) {
        return 2;
    }
    if (strstr(name, "ROOM_") || strstr(name, "FURNITURE_") || strstr(name, "COLLECTOR_")) {
        return 3;
    }
    if (strstr(name, "SCUTTER_") || strstr(name, "SCUZZER_") || strstr(name, "PEEP_") ||
        strstr(name, "PENITENT_") || strstr(name, "GEMSLUG_") || strstr(name, "MEMAU_") ||
        strstr(name, "DISEASE_") || strstr(name, "TRADER_")) {
        return 4;
    }
    if (strstr(name, "AI_") || strstr(name, "LASER") || strstr(name, "BLAST_") ||
        strstr(name, "CRIMINALS_") || strstr(name, "DISABLE_WAR") || strstr(name, "GUNNER2_") ||
        strstr(name, "SPAWN_ROOM")) {
        return 5;
    }
    if (strstr(name, "COST") || strstr(name, "RRP") || strstr(name, "PROFIT") || strstr(name, "HANDOUT")) {
        return 6;
    }
    return 7;
}

void ComputeSmartRangeInt(const char* name, int defVal, int& outMin, int& outMax)
{
    if (strstr(name, "UNLOCK_ALL_SEGS") || strstr(name, "DISABLE_WAR") || strstr(name, "AI_FIGHT_")) {
        outMin = 0; outMax = 1; return;
    }
    if (strstr(name, "PERCENT") || strstr(name, "PROBABILITY") || strstr(name, "CHANCE")) {
        outMin = (defVal < 0) ? defVal : 0;
        outMax = (defVal > 100) ? defVal * 2 : 100;
        return;
    }
    if (strstr(name, "LEVEL") && defVal <= 255) {
        outMin = 0; outMax = 255; return;
    }
    outMin = (defVal < 0) ? (defVal * 2) : 0;
    int absDef = (defVal < 0) ? -defVal : defVal;
    if (absDef == 0) outMax = 1000;
    else if (absDef <= 20) outMax = 100;
    else if (absDef <= 500) outMax = absDef * 5;
    else outMax = absDef * 4;
}

void ComputeSmartRangeFloat(const char* name, float defVal, float& outMin, float& outMax)
{
    if (strstr(name, "BUILD_SUN_")) {
        outMin = -2.0f; outMax = 2.0f; return;
    }
    if (strstr(name, "CHANCE")) {
        outMin = 0.0f; outMax = 100.0f; return;
    }
    outMin = (defVal < 0.0f) ? (defVal * 2.0f) : 0.0f;
    float absDef = (defVal < 0.0f) ? -defVal : defVal;
    if (absDef < 0.0001f) outMax = 10.0f;
    else if (absDef < 0.05f) outMax = absDef * 20.0f;
    else if (absDef <= 5.0f) outMax = absDef * 10.0f;
    else outMax = absDef * 4.0f;
}

struct RawBuildNode {
    const char* name;
    void* pValueAddr;
    bool isFloat;
};

int ReadBuildVarsRaw(RawBuildNode* outNodes, int maxNodes)
{
    int count = 0;
    __try {
        DWORD node = *(DWORD*)(g_baseAddress + g_offsets.buildSlongsList);
        int guard = 0;
        while (node != 0 && node > 0x10000 && guard++ < 2048 && count < maxNodes) {
            const char* varName = *(const char**)(node + 0x0C);
            int* pVal = (int*)(node + 0x14);
            if (varName != nullptr && (uintptr_t)varName > 0x10000 && varName[0] != '\0') {
                outNodes[count++] = { varName, pVal, false };
            }
            node = *(DWORD*)(node + 0x08);
        }

        node = *(DWORD*)(g_baseAddress + g_offsets.buildFloatsList);
        guard = 0;
        while (node != 0 && node > 0x10000 && guard++ < 2048 && count < maxNodes) {
            const char* varName = *(const char**)(node + 0x0C);
            float* pVal = (float*)(node + 0x14);
            if (varName != nullptr && (uintptr_t)varName > 0x10000 && varName[0] != '\0') {
                outNodes[count++] = { varName, pVal, true };
            }
            node = *(DWORD*)(node + 0x08);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return count;
}

void EnsureBuildVarsLoaded(bool forceRescan = false)
{
    if (g_baseAddress == 0) return;

    RawBuildNode rawNodes[2048]{};
    int count = ReadBuildVarsRaw(rawNodes, 2048);
    if (count < 0) {
        LogMsg("[BUILD-VARS] Exception while scanning build changer lists.\n");
        return;
    }
    if (count == 0) return;

    if (!forceRescan && count == g_lastScannedBuildCount) {
        return;
    }

    g_buildVars.clear();
    g_buildVars.reserve(count);

    for (int i = 0; i < count; i++) {
        BuildVarEntry entry{};
        entry.name = rawNodes[i].name;
        entry.category = ClassifyBuildVarCategory(rawNodes[i].name);
        entry.isFloat = rawNodes[i].isFloat;
        entry.pValueAddr = rawNodes[i].pValueAddr;

        if (!entry.isFloat) {
            entry.defaultInt = *(int*)entry.pValueAddr;
            ComputeSmartRangeInt(rawNodes[i].name, entry.defaultInt, entry.minInt, entry.maxInt);
        }
        else {
            entry.defaultFloat = *(float*)entry.pValueAddr;
            ComputeSmartRangeFloat(rawNodes[i].name, entry.defaultFloat, entry.minFloat, entry.maxFloat);
        }
        g_buildVars.push_back(entry);
    }

    g_lastScannedBuildCount = count;
    LogMsg("[BUILD-VARS] Indexed %zu live BUILD SLONG/FLOAT entries.\n", g_buildVars.size());
}

bool MatchesBuildFilter(const BuildVarEntry& entry, int catFilter, const char* searchFilter)
{
    if (catFilter > 0 && entry.category != catFilter) return false;
    if (!searchFilter || searchFilter[0] == '\0') return true;

    std::string hay = entry.name;
    std::string needle = searchFilter;
    for (char& c : hay) c = (char)toupper((unsigned char)c);
    for (char& c : needle) c = (char)toupper((unsigned char)c);
    return hay.find(needle) != std::string::npos;
}

// ==============================================================================
// Command Queue Execution on Game Main Thread
// ==============================================================================
void ProcessCommandQueueOnMainThread()
{
    void* pPlayer = *(void**)(g_baseAddress + g_offsets.playerPtr);
    if (pPlayer == nullptr) {
        std::lock_guard<std::mutex> lock(g_cmdMutex);
        if (!g_cmdQueue.empty()) {
            LogMsg("[WARN] Load into a mission or sandbox map before executing commands!\n");
            while (!g_cmdQueue.empty()) g_cmdQueue.pop();
        }
        return;
    }

    ModCommand cmd{};
    bool hasCmd = false;
    {
        std::lock_guard<std::mutex> lock(g_cmdMutex);
        if (!g_cmdQueue.empty()) {
            cmd = g_cmdQueue.front();
            g_cmdQueue.pop();
            hasCmd = true;
        }
    }

    if (hasCmd) {
        switch (cmd.type) {
        case CMD_ADD_ENERGY:
            ExecuteAddEnergy(pPlayer, cmd.amount);
            break;
        case CMD_UNLOCK_ALL_TECH:
            ExecuteUnlockAllTech(pPlayer);
            break;
        case CMD_SPAWN_PEEP:
            ExecuteSpawnPeep(cmd, pPlayer);
            break;
        case CMD_SPAWN_CRATE:
            ExecuteSpawnCrate(pPlayer, cmd.id, cmd.extraData, cmd.localX, cmd.localZ, cmd.deck);
            break;
        }
    }
}

HWND FindStartopiaMainWindow()
{
    DWORD currentPid = GetCurrentProcessId();
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExA(nullptr, hwnd, nullptr, nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == currentPid && hwnd != g_menuHwnd && IsWindowVisible(hwnd)) {
            char cls[128]{};
            GetClassNameA(hwnd, cls, sizeof(cls));
            if (strcmp(cls, "ConsoleWindowClass") != 0 &&
                strcmp(cls, "StartopiaStandaloneCheatMenu") != 0 &&
                strcmp(cls, "StartopiaImGuiCheeseburgerMenu") != 0) {
                return hwnd;
            }
        }
    }
    return nullptr;
}

int ClampInt(int val, int minVal, int maxVal)
{
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

// ==============================================================================
// Classic Win32 GDI Menu Controls (Used when UseImGUI=false)
// ==============================================================================
enum MenuControlIDs {
    IDC_CHK_ALWAYS_ON_TOP = 99,
    IDC_MAIN_TAB = 100,

    IDC_BTN_NRG_ADD_50K = 1001,
    IDC_BTN_NRG_ADD_500K,
    IDC_BTN_NRG_SUB_50K,
    IDC_EDIT_NRG_CUSTOM,
    IDC_BTN_NRG_APPLY_CUSTOM,
    IDC_LBL_NRG_1,
    IDC_LBL_NRG_2,
    IDC_CHK_NRG_FREE_SEGMENTS,
    IDC_BTN_NRG_UNLOCK_ALL_TECH,
    IDC_LBL_NRG_TECH_NOTE,

    IDC_LBL_PEEP_TYPE = 2001,
    IDC_COMBO_PEEP_TYPE,
    IDC_LBL_PEEP_DECK,
    IDC_COMBO_PEEP_DECK,
    IDC_LBL_PEEP_X,
    IDC_EDIT_PEEP_X,
    IDC_LBL_PEEP_Y,
    IDC_EDIT_PEEP_Y,
    IDC_LBL_PEEP_QTY,
    IDC_EDIT_PEEP_QTY,
    IDC_CHK_PEEP_RESIDENT,
    IDC_LBL_PEEP_SKILL,
    IDC_EDIT_PEEP_SKILL,
    IDC_LBL_PEEP_DEDICATION,
    IDC_EDIT_PEEP_DEDICATION,
    IDC_LBL_PEEP_LOYALTY,
    IDC_EDIT_PEEP_LOYALTY,
    IDC_BTN_PEEP_SPAWN,

    IDC_LBL_CRATE_TYPE = 3001,
    IDC_COMBO_CRATE_TYPE,
    IDC_LBL_CRATE_SUBTYPE,
    IDC_COMBO_CRATE_SUBTYPE,
    IDC_LBL_CRATE_DECK,
    IDC_COMBO_CRATE_DECK,
    IDC_LBL_CRATE_X,
    IDC_EDIT_CRATE_X,
    IDC_LBL_CRATE_Y,
    IDC_EDIT_CRATE_Y,
    IDC_LBL_CRATE_QTY,
    IDC_EDIT_CRATE_QTY,
    IDC_BTN_CRATE_SPAWN,
    IDC_BTN_CRATE_SPAWN_ALL_STD,

    IDC_COMBO_BUILD_CAT = 4001,
    IDC_EDIT_BUILD_SEARCH,
    IDC_COMBO_BUILD_VAR,
    IDC_SLIDER_BUILD_VAL,
    IDC_EDIT_BUILD_VAL,
    IDC_BTN_BUILD_APPLY,
    IDC_BTN_BUILD_RESET,
    IDC_LBL_BUILD_INFO,
    IDC_LBL_BUILD_WARNING,

    IDC_LBL_HELP_TEXT = 5001
};

void SetChildFont(HWND hParent)
{
    EnumChildWindows(hParent, [](HWND hChild, LPARAM lParam) -> BOOL {
        SendMessageA(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
        }, (LPARAM)g_uiFont);
}

static std::vector<size_t> g_classicFilteredBuildIndices;

void SyncClassicBuildVarValueUI(HWND hwnd)
{
    int sel = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_BUILD_VAR), CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_classicFilteredBuildIndices.size()) {
        SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_BUILD_VAL), "");
        SetWindowTextA(GetDlgItem(hwnd, IDC_LBL_BUILD_INFO), "Load into a map to inspect Build Changers.");
        return;
    }

    const auto& v = g_buildVars[g_classicFilteredBuildIndices[sel]];
    HWND hSlider = GetDlgItem(hwnd, IDC_SLIDER_BUILD_VAL);
    char valBuf[64]{}, infoBuf[160]{};

    if (!v.isFloat) {
        int cur = *(int*)v.pValueAddr;
        sprintf_s(valBuf, "%d", cur);
        sprintf_s(infoBuf, "Type: SLONG (int) | Default: %d | Range: %d .. %d", v.defaultInt, v.minInt, v.maxInt);
        SendMessageA(hSlider, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageA(hSlider, TBM_SETRANGEMAX, TRUE, 1000);
        int span = (v.maxInt > v.minInt) ? (v.maxInt - v.minInt) : 1;
        int pos = ClampInt((int)(((long long)(cur - v.minInt) * 1000LL) / span), 0, 1000);
        SendMessageA(hSlider, TBM_SETPOS, TRUE, pos);
    }
    else {
        float cur = *(float*)v.pValueAddr;
        sprintf_s(valBuf, "%.4f", cur);
        sprintf_s(infoBuf, "Type: FLOAT | Default: %.4f | Range: %.3f .. %.3f", v.defaultFloat, v.minFloat, v.maxFloat);
        SendMessageA(hSlider, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageA(hSlider, TBM_SETRANGEMAX, TRUE, 1000);
        float span = (v.maxFloat > v.minFloat) ? (v.maxFloat - v.minFloat) : 1.0f;
        int pos = ClampInt((int)(((cur - v.minFloat) / span) * 1000.0f), 0, 1000);
        SendMessageA(hSlider, TBM_SETPOS, TRUE, pos);
    }

    SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_BUILD_VAL), valBuf);
    SetWindowTextA(GetDlgItem(hwnd, IDC_LBL_BUILD_INFO), infoBuf);
}

void RebuildClassicBuildVarList(HWND hwnd, bool forceRescan = false)
{
    EnsureBuildVarsLoaded(forceRescan);
    int catIdx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_BUILD_CAT), CB_GETCURSEL, 0, 0);
    if (catIdx < 0) catIdx = 0;

    char searchBuf[64]{};
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_BUILD_SEARCH), searchBuf, sizeof(searchBuf));

    HWND hComboVar = GetDlgItem(hwnd, IDC_COMBO_BUILD_VAR);
    SendMessageA(hComboVar, CB_RESETCONTENT, 0, 0);
    g_classicFilteredBuildIndices.clear();

    for (size_t i = 0; i < g_buildVars.size(); i++) {
        if (MatchesBuildFilter(g_buildVars[i], catIdx, searchBuf)) {
            g_classicFilteredBuildIndices.push_back(i);
            SendMessageA(hComboVar, CB_ADDSTRING, 0, (LPARAM)g_buildVars[i].name.c_str());
        }
    }

    if (!g_classicFilteredBuildIndices.empty()) {
        SendMessageA(hComboVar, CB_SETCURSEL, 0, 0);
    }
    SyncClassicBuildVarValueUI(hwnd);
}

void SwitchMenuTab(int tabIndex)
{
    auto ShowRange = [&](int firstId, int lastId, bool show) {
        for (int id = firstId; id <= lastId; id++) {
            HWND hCtrl = GetDlgItem(g_menuHwnd, id);
            if (hCtrl) {
                ShowWindow(hCtrl, show ? SW_SHOW : SW_HIDE);
                if (show) {
                    SetWindowPos(hCtrl, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                }
            }
        }
        };

    ShowRange(1001, 1020, tabIndex == 0);
    ShowRange(2001, 2040, tabIndex == 1);
    ShowRange(3001, 3040, tabIndex == 2);
    ShowRange(4001, 4020, tabIndex == 3);
    ShowRange(5001, 5020, tabIndex == 4);

    if (tabIndex == 3 && g_menuHwnd != nullptr) {
        RebuildClassicBuildVarList(g_menuHwnd, true);
    }
}

void SyncPeepControlsForSelection()
{
    int idx = (int)SendMessageA(GetDlgItem(g_menuHwnd, IDC_COMBO_PEEP_TYPE), CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= PEEP_CATALOG_COUNT) return;

    const auto& entry = g_peepCatalog[idx];
    HWND hChkResident = GetDlgItem(g_menuHwnd, IDC_CHK_PEEP_RESIDENT);

    if (entry.canBeResident) {
        EnableWindow(hChkResident, TRUE);
    }
    else {
        SendMessageA(hChkResident, BM_SETCHECK, BST_UNCHECKED, 0);
        EnableWindow(hChkResident, FALSE);
    }
}

void SyncCrateControlsForSelection()
{
    int idx = (int)SendMessageA(GetDlgItem(g_menuHwnd, IDC_COMBO_CRATE_TYPE), CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= CRATE_CATALOG_COUNT) return;

    const auto& entry = g_crateCatalog[idx];
    HWND hComboSub = GetDlgItem(g_menuHwnd, IDC_COMBO_CRATE_SUBTYPE);
    SendMessageA(hComboSub, CB_RESETCONTENT, 0, 0);

    if (entry.subTypeMode == 0) {
        SendMessageA(hComboSub, CB_ADDSTRING, 0, (LPARAM)"N/A - Standard Crate (Extra = 0)");
        SendMessageA(hComboSub, CB_SETITEMDATA, 0, 0);
        SendMessageA(hComboSub, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboSub, FALSE);
    }
    else if (entry.subTypeMode == 1) {
        for (int i = 0; i < TECH_THEORY_COUNT; i++) {
            int itemIdx = (int)SendMessageA(hComboSub, CB_ADDSTRING, 0, (LPARAM)g_techTheoryCatalog[i].label);
            SendMessageA(hComboSub, CB_SETITEMDATA, itemIdx, (LPARAM)g_techTheoryCatalog[i].id);
        }
        SendMessageA(hComboSub, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboSub, TRUE);
    }
    else if (entry.subTypeMode == 2) {
        for (int i = 0; i < PEEP_CATALOG_COUNT; i++) {
            int itemIdx = (int)SendMessageA(hComboSub, CB_ADDSTRING, 0, (LPARAM)g_peepCatalog[i].label);
            SendMessageA(hComboSub, CB_SETITEMDATA, itemIdx, (LPARAM)g_peepCatalog[i].id);
        }
        SendMessageA(hComboSub, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboSub, TRUE);
    }
    else if (entry.subTypeMode == 3) {
        for (int i = 0; i < ROOM_PLAN_COUNT; i++) {
            int itemIdx = (int)SendMessageA(hComboSub, CB_ADDSTRING, 0, (LPARAM)g_roomPlanCatalog[i].label);
            SendMessageA(hComboSub, CB_SETITEMDATA, itemIdx, (LPARAM)g_roomPlanCatalog[i].id);
        }
        SendMessageA(hComboSub, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboSub, TRUE);
    }
    else if (entry.subTypeMode == 4) {
        for (int i = 0; i < FURN_PLAN_COUNT; i++) {
            int itemIdx = (int)SendMessageA(hComboSub, CB_ADDSTRING, 0, (LPARAM)g_furniturePlanCatalog[i].label);
            SendMessageA(hComboSub, CB_SETITEMDATA, itemIdx, (LPARAM)g_furniturePlanCatalog[i].id);
        }
        SendMessageA(hComboSub, CB_SETCURSEL, 0, 0);
        EnableWindow(hComboSub, TRUE);
    }
}

LRESULT CALLBACK ClassicMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_TAB_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&icex);

        g_uiFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        HWND hChkTop = CreateWindowA("BUTTON", "Always on top",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | BS_AUTOCHECKBOX,
            12, 6, 150, 22, hwnd, (HMENU)IDC_CHK_ALWAYS_ON_TOP, nullptr, nullptr);
        SendMessageA(hChkTop, BM_SETCHECK, g_alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);

        g_tabHwnd = CreateWindowExA(0, WC_TABCONTROLA, "",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            8, 32, 455, 422, hwnd, (HMENU)IDC_MAIN_TAB, nullptr, nullptr);

        const char* tabNames[] = {
            "Energy/Tech",
            "Peeps",
            "Crates",
            "Build Changers",
            "Help/About"
        };
        for (int i = 0; i < 5; i++) {
            TCITEMA tie{};
            tie.mask = TCIF_TEXT;
            tie.pszText = (LPSTR)tabNames[i];
            TabCtrl_InsertItem(g_tabHwnd, i, &tie);
        }

        // --- TAB 0: Energy/Tech ---
        CreateWindowA("STATIC", "Direct Wallet Energy Controls (No Script Needed):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 72, 420, 20, hwnd, (HMENU)IDC_LBL_NRG_1, nullptr, nullptr);
        CreateWindowA("BUTTON", "+50,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 100, 132, 32, hwnd, (HMENU)IDC_BTN_NRG_ADD_50K, nullptr, nullptr);
        CreateWindowA("BUTTON", "+500,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            164, 100, 140, 32, hwnd, (HMENU)IDC_BTN_NRG_ADD_500K, nullptr, nullptr);
        CreateWindowA("BUTTON", "-50,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            314, 100, 132, 32, hwnd, (HMENU)IDC_BTN_NRG_SUB_50K, nullptr, nullptr);

        CreateWindowA("STATIC", "Custom Energy Amount (Positive or Negative):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 149, 420, 20, hwnd, (HMENU)IDC_LBL_NRG_2, nullptr, nullptr);
        CreateWindowA("EDIT", "100000", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL,
            22, 174, 215, 26, hwnd, (HMENU)IDC_EDIT_NRG_CUSTOM, nullptr, nullptr);
        CreateWindowA("BUTTON", "Apply Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            247, 172, 199, 30, hwnd, (HMENU)IDC_BTN_NRG_APPLY_CUSTOM, nullptr, nullptr);

        CreateWindowA("BUTTON", "Unlock all tech", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 246, 424, 34, hwnd, (HMENU)IDC_BTN_NRG_UNLOCK_ALL_TECH, nullptr, nullptr);
        CreateWindowA("STATIC",
            "Discovers @alltech (all theories & blueprints). Note: Avoid using during\n"
            "the Turakken research mission unless you want an instant win.\n"
            "Warning: Modifying tech states may cause unwanted engine behavior.",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 288, 424, 52, hwnd, (HMENU)IDC_LBL_NRG_TECH_NOTE, nullptr, nullptr);

        // --- TAB 1: Peeps ---
        CreateWindowA("STATIC", "Peep / Droid Species:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 70, 425, 18, hwnd, (HMENU)IDC_LBL_PEEP_TYPE, nullptr, nullptr);
        HWND hComboPeep = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 90, 425, 280, hwnd, (HMENU)IDC_COMBO_PEEP_TYPE, nullptr, nullptr);
        for (int i = 0; i < PEEP_CATALOG_COUNT; i++) {
            SendMessageA(hComboPeep, CB_ADDSTRING, 0, (LPARAM)g_peepCatalog[i].label);
        }
        SendMessageA(hComboPeep, CB_SETCURSEL, 9, 0);

        CreateWindowA("STATIC", "Target Deck:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 122, 425, 18, hwnd, (HMENU)IDC_LBL_PEEP_DECK, nullptr, nullptr);
        HWND hComboPeepDeck = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 142, 425, 120, hwnd, (HMENU)IDC_COMBO_PEEP_DECK, nullptr, nullptr);
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 0 - Sub Deck (Engineering / Cargo)");
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 1 - Leisure Deck");
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 2 - Bio Deck");
        SendMessageA(hComboPeepDeck, CB_SETCURSEL, 0, 0);

        CreateWindowA("STATIC", "Tile X:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 176, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_X, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            72, 173, 55, 24, hwnd, (HMENU)IDC_EDIT_PEEP_X, nullptr, nullptr);

        CreateWindowA("STATIC", "Tile Y:", WS_CHILD | WS_CLIPSIBLINGS,
            142, 176, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_Y, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            192, 173, 55, 24, hwnd, (HMENU)IDC_EDIT_PEEP_Y, nullptr, nullptr);

        CreateWindowA("STATIC", "Qty (1-50):", WS_CHILD | WS_CLIPSIBLINGS,
            262, 176, 72, 20, hwnd, (HMENU)IDC_LBL_PEEP_QTY, nullptr, nullptr);
        CreateWindowA("EDIT", "1", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            338, 173, 57, 24, hwnd, (HMENU)IDC_EDIT_PEEP_QTY, nullptr, nullptr);

        HWND hChkRes = CreateWindowA("BUTTON", "Resident (Hired Staff)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 208, 210, 22, hwnd, (HMENU)IDC_CHK_PEEP_RESIDENT, nullptr, nullptr);
        SendMessageA(hChkRes, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowA("STATIC", "Skill (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 238, 72, 20, hwnd, (HMENU)IDC_LBL_PEEP_SKILL, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            96, 235, 42, 24, hwnd, (HMENU)IDC_EDIT_PEEP_SKILL, nullptr, nullptr);

        CreateWindowA("STATIC", "Dedication (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            150, 238, 108, 20, hwnd, (HMENU)IDC_LBL_PEEP_DEDICATION, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            260, 235, 42, 24, hwnd, (HMENU)IDC_EDIT_PEEP_DEDICATION, nullptr, nullptr);

        CreateWindowA("STATIC", "Loyalty (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            314, 238, 82, 20, hwnd, (HMENU)IDC_LBL_PEEP_LOYALTY, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            396, 235, 51, 24, hwnd, (HMENU)IDC_EDIT_PEEP_LOYALTY, nullptr, nullptr);

        CreateWindowA("BUTTON", "Spawn Peep(s) Now (Paced Grid)", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 274, 425, 34, hwnd, (HMENU)IDC_BTN_PEEP_SPAWN, nullptr, nullptr);

        // --- TAB 2: Crates ---
        CreateWindowA("STATIC", "Crate / Portable Type (Alphabetical):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 70, 425, 18, hwnd, (HMENU)IDC_LBL_CRATE_TYPE, nullptr, nullptr);
        HWND hComboCrate = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 90, 425, 280, hwnd, (HMENU)IDC_COMBO_CRATE_TYPE, nullptr, nullptr);
        for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
            SendMessageA(hComboCrate, CB_ADDSTRING, 0, (LPARAM)g_crateCatalog[i].label);
        }
        SendMessageA(hComboCrate, CB_SETCURSEL, 5, 0);

        CreateWindowA("STATIC", "Crate Sub-Type (Hardplan, Furniture, Droid, Tech):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 122, 425, 18, hwnd, (HMENU)IDC_LBL_CRATE_SUBTYPE, nullptr, nullptr);
        CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 142, 425, 280, hwnd, (HMENU)IDC_COMBO_CRATE_SUBTYPE, nullptr, nullptr);

        CreateWindowA("STATIC", "Target Deck:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 174, 425, 18, hwnd, (HMENU)IDC_LBL_CRATE_DECK, nullptr, nullptr);
        HWND hComboCrateDeck = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 194, 425, 120, hwnd, (HMENU)IDC_COMBO_CRATE_DECK, nullptr, nullptr);
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 0 - Sub Deck (Engineering / Cargo)");
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 1 - Leisure Deck");
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 2 - Bio Deck");
        SendMessageA(hComboCrateDeck, CB_SETCURSEL, 0, 0);

        CreateWindowA("STATIC", "Tile X:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 230, 48, 20, hwnd, (HMENU)IDC_LBL_CRATE_X, nullptr, nullptr);
        CreateWindowA("EDIT", "8", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            72, 227, 55, 24, hwnd, (HMENU)IDC_EDIT_CRATE_X, nullptr, nullptr);

        CreateWindowA("STATIC", "Tile Y:", WS_CHILD | WS_CLIPSIBLINGS,
            142, 230, 48, 20, hwnd, (HMENU)IDC_LBL_CRATE_Y, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            192, 227, 55, 24, hwnd, (HMENU)IDC_EDIT_CRATE_Y, nullptr, nullptr);

        CreateWindowA("STATIC", "Qty (1-50):", WS_CHILD | WS_CLIPSIBLINGS,
            262, 230, 72, 20, hwnd, (HMENU)IDC_LBL_CRATE_QTY, nullptr, nullptr);
        CreateWindowA("EDIT", "1", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            338, 227, 57, 24, hwnd, (HMENU)IDC_EDIT_CRATE_QTY, nullptr, nullptr);

        CreateWindowA("BUTTON", "Spawn Crate(s) Now", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 266, 205, 34, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN, nullptr, nullptr);
        CreateWindowA("BUTTON", "Spawn All 17 Types", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            237, 266, 210, 34, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN_ALL_STD, nullptr, nullptr);

        // --- TAB 3: Build Changers ---
        HWND hComboBuildCat = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 70, 200, 220, hwnd, (HMENU)IDC_COMBO_BUILD_CAT, nullptr, nullptr);
        for (int i = 0; i < BUILD_CATEGORY_COUNT; i++) {
            SendMessageA(hComboBuildCat, CB_ADDSTRING, 0, (LPARAM)g_buildCategoryNames[i]);
        }
        SendMessageA(hComboBuildCat, CB_SETCURSEL, 0, 0);

        CreateWindowA("EDIT", "", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL,
            230, 70, 217, 24, hwnd, (HMENU)IDC_EDIT_BUILD_SEARCH, nullptr, nullptr);

        CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 102, 425, 280, hwnd, (HMENU)IDC_COMBO_BUILD_VAR, nullptr, nullptr);

        CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_CLIPSIBLINGS | TBS_HORZ | TBS_NOTICKS,
            22, 140, 280, 28, hwnd, (HMENU)IDC_SLIDER_BUILD_VAL, nullptr, nullptr);

        CreateWindowA("EDIT", "", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL,
            310, 140, 137, 26, hwnd, (HMENU)IDC_EDIT_BUILD_VAL, nullptr, nullptr);

        CreateWindowA("BUTTON", "Apply Value", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 178, 205, 30, hwnd, (HMENU)IDC_BTN_BUILD_APPLY, nullptr, nullptr);

        CreateWindowA("BUTTON", "Reset to Default", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            237, 178, 210, 30, hwnd, (HMENU)IDC_BTN_BUILD_RESET, nullptr, nullptr);

        CreateWindowA("STATIC", "Select a category or type in the search box to filter variables.",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 220, 425, 45, hwnd, (HMENU)IDC_LBL_BUILD_INFO, nullptr, nullptr);

        CreateWindowA("STATIC",
            "Warning: Adjusting build changer settings may cause unwanted engine behavior.",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 272, 425, 36, hwnd, (HMENU)IDC_LBL_BUILD_WARNING, nullptr, nullptr);

        // --- TAB 4: Help/About ---
        char helpBuf[768];
        sprintf_s(helpBuf,
            "Startopia Cheeseburger Menu v1.0 (Classic Win32 UI)\n"
            "Build Date: 11/21/2026 | Game: %s\n"
            "Powered by Dear ImGui & MinHook\n"
            "Dedicated to Chris Nelson.\n\n"
            "• [%s] : Show / Hide this Menu Overlay\n"
            "• Edit StartopiaCheeseburger.ini to toggle ImGui, Console, or Hotkey\n"
            "• Deck 0 = Sub Deck | Deck 1 = Leisure | Deck 2 = Bio\n"
            "• Batch Spawning: Spawns 1 to 50 entities on a clean paced grid\n"
            "  starting at your chosen Tile X / Tile Y coordinates.\n",
            g_offsets.versionLabel,
            g_config.hotkeyName);
        CreateWindowA("STATIC", helpBuf, WS_CHILD | WS_CLIPSIBLINGS,
            22, 72, 425, 310, hwnd, (HMENU)IDC_LBL_HELP_TEXT, nullptr, nullptr);

        SetChildFont(hwnd);
        SyncPeepControlsForSelection();
        SyncCrateControlsForSelection();
        SwitchMenuTab(0);
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_MAIN_TAB && nm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(g_tabHwnd);
            SwitchMenuTab(sel);
        }
        break;
    }

    case WM_HSCROLL: {
        if ((HWND)lParam == GetDlgItem(hwnd, IDC_SLIDER_BUILD_VAL)) {
            int sel = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_BUILD_VAR), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_classicFilteredBuildIndices.size()) {
                auto& v = g_buildVars[g_classicFilteredBuildIndices[sel]];
                int pos = (int)SendMessageA((HWND)lParam, TBM_GETPOS, 0, 0);
                char valBuf[64]{};
                if (!v.isFloat) {
                    int newVal = v.minInt + (int)(((long long)(v.maxInt - v.minInt) * pos) / 1000LL);
                    *(int*)v.pValueAddr = newVal;
                    sprintf_s(valBuf, "%d", newVal);
                }
                else {
                    float newVal = v.minFloat + ((v.maxFloat - v.minFloat) * (pos / 1000.0f));
                    *(float*)v.pValueAddr = newVal;
                    sprintf_s(valBuf, "%.4f", newVal);
                }
                SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_BUILD_VAL), valBuf);
            }
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_CHK_ALWAYS_ON_TOP && code == BN_CLICKED) {
            bool checked = (SendMessageA(GetDlgItem(hwnd, IDC_CHK_ALWAYS_ON_TOP), BM_GETCHECK, 0, 0) == BST_CHECKED);
            ApplyMenuAlwaysOnTop(checked);
        }
        else if (id == IDC_BTN_NRG_ADD_50K && code == BN_CLICKED) {
            EnqueueCommand({ CMD_ADD_ENERGY, 50000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_BTN_NRG_ADD_500K && code == BN_CLICKED) {
            EnqueueCommand({ CMD_ADD_ENERGY, 500000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_BTN_NRG_SUB_50K && code == BN_CLICKED) {
            EnqueueCommand({ CMD_ADD_ENERGY, -50000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_BTN_NRG_APPLY_CUSTOM && code == BN_CLICKED) {
            char buf[64]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_NRG_CUSTOM), buf, sizeof(buf));
            int val = atoi(buf);
            if (val != 0) {
                EnqueueCommand({ CMD_ADD_ENERGY, val, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }
        }
        else if (id == IDC_CHK_NRG_FREE_SEGMENTS && code == BN_CLICKED) {
            g_freeNewSegments = (SendMessageA(GetDlgItem(hwnd, IDC_CHK_NRG_FREE_SEGMENTS), BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (id == IDC_BTN_NRG_UNLOCK_ALL_TECH && code == BN_CLICKED) {
            EnqueueCommand({ CMD_UNLOCK_ALL_TECH, 0, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_COMBO_PEEP_TYPE && code == CBN_SELCHANGE) {
            SyncPeepControlsForSelection();
        }
        else if (id == IDC_BTN_PEEP_SPAWN && code == BN_CLICKED) {
            int idx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_PEEP_TYPE), CB_GETCURSEL, 0, 0);
            int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_PEEP_DECK), CB_GETCURSEL, 0, 0);

            char bufX[16]{}, bufY[16]{}, bufQty[16]{}, bufSkill[16]{}, bufDed[16]{}, bufLoy[16]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_X), bufX, sizeof(bufX));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_Y), bufY, sizeof(bufY));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_QTY), bufQty, sizeof(bufQty));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_SKILL), bufSkill, sizeof(bufSkill));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_DEDICATION), bufDed, sizeof(bufDed));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_LOYALTY), bufLoy, sizeof(bufLoy));

            int baseX = ClampInt(atoi(bufX), 0, 15);
            int baseY = ClampInt(atoi(bufY), 0, 31);
            int qty = ClampInt(atoi(bufQty), 1, 50);
            unsigned char skill = (unsigned char)ClampInt(atoi(bufSkill), 0, 5);
            unsigned char ded = (unsigned char)ClampInt(atoi(bufDed), 0, 5);
            unsigned char loy = (unsigned char)ClampInt(atoi(bufLoy), 0, 5);

            char clampedBuf[16];
            sprintf_s(clampedBuf, "%d", qty);
            SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_QTY), clampedBuf);

            if (idx >= 0 && idx < PEEP_CATALOG_COUNT) {
                const auto& entry = g_peepCatalog[idx];
                bool isResident = entry.canBeResident &&
                    (SendMessageA(GetDlgItem(hwnd, IDC_CHK_PEEP_RESIDENT), BM_GETCHECK, 0, 0) == BST_CHECKED);

                for (int i = 0; i < qty; i++) {
                    int spawnX = (baseX + (i % 6)) & 0x0F;
                    int spawnY = (baseY + (i / 6)) & 0x1F;
                    EnqueueCommand({
                        CMD_SPAWN_PEEP, 0, entry.id, 0,
                        spawnX, spawnY, deck,
                        isResident, skill, ded, loy
                        });
                }
            }
        }
        else if (id == IDC_COMBO_CRATE_TYPE && code == CBN_SELCHANGE) {
            SyncCrateControlsForSelection();
        }
        else if (id == IDC_BTN_CRATE_SPAWN && code == BN_CLICKED) {
            int idx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_TYPE), CB_GETCURSEL, 0, 0);
            int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_DECK), CB_GETCURSEL, 0, 0);

            HWND hComboSub = GetDlgItem(hwnd, IDC_COMBO_CRATE_SUBTYPE);
            int subIdx = (int)SendMessageA(hComboSub, CB_GETCURSEL, 0, 0);
            unsigned int extraVal = (subIdx >= 0) ? (unsigned int)SendMessageA(hComboSub, CB_GETITEMDATA, subIdx, 0) : 0;

            char bufX[16]{}, bufY[16]{}, bufQty[16]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_X), bufX, sizeof(bufX));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_Y), bufY, sizeof(bufY));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_QTY), bufQty, sizeof(bufQty));

            int baseX = ClampInt(atoi(bufX), 0, 15);
            int baseY = ClampInt(atoi(bufY), 0, 31);
            int qty = ClampInt(atoi(bufQty), 1, 50);

            char clampedBuf[16];
            sprintf_s(clampedBuf, "%d", qty);
            SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_QTY), clampedBuf);

            if (idx >= 0 && idx < CRATE_CATALOG_COUNT) {
                const auto& entry = g_crateCatalog[idx];
                for (int i = 0; i < qty; i++) {
                    int spawnX = (baseX + (i % 6)) & 0x0F;
                    int spawnY = (baseY + (i / 6)) & 0x1F;
                    EnqueueCommand({
                        CMD_SPAWN_CRATE, 0, entry.id, extraVal,
                        spawnX, spawnY, deck,
                        false, 0, 0, 0
                        });
                }
            }
        }
        else if (id == IDC_BTN_CRATE_SPAWN_ALL_STD && code == BN_CLICKED) {
            int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_DECK), CB_GETCURSEL, 0, 0);
            char bufX[16]{}, bufY[16]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_X), bufX, sizeof(bufX));
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_Y), bufY, sizeof(bufY));
            int baseX = ClampInt(atoi(bufX), 0, 15);
            int baseY = ClampInt(atoi(bufY), 0, 31);

            for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
                const auto& entry = g_crateCatalog[i];
                unsigned int defExtra = (entry.subTypeMode == 1) ? 117 : ((entry.subTypeMode != 0) ? 1 : 0);
                int gx = (baseX + (i % 6)) & 0x0F;
                int gz = (baseY + (i / 6)) & 0x1F;
                EnqueueCommand({ CMD_SPAWN_CRATE, 0, entry.id, defExtra, gx, gz, deck, false, 0, 0, 0 });
            }
        }
        else if ((id == IDC_COMBO_BUILD_CAT && code == CBN_SELCHANGE) ||
            (id == IDC_EDIT_BUILD_SEARCH && code == EN_CHANGE)) {
            RebuildClassicBuildVarList(hwnd, false);
        }
        else if (id == IDC_COMBO_BUILD_VAR && code == CBN_SELCHANGE) {
            SyncClassicBuildVarValueUI(hwnd);
        }
        else if (id == IDC_BTN_BUILD_APPLY && code == BN_CLICKED) {
            int sel = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_BUILD_VAR), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_classicFilteredBuildIndices.size()) {
                auto& v = g_buildVars[g_classicFilteredBuildIndices[sel]];
                char buf[64]{};
                GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_BUILD_VAL), buf, sizeof(buf));
                if (!v.isFloat) *(int*)v.pValueAddr = atoi(buf);
                else            *(float*)v.pValueAddr = (float)atof(buf);
                SyncClassicBuildVarValueUI(hwnd);
            }
        }
        else if (id == IDC_BTN_BUILD_RESET && code == BN_CLICKED) {
            int sel = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_BUILD_VAR), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_classicFilteredBuildIndices.size()) {
                auto& v = g_buildVars[g_classicFilteredBuildIndices[sel]];
                if (!v.isFloat) *(int*)v.pValueAddr = v.defaultInt;
                else            *(float*)v.pValueAddr = v.defaultFloat;
                SyncClassicBuildVarValueUI(hwnd);
            }
        }
        break;
    }

    case WM_CLOSE:
        SetMenuVisibility(false);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void CreateClassicMenuWindow(HMODULE hModule)
{
    WNDCLASSA wc{};
    wc.lpfnWndProc = ClassicMenuWndProc;
    wc.hInstance = hModule;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "StartopiaStandaloneCheatMenu";
    RegisterClassA(&wc);

    g_menuHwnd = CreateWindowExA(
        (g_alwaysOnTop ? WS_EX_TOPMOST : 0) | WS_EX_TOOLWINDOW,
        "StartopiaStandaloneCheatMenu",
        "Startopia Cheeseburger Menu",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        35, 35, 488, 500,
        nullptr, nullptr, hModule, nullptr
    );

    SetMenuVisibility(g_menuVisible);
    if (g_menuVisible) UpdateWindow(g_menuHwnd);
}

// ==============================================================================
// External DirectX 11 + Dear ImGui Overlay Window (Used when UseImGUI=true)
// ==============================================================================
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (FAILED(res)) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

LRESULT CALLBACK ImGuiOverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        SetMenuVisibility(false);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void RenderImGuiMenuContents()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Startopia Cheeseburger Menu", nullptr, flags);
    ImGui::PushTextWrapPos(0.0f);

    if (ImGui::Checkbox("Always on top", &g_alwaysOnTop)) {
        ApplyMenuAlwaysOnTop(g_alwaysOnTop);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  |  Game Build: %s", g_offsets.versionLabel);
    ImGui::Separator();

    if (ImGui::BeginTabBar("CheeseburgerTabs")) {
        // --- TAB 0: Energy/Tech ---
        if (ImGui::BeginTabItem("Energy/Tech")) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Direct Wallet Energy Controls (No Script Needed):");
            ImGui::Spacing();

            if (ImGui::Button("+50,000 Energy", ImVec2(164, 32))) {
                EnqueueCommand({ CMD_ADD_ENERGY, 50000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }
            ImGui::SameLine();
            if (ImGui::Button("+500,000 Energy", ImVec2(170, 32))) {
                EnqueueCommand({ CMD_ADD_ENERGY, 500000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }
            ImGui::SameLine();
            if (ImGui::Button("-50,000 Energy", ImVec2(164, 32))) {
                EnqueueCommand({ CMD_ADD_ENERGY, -50000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            static int customEnergy = 100000;
            ImGui::TextUnformatted("Custom Energy Amount (Positive or Negative):");
            ImGui::SetNextItemWidth(285.0f);
            ImGui::InputInt("##CustomEnergy", &customEnergy, 10000, 50000);
            ImGui::SameLine();
            if (ImGui::Button("Apply Energy", ImVec2(218, 26)) && customEnergy != 0) {
                EnqueueCommand({ CMD_ADD_ENERGY, customEnergy, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Unlock all tech", ImVec2(510, 36))) {
                EnqueueCommand({ CMD_UNLOCK_ALL_TECH, 0, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }
            ImGui::TextWrapped(
                "Discovers @alltech (all theories & blueprints). Note: Avoid using "
                "during the Turakken research mission unless you want an instant win.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f),
                "Warning: Modifying tech or simulation states may cause unwanted engine behavior.");

            ImGui::EndTabItem();
        }

        // --- TAB 1: Peeps ---
        if (ImGui::BeginTabItem("Peeps")) {
            static int peepIdx = 9;
            static int peepDeck = 0;
            static int peepX = 6;
            static int peepY = 6;
            static int peepQty = 1;
            static bool peepRes = true;
            static int peepSkill = 5;
            static int peepDed = 5;
            static int peepLoy = 5;

            const char* deckLabels[] = {
                "Deck 0 - Sub Deck (Engineering / Cargo)",
                "Deck 1 - Leisure Deck",
                "Deck 2 - Bio Deck"
            };

            ImGui::Spacing();
            ImGui::TextUnformatted("Peep / Droid Species:");
            ImGui::SetNextItemWidth(510.0f);
            if (ImGui::BeginCombo("##PeepSpecies", g_peepCatalog[peepIdx].label)) {
                for (int i = 0; i < PEEP_CATALOG_COUNT; i++) {
                    bool isSelected = (peepIdx == i);
                    if (ImGui::Selectable(g_peepCatalog[i].label, isSelected)) {
                        peepIdx = i;
                        if (!g_peepCatalog[peepIdx].canBeResident) {
                            peepRes = false;
                        }
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TextUnformatted("Target Deck:");
            ImGui::SetNextItemWidth(510.0f);
            ImGui::Combo("##PeepDeck", &peepDeck, deckLabels, 3);

            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tile X:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(98.0f);
            ImGui::InputInt("##PeepX", &peepX);
            peepX = ClampInt(peepX, 0, 15);

            ImGui::SameLine(0.0f, 14.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tile Y:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(98.0f);
            ImGui::InputInt("##PeepY", &peepY);
            peepY = ClampInt(peepY, 0, 31);

            ImGui::SameLine(0.0f, 14.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Qty:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(98.0f);
            ImGui::InputInt("##PeepQty", &peepQty);
            peepQty = ClampInt(peepQty, 1, 50);

            const auto& entry = g_peepCatalog[peepIdx];
            if (!entry.canBeResident) {
                peepRes = false;
                ImGui::BeginDisabled();
                ImGui::Checkbox("Resident (Hired Staff)", &peepRes);
                ImGui::EndDisabled();
            }
            else {
                ImGui::Checkbox("Resident (Hired Staff)", &peepRes);
            }

            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("Skill", &peepSkill);
            peepSkill = ClampInt(peepSkill, 0, 5);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("Dedication", &peepDed);
            peepDed = ClampInt(peepDed, 0, 5);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("Loyalty", &peepLoy);
            peepLoy = ClampInt(peepLoy, 0, 5);

            ImGui::Spacing();
            if (ImGui::Button("Spawn Peep(s) Now (Paced Grid)", ImVec2(510, 36))) {
                bool isResident = entry.canBeResident && peepRes;
                for (int i = 0; i < peepQty; i++) {
                    int spawnX = (peepX + (i % 6)) & 0x0F;
                    int spawnY = (peepY + (i / 6)) & 0x1F;
                    EnqueueCommand({
                        CMD_SPAWN_PEEP, 0, entry.id, 0,
                        spawnX, spawnY, peepDeck,
                        isResident,
                        (unsigned char)peepSkill,
                        (unsigned char)peepDed,
                        (unsigned char)peepLoy
                        });
                }
            }

            ImGui::EndTabItem();
        }

        // --- TAB 2: Crates ---
        if (ImGui::BeginTabItem("Crates")) {
            static int crateIdx = 5;
            static int crateSubIdx = 0;
            static int crateDeck = 0;
            static int crateX = 8;
            static int crateY = 6;
            static int crateQty = 1;

            const char* deckLabels[] = {
                "Deck 0 - Sub Deck (Engineering / Cargo)",
                "Deck 1 - Leisure Deck",
                "Deck 2 - Bio Deck"
            };

            ImGui::Spacing();
            ImGui::TextUnformatted("Crate / Portable Type (Alphabetical):");
            ImGui::SetNextItemWidth(510.0f);
            if (ImGui::BeginCombo("##CrateType", g_crateCatalog[crateIdx].label)) {
                for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
                    bool isSelected = (crateIdx == i);
                    if (ImGui::Selectable(g_crateCatalog[i].label, isSelected)) {
                        crateIdx = i;
                        crateSubIdx = 0;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const auto& entry = g_crateCatalog[crateIdx];
            unsigned int extraVal = 0;

            ImGui::TextUnformatted("Crate Sub-Type (Hardplan, Furniture, Droid, Tech):");
            ImGui::SetNextItemWidth(510.0f);
            if (entry.subTypeMode == 0) {
                ImGui::BeginDisabled();
                if (ImGui::BeginCombo("##CrateSub", "N/A - Standard Crate (Extra = 0)")) {
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
            }
            else if (entry.subTypeMode == 1) {
                if (crateSubIdx >= TECH_THEORY_COUNT) crateSubIdx = 0;
                if (ImGui::BeginCombo("##CrateSub", g_techTheoryCatalog[crateSubIdx].label)) {
                    for (int i = 0; i < TECH_THEORY_COUNT; i++) {
                        if (ImGui::Selectable(g_techTheoryCatalog[i].label, crateSubIdx == i)) crateSubIdx = i;
                    }
                    ImGui::EndCombo();
                }
                extraVal = g_techTheoryCatalog[crateSubIdx].id;
            }
            else if (entry.subTypeMode == 2) {
                if (crateSubIdx >= PEEP_CATALOG_COUNT) crateSubIdx = 0;
                if (ImGui::BeginCombo("##CrateSub", g_peepCatalog[crateSubIdx].label)) {
                    for (int i = 0; i < PEEP_CATALOG_COUNT; i++) {
                        if (ImGui::Selectable(g_peepCatalog[i].label, crateSubIdx == i)) crateSubIdx = i;
                    }
                    ImGui::EndCombo();
                }
                extraVal = g_peepCatalog[crateSubIdx].id;
            }
            else if (entry.subTypeMode == 3) {
                if (crateSubIdx >= ROOM_PLAN_COUNT) crateSubIdx = 0;
                if (ImGui::BeginCombo("##CrateSub", g_roomPlanCatalog[crateSubIdx].label)) {
                    for (int i = 0; i < ROOM_PLAN_COUNT; i++) {
                        if (ImGui::Selectable(g_roomPlanCatalog[i].label, crateSubIdx == i)) crateSubIdx = i;
                    }
                    ImGui::EndCombo();
                }
                extraVal = g_roomPlanCatalog[crateSubIdx].id;
            }
            else if (entry.subTypeMode == 4) {
                if (crateSubIdx >= FURN_PLAN_COUNT) crateSubIdx = 0;
                if (ImGui::BeginCombo("##CrateSub", g_furniturePlanCatalog[crateSubIdx].label)) {
                    for (int i = 0; i < FURN_PLAN_COUNT; i++) {
                        if (ImGui::Selectable(g_furniturePlanCatalog[i].label, crateSubIdx == i)) crateSubIdx = i;
                    }
                    ImGui::EndCombo();
                }
                extraVal = g_furniturePlanCatalog[crateSubIdx].id;
            }

            ImGui::TextUnformatted("Target Deck:");
            ImGui::SetNextItemWidth(510.0f);
            ImGui::Combo("##CrateDeck", &crateDeck, deckLabels, 3);

            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tile X:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(98.0f);
            ImGui::InputInt("##CrateX", &crateX);
            crateX = ClampInt(crateX, 0, 15);

            ImGui::SameLine(0.0f, 14.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tile Y:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(98.0f);
            ImGui::InputInt("##CrateY", &crateY);
            crateY = ClampInt(crateY, 0, 31);

            ImGui::SameLine(0.0f, 14.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Qty:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(98.0f);
            ImGui::InputInt("##CrateQty", &crateQty);
            crateQty = ClampInt(crateQty, 1, 50);

            ImGui::Spacing();
            if (ImGui::Button("Spawn Crate(s) Now", ImVec2(251, 36))) {
                for (int i = 0; i < crateQty; i++) {
                    int spawnX = (crateX + (i % 6)) & 0x0F;
                    int spawnY = (crateY + (i / 6)) & 0x1F;
                    EnqueueCommand({
                        CMD_SPAWN_CRATE, 0, entry.id, extraVal,
                        spawnX, spawnY, crateDeck,
                        false, 0, 0, 0
                        });
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Spawn All 17 Types", ImVec2(251, 36))) {
                for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
                    const auto& cEntry = g_crateCatalog[i];
                    unsigned int defExtra = (cEntry.subTypeMode == 1) ? 117 : ((cEntry.subTypeMode != 0) ? 1 : 0);
                    int gx = (crateX + (i % 6)) & 0x0F;
                    int gz = (crateY + (i / 6)) & 0x1F;
                    EnqueueCommand({ CMD_SPAWN_CRATE, 0, cEntry.id, defExtra, gx, gz, crateDeck, false, 0, 0, 0 });
                }
            }

            ImGui::EndTabItem();
        }

        // --- TAB 3: Build Changers ---
        if (ImGui::BeginTabItem("Build Changers")) {
            EnsureBuildVarsLoaded();

            static int selectedCat = 0;
            static char searchBuf[64] = "";

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f),
                "Warning: Adjusting build changer settings may cause unwanted engine behavior.");
            ImGui::Spacing();

            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("##BuildCat", &selectedCat, g_buildCategoryNames, BUILD_CATEGORY_COUNT);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(282.0f);
            ImGui::InputTextWithHint("##BuildSearch", "Search variable name...", searchBuf, sizeof(searchBuf));

            ImGui::Spacing();
            if (ImGui::Button("Reset Visible to Defaults", ImVec2(372, 24))) {
                for (auto& v : g_buildVars) {
                    if (!MatchesBuildFilter(v, selectedCat, searchBuf)) continue;
                    if (v.isFloat) *(float*)v.pValueAddr = v.defaultFloat;
                    else           *(int*)v.pValueAddr = v.defaultInt;
                }
                LogMsg("[BUILD-VARS] Reset filtered build changers to defaults.\n");
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh List", ImVec2(130, 24))) {
                EnsureBuildVarsLoaded(true);
            }

            ImGui::Separator();
            ImGui::BeginChild("BuildVarsScroll", ImVec2(0, 285), false);

            int shownCount = 0;
            for (size_t i = 0; i < g_buildVars.size(); i++) {
                auto& v = g_buildVars[i];
                if (!MatchesBuildFilter(v, selectedCat, searchBuf)) continue;
                shownCount++;

                ImGui::PushID((int)i);
                ImGui::TextColored(
                    v.isFloat ? ImVec4(0.55f, 0.85f, 1.0f, 1.0f) : ImVec4(0.65f, 1.0f, 0.70f, 1.0f),
                    "%s", v.name.c_str());

                if (!v.isFloat) {
                    int curVal = *(int*)v.pValueAddr;
                    ImGui::SetNextItemWidth(300.0f);
                    if (ImGui::SliderInt("##Sld", &curVal, v.minInt, v.maxInt)) {
                        *(int*)v.pValueAddr = curVal;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(122.0f);
                    if (ImGui::InputInt("##Box", &curVal, 0, 0)) {
                        *(int*)v.pValueAddr = curVal;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset", ImVec2(60, 0))) {
                        *(int*)v.pValueAddr = v.defaultInt;
                    }
                }
                else {
                    float curVal = *(float*)v.pValueAddr;
                    ImGui::SetNextItemWidth(300.0f);
                    if (ImGui::SliderFloat("##Sld", &curVal, v.minFloat, v.maxFloat, "%.4f")) {
                        *(float*)v.pValueAddr = curVal;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(122.0f);
                    if (ImGui::InputFloat("##Box", &curVal, 0.0f, 0.0f, "%.4f")) {
                        *(float*)v.pValueAddr = curVal;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset", ImVec2(60, 0))) {
                        *(float*)v.pValueAddr = v.defaultFloat;
                    }
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (shownCount == 0) {
                ImGui::TextDisabled(g_buildVars.empty()
                    ? "Load into a mission or sandbox map to index Build Changers."
                    : "No Build Changers match the current category/search filter.");
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // --- TAB 4: Help/About ---
        if (ImGui::BeginTabItem("Help/About")) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Startopia Cheeseburger Menu v1.0 (Dear ImGui DX11 Overlay)");
            ImGui::TextDisabled("Build Date: 11/21/2026  |  Game: %s", g_offsets.versionLabel);
            ImGui::TextDisabled("Powered by Dear ImGui & MinHook  |  Dedicated to Chris Nelson.");
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::BulletText("[%s] : Show / Hide this Menu Overlay", g_config.hotkeyName);
            ImGui::BulletText("INI File : StartopiaCheeseburger.ini (auto-generated)");
            ImGui::BulletText("Decks : 0 = Sub Deck | 1 = Leisure | 2 = Bio");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Batch Spawning: Spawns 1 to 50 entities on a clean paced grid "
                "starting at your chosen Tile X / Tile Y coordinates.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopTextWrapPos();
    ImGui::End();
}

void RunImGuiWindowLoop(HMODULE hModule)
{
    WNDCLASSEXA wc{
        sizeof(WNDCLASSEXA), CS_CLASSDC, ImGuiOverlayWndProc, 0L, 0L,
        hModule, nullptr, nullptr, nullptr, nullptr,
        "StartopiaImGuiCheeseburgerMenu", nullptr
    };
    RegisterClassExA(&wc);

    g_menuHwnd = CreateWindowExA(
        (g_alwaysOnTop ? WS_EX_TOPMOST : 0) | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        "Startopia Cheeseburger Menu",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        35, 35, 545, 505,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!CreateDeviceD3D(g_menuHwnd)) {
        CleanupDeviceD3D();
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        LogMsg("[ERROR] DX11 Init failed for ImGui menu; falling back to Classic Win32 UI.\n");
        CreateClassicMenuWindow(hModule);

        MSG msg{};
        while (true) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (GetAsyncKeyState(g_config.hotkeyVK) & 1) {
                SetMenuVisibility(!g_menuVisible);
            }
            Sleep(20);
        }
        return;
    }

    SetMenuVisibility(g_menuVisible);
    if (g_menuVisible) UpdateWindow(g_menuHwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(g_menuHwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.00f };
    MSG msg{};

    while (true) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (GetAsyncKeyState(g_config.hotkeyVK) & 1) {
            SetMenuVisibility(!g_menuVisible);
        }

        if (g_menuVisible && g_pd3dDevice != nullptr) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            RenderImGuiMenuContents();

            ImGui::Render();
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_pSwapChain->Present(1, 0);
        }
        else {
            Sleep(25);
        }
    }
}

// ==============================================================================
// Background Initialization & Menu Message Pump Thread
// ==============================================================================
DWORD WINAPI MainThread(LPVOID lpReserved)
{
    HMODULE hModule = (HMODULE)lpReserved;
    Sleep(2500);

    if (g_config.toggleConsole) {
        AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }

    if (g_config.outputCrashLog) {
        fopen_s(&g_logFile, "mod_crash.log", "w");
    }

    AddVectoredExceptionHandler(1, GlobalVectoredCrashHandler);

    g_baseAddress = (DWORD)GetModuleHandle(NULL);
    LogMsg("Startopia Base Address: 0x%08X\n", g_baseAddress);
    DetectGameVersion();

    LogMsg("Loaded INI: ToggleConsole=%d | UseImGUI=%d | Hotkey=%s (0x%02X) | OutputCrashLog=%d | ShowMenuOnStart=%d | ForceWindowed=%d\n",
        g_config.toggleConsole ? 1 : 0,
        g_config.useImGUI ? 1 : 0,
        g_config.hotkeyName, g_config.hotkeyVK,
        g_config.outputCrashLog ? 1 : 0,
        g_config.showMenuOnStart ? 1 : 0,
        g_config.forceWindowed ? 1 : 0);

    if (g_offsets.deductEnergy != 0 &&
        (MH_Initialize() == MH_OK || MH_Initialize() == MH_ERROR_ALREADY_INITIALIZED)) {
        void* pTargetDeduct = (void*)(g_baseAddress + g_offsets.deductEnergy);
        if (MH_CreateHook(pTargetDeduct, &HookedDeductEnergy, (LPVOID*)&pOriginalDeductEnergy) == MH_OK &&
            MH_EnableHook(pTargetDeduct) == MH_OK) {
            LogMsg("Hooked DeductEnergy at 0x%p\n", pTargetDeduct);
        }
        else {
            LogMsg("[WARN] Failed to hook DeductEnergy.\n");
        }
    }

    if (g_gameHwnd == nullptr) {
        for (int attempt = 0; attempt < 20 && g_gameHwnd == nullptr; attempt++) {
            g_gameHwnd = FindStartopiaMainWindow();
            if (g_gameHwnd == nullptr) Sleep(250);
        }
    }

    if (g_gameHwnd != nullptr) {
        if (pOriginalGameWndProc == nullptr) {
            pOriginalGameWndProc = (WNDPROC)SetWindowLongA(g_gameHwnd, GWL_WNDPROC, (LONG)SubclassGameWndProc);
            SetTimer(g_gameHwnd, GAME_QUEUE_TIMER_ID, 100, nullptr);
        }
        LogMsg("Attached paced command queue (100ms) to HWND: 0x%p\n", g_gameHwnd);
    }
    else {
        LogMsg("[ERROR] Could not locate Startopia main window handle!\n");
    }

    LogMsg("Startopia Cheeseburger Menu ready! Press [%s] to toggle visibility.\n", g_config.hotkeyName);

    if (g_config.useImGUI) {
        RunImGuiWindowLoop(hModule);
    }
    else {
        CreateClassicMenuWindow(hModule);
        MSG msg{};
        while (true) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }

            if (GetAsyncKeyState(g_config.hotkeyVK) & 1) {
                SetMenuVisibility(!g_menuVisible);
            }

            Sleep(20);
        }
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOrCreateIniConfig();
        if (g_config.forceWindowed) {
            InstallWindowedModeEnforcer();
        }
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}