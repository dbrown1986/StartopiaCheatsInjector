#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <queue>
#include <mutex>
#include "MinHook.h"

#pragma comment(lib, "comctl32.lib")

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
// Startopia Steam Build Dynamic Offsets (Ghidra Addr - 0x00400000)
// ==============================================================================
const DWORD OFFSET_SCRIPT_ADD_ENERGY = 0x001AEEC0; // FUN_005aeec0 (Player Energy Modifier)
const DWORD OFFSET_SPAWN_PEEP_SP = 0x0022E2B0; // FUN_0062e2b0 (Singleplayer Peep Spawner)
const DWORD OFFSET_SPAWN_PEEP_MP = 0x0022E3D0; // FUN_0062e3d0 (Multiplayer/Alt Peep Spawner)
const DWORD OFFSET_SPAWN_PORTABLE = 0x0018F9D0; // FUN_0058f9d0 (Portable/Crate Spawner)
const DWORD OFFSET_GET_PLAYER_SECTOR = 0x0023C680; // FUN_0063c680 (Gets player's station sector)
const DWORD OFFSET_PACK_COORDS = 0x00061280; // FUN_00461280 (Packs X, Z, Deck into tile ID)
const DWORD OFFSET_SPECIES_TABLE = 0x004D9AF8; // DAT_008d9af8 (Loaded Peep definitions)
const DWORD OFFSET_FLAG_MP_MODE = 0x007FE1AC; // DAT_00bfe1ac
const DWORD OFFSET_PLAYER_PTR = 0x007F5170; // DAT_00bf5170

DWORD   g_baseAddress = 0;
FILE* g_logFile = nullptr;
HWND    g_gameHwnd = nullptr;
WNDPROC pOriginalGameWndProc = nullptr;
HWND    g_menuHwnd = nullptr;
HWND    g_tabHwnd = nullptr;
HFONT   g_uiFont = nullptr;

// ==============================================================================
// Verified Crate & Peep Catalogs
// ==============================================================================
struct CrateCatalogEntry {
    unsigned int id;
    const char* label;
    bool requiresExtra;
    unsigned int defaultExtra;
};

static const CrateCatalogEntry g_crateCatalog[] = {
    { 1,  "01 - CT_INDUSTRIAL (Industrial Crate)",           false, 0 },
    { 2,  "02 - CT_MEDICAL_SUPPLY (Medical Supplies)",       false, 0 },
    { 3,  "03 - CT_FOOD_SUPPLY (Food Crate)",                false, 0 },
    { 4,  "04 - CT_HARDWARE (Hardware Crate)",               false, 0 },
    { 5,  "05 - CT_TECHNOLOGY (Tech Crate [Extra >= 1])",    true,  1 },
    { 6,  "06 - CT_LUXURY_ITEM (Luxury Goods)",              false, 0 },
    { 7,  "07 - CT_ORE (Ore Crate)",                         false, 0 },
    { 8,  "08 - CT_BLACK_MARKET_GOODS (Black Market)",       false, 0 },
    { 9,  "09 - CT_BOMB (Explosive / Bomb)",                 false, 0 },
    { 10, "10 - CT_ALIEN_ARTIFACT (Alien Artifact)",         false, 0 },
    { 11, "11 - CT_JUNK (Junk [Flag=0])",                    false, 0 },
    { 12, "12 - CT_LITTER (Litter [Flag=0])",                false, 0 },
    { 13, "13 - CT_TURDITE (Turdite)",                       false, 0 },
    { 14, "14 - CT_SCUTTER (Droid/Peep Crate [Extra 1..22])", true, 1 },
    { 15, "15 - CT_HARDPLAN (Room Flat-Pack [Extra 1..41])",  true, 1 },
    { 16, "16 - CT_FURNITUREPLAN (Furniture [Extra 1..45])",  true, 1 },
    { 17, "17 - CT_DAMAGED_GOODS (Damaged Goods)",           false, 0 }
};
static const int CRATE_CATALOG_COUNT = sizeof(g_crateCatalog) / sizeof(g_crateCatalog[0]);

struct PeepCatalogEntry {
    unsigned int id;
    const char* label;
    bool canBeResident;
};

static const PeepCatalogEntry g_peepCatalog[] = {
    { 1,  "01 - Scutter Mk I (Maintenance Droid)", false },
    { 2,  "02 - Wheel Scutter Mk II",              false },
    { 3,  "03 - Hover Scutter Mk III",             false },
    { 4,  "04 - Police Scutter",                   false },
    { 5,  "05 - Salt Hog",                         true  },
    { 6,  "06 - Gray",                             true  },
    { 7,  "07 - Kasvagorian",                      true  },
    { 8,  "08 - Grekka Targ",                      true  },
    { 9,  "09 - Karmarama",                        true  },
    { 10, "10 - Zedem Monk",                       true  },
    { 11, "11 - Turakken",                         true  },
    { 12, "12 - Dahanese Siren (Female)",          true  },
    { 13, "13 - Dahanese Siren (Male)",            true  },
    { 14, "14 - Gem Slug",                         false },
    { 15, "15 - Penitent",                         false },
    { 19, "19 - Space Vermin",                     false },
    { 20, "20 - Memau",                            false },
    { 21, "21 - Skrasher",                         false },
    { 22, "22 - Spy",                              false }
};
static const int PEEP_CATALOG_COUNT = sizeof(g_peepCatalog) / sizeof(g_peepCatalog[0]);

// ==============================================================================
// Native 72-Byte Peep Attribute Struct (Matches local_50..local_8 in FUN_006702a0)
// ==============================================================================
#pragma pack(push, 1)
struct PeepAttributesFull {
    unsigned int peepType;       // +0x00 (local_50): Peep species ID
    unsigned short flags;        // +0x04 (local_4c): 0x0001 = Resident, 0x0000 = Visitor
    unsigned char moodData[54];  // +0x06..+0x3B: Zero-initialized mood/preference array
    unsigned int defaultFlag;    // +0x3C (local_14): 1 = use default mood weights
    unsigned char skill;         // +0x40 (local_10): 0..5
    unsigned char loyalty;       // +0x41 (local_f low): 0..5
    unsigned char dedication;    // +0x42 (local_f high): 0..5
    unsigned char infected;      // +0x43 (local_c): 0 = healthy
    unsigned char reserved[4];   // +0x44..+0x47
};
#pragma pack(pop)

// ==============================================================================
// Thread-Safe Main-Thread Command Queue
// ==============================================================================
enum CommandType {
    CMD_ADD_ENERGY,
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
    std::lock_guard<std::mutex> lock(g_cmdMutex);
    g_cmdQueue.push(cmd);
}

// ==============================================================================
// Logger & Global Crash Handler
// ==============================================================================
void LogMsg(const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    printf("%s", buffer);
    if (g_logFile) {
        fprintf(g_logFile, "%s", buffer);
        fflush(g_logFile);
    }
}

LONG WINAPI GlobalVectoredCrashHandler(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == EXCEPTION_BREAKPOINT) {
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

// ==============================================================================
// Engine Function Typedefs & Execution Helpers
// ==============================================================================
typedef unsigned int(__fastcall* tScriptAddEnergy)(void* pPlayer, int amount);
typedef int(__fastcall* tGetPlayerSector)(void* pPlayer, int unusedDeck);
typedef unsigned int* (__thiscall* tPackCoords)(unsigned int* outPacked, char x, unsigned int z, unsigned int deck);
typedef unsigned int(__fastcall* tSpawnPeepSP)(int mode, unsigned int* pPackedCoords, unsigned int peepType, unsigned short angle, void* pPlayer, void* pAttr);
typedef void(__fastcall* tSpawnPeepMP)(int mode, unsigned int* pPackedCoords, int peepType, unsigned int angle, void* pPlayer, int* pAttr);
typedef unsigned int(__fastcall* tSpawnPortable)(unsigned int* pPackedCoords, unsigned int cargoType, unsigned int angle, void* pPlayer, unsigned int extraData, int flag);

unsigned int GetSafePlayerTile(void* pPlayer, int localOffsetX, int localZ, int deck)
{
    auto GetSector = (tGetPlayerSector)(g_baseAddress + OFFSET_GET_PLAYER_SECTOR);
    auto PackCoords = (tPackCoords)(g_baseAddress + OFFSET_PACK_COORDS);

    unsigned int minX = *(unsigned int*)((BYTE*)pPlayer + 0x1785 + deck * 4);
    unsigned int maxX = *(unsigned int*)((BYTE*)pPlayer + 0x1791 + deck * 4);
    int sector = GetSector(pPlayer, deck);

    unsigned char finalX = (unsigned char)((minX + localOffsetX) & 0xFF);
    if (minX == 0 && maxX == 0) {
        finalX = (unsigned char)((sector * 0x10 + localOffsetX) & 0xFF);
    }

    unsigned int packedCoord = 0;
    PackCoords(&packedCoord, (char)finalX, (unsigned int)localZ, (unsigned int)deck);
    return packedCoord;
}

void ExecuteAddEnergy(void* pPlayer, int amount)
{
    auto ScriptAddEnergy = (tScriptAddEnergy)(g_baseAddress + OFFSET_SCRIPT_ADD_ENERGY);
    LogMsg("[EXEC-ENERGY] Modifying player energy by %+d (pPlayer=0x%p)\n", amount, pPlayer);
    ScriptAddEnergy(pPlayer, amount);
}

void ExecuteSpawnPeep(const ModCommand& cmd, void* pPlayer)
{
    void** speciesTable = (void**)(g_baseAddress + OFFSET_SPECIES_TABLE);
    if (cmd.id < 1 || cmd.id > 23 || speciesTable[cmd.id] == nullptr) {
        LogMsg("[PEEP-SKIP] Peep ID %u is NOT loaded in DAT_008d9af8 for this map.\n", cmd.id);
        return;
    }

    int mpFlag = *(int*)(g_baseAddress + OFFSET_FLAG_MP_MODE);
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
        auto SpawnSP = (tSpawnPeepSP)(g_baseAddress + OFFSET_SPAWN_PEEP_SP);
        SpawnSP(0, &packedCoord, cmd.id, 0, pPlayer, &attr);
    }
    else {
        auto SpawnMP = (tSpawnPeepMP)(g_baseAddress + OFFSET_SPAWN_PEEP_MP);
        SpawnMP(0, &packedCoord, (int)cmd.id, 0, pPlayer, (int*)&attr);
    }
}

void ExecuteSpawnCrate(void* pPlayer, unsigned int cargoTypeId, unsigned int extraData, int localX, int localZ, int deck)
{
    if (cargoTypeId < 1 || cargoTypeId > 17) return;

    auto SpawnPortable = (tSpawnPortable)(g_baseAddress + OFFSET_SPAWN_PORTABLE);
    unsigned int packedCoord = GetSafePlayerTile(pPlayer, localX, localZ, deck);
    int flag = (cargoTypeId == 0xB || cargoTypeId == 0xC) ? 0 : 1;

    LogMsg("[EXEC-CRATE] Spawning CargoID %u | Extra=%u | Tile=(%d, %d) Deck=%d | Packed=0x%08X\n",
        cargoTypeId, extraData, localX, localZ, deck, packedCoord);

    unsigned int res = SpawnPortable(&packedCoord, cargoTypeId, 0, pPlayer, extraData, flag);
    LogMsg("[EXEC-CRATE] SpawnPortable returned: %u\n", res);
}

// ==============================================================================
// Subclassed Game Main-Thread WndProc + Timer
// ==============================================================================
#define GAME_QUEUE_TIMER_ID 0x5A70

void ProcessCommandQueueOnMainThread()
{
    void* pPlayer = *(void**)(g_baseAddress + OFFSET_PLAYER_PTR);
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
        case CMD_SPAWN_PEEP:
            ExecuteSpawnPeep(cmd, pPlayer);
            break;
        case CMD_SPAWN_CRATE:
            ExecuteSpawnCrate(pPlayer, cmd.id, cmd.extraData, cmd.localX, cmd.localZ, cmd.deck);
            break;
        }
    }
}

LRESULT CALLBACK SubclassGameWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TIMER && wParam == GAME_QUEUE_TIMER_ID) {
        ProcessCommandQueueOnMainThread();
    }
    return CallWindowProcA(pOriginalGameWndProc, hwnd, uMsg, wParam, lParam);
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
            if (strcmp(cls, "ConsoleWindowClass") != 0 && strcmp(cls, "StartopiaStandaloneCheatMenu") != 0) {
                return hwnd;
            }
        }
    }
    return nullptr;
}

// ==============================================================================
// Tabbed Win32 Overlay Menu Controls
// ==============================================================================
enum MenuControlIDs {
    IDC_MAIN_TAB = 100,

    // Tab 0: ENERGY MANIPULATION
    IDC_BTN_NRG_ADD_50K = 1001,
    IDC_BTN_NRG_ADD_500K,
    IDC_BTN_NRG_SUB_50K,
    IDC_EDIT_NRG_CUSTOM,
    IDC_BTN_NRG_APPLY_CUSTOM,
    IDC_LBL_NRG_1,
    IDC_LBL_NRG_2,

    // Tab 1: SPAWN PEEPS
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
    IDC_BTN_PEEP_LOG_LOADED,

    // Tab 2: SPAWN CRATES
    IDC_LBL_CRATE_TYPE = 3001,
    IDC_COMBO_CRATE_TYPE,
    IDC_LBL_CRATE_EXTRA,
    IDC_EDIT_CRATE_EXTRA,
    IDC_LBL_CRATE_DECK,
    IDC_COMBO_CRATE_DECK,
    IDC_BTN_CRATE_SPAWN_1,
    IDC_BTN_CRATE_SPAWN_5,
    IDC_BTN_CRATE_SPAWN_ALL_STD,

    // Tab 3: BUILD CHANGERS
    IDC_LBL_BUILD_TITLE = 4001,
    IDC_CHK_BUILD_INSTANT,
    IDC_CHK_BUILD_UNLOCK_ALL,
    IDC_LBL_BUILD_NOTE,

    // Tab 4: HELP
    IDC_LBL_HELP_TEXT = 5001
};

int ClampInt(int val, int minVal, int maxVal)
{
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

void SetChildFont(HWND hParent)
{
    EnumChildWindows(hParent, [](HWND hChild, LPARAM lParam) -> BOOL {
        SendMessageA(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
        }, (LPARAM)g_uiFont);
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

    ShowRange(1001, 1020, tabIndex == 0); // ENERGY MANIPULATION
    ShowRange(2001, 2030, tabIndex == 1); // SPAWN PEEPS
    ShowRange(3001, 3020, tabIndex == 2); // SPAWN CRATES
    ShowRange(4001, 4020, tabIndex == 3); // BUILD CHANGERS
    ShowRange(5001, 5020, tabIndex == 4); // HELP
}

void UpdatePeepResidentCheckboxState()
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

void UpdateCrateExtraFieldState()
{
    int idx = (int)SendMessageA(GetDlgItem(g_menuHwnd, IDC_COMBO_CRATE_TYPE), CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= CRATE_CATALOG_COUNT) return;

    const auto& entry = g_crateCatalog[idx];
    HWND hEditExtra = GetDlgItem(g_menuHwnd, IDC_EDIT_CRATE_EXTRA);
    EnableWindow(hEditExtra, entry.requiresExtra ? TRUE : FALSE);

    char buf[32];
    sprintf_s(buf, "%u", entry.defaultExtra);
    SetWindowTextA(hEditExtra, buf);
}

LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_TAB_CLASSES };
        InitCommonControlsEx(&icex);

        g_uiFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        g_tabHwnd = CreateWindowExA(0, WC_TABCONTROLA, "",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            8, 8, 445, 360, hwnd, (HMENU)IDC_MAIN_TAB, nullptr, nullptr);

        const char* tabNames[] = {
            "ENERGY MANIPULATION",
            "SPAWN PEEPS",
            "SPAWN CRATES",
            "BUILD CHANGERS",
            "HELP"
        };
        for (int i = 0; i < 5; i++) {
            TCITEMA tie{};
            tie.mask = TCIF_TEXT;
            tie.pszText = (LPSTR)tabNames[i];
            TabCtrl_InsertItem(g_tabHwnd, i, &tie);
        }

        // --- TAB 0: ENERGY MANIPULATION ---
        CreateWindowA("STATIC", "Direct Wallet Energy Controls (No Script Needed):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 48, 410, 20, hwnd, (HMENU)IDC_LBL_NRG_1, nullptr, nullptr);
        CreateWindowA("BUTTON", "+50,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 76, 130, 32, hwnd, (HMENU)IDC_BTN_NRG_ADD_50K, nullptr, nullptr);
        CreateWindowA("BUTTON", "+500,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            162, 76, 135, 32, hwnd, (HMENU)IDC_BTN_NRG_ADD_500K, nullptr, nullptr);
        CreateWindowA("BUTTON", "-50,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            307, 76, 130, 32, hwnd, (HMENU)IDC_BTN_NRG_SUB_50K, nullptr, nullptr);

        CreateWindowA("STATIC", "Custom Energy Amount (Positive or Negative):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 125, 410, 20, hwnd, (HMENU)IDC_LBL_NRG_2, nullptr, nullptr);
        CreateWindowA("EDIT", "100000", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL,
            22, 150, 210, 26, hwnd, (HMENU)IDC_EDIT_NRG_CUSTOM, nullptr, nullptr);
        CreateWindowA("BUTTON", "Apply Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            242, 148, 195, 30, hwnd, (HMENU)IDC_BTN_NRG_APPLY_CUSTOM, nullptr, nullptr);

        // --- TAB 1: SPAWN PEEPS ---
        CreateWindowA("STATIC", "Peep / Droid Species:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 46, 415, 18, hwnd, (HMENU)IDC_LBL_PEEP_TYPE, nullptr, nullptr);
        HWND hComboPeep = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 66, 415, 280, hwnd, (HMENU)IDC_COMBO_PEEP_TYPE, nullptr, nullptr);
        for (int i = 0; i < PEEP_CATALOG_COUNT; i++) {
            SendMessageA(hComboPeep, CB_ADDSTRING, 0, (LPARAM)g_peepCatalog[i].label);
        }
        SendMessageA(hComboPeep, CB_SETCURSEL, 4, 0); // Default to 05 - Salt Hog

        CreateWindowA("STATIC", "Target Deck:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 98, 415, 18, hwnd, (HMENU)IDC_LBL_PEEP_DECK, nullptr, nullptr);
        HWND hComboPeepDeck = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 118, 415, 120, hwnd, (HMENU)IDC_COMBO_PEEP_DECK, nullptr, nullptr);
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 0 - Sub Deck (Engineering / Cargo)");
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 1 - Leisure Deck");
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 2 - Bio Deck");
        SendMessageA(hComboPeepDeck, CB_SETCURSEL, 0, 0);

        // Coordinates (X, Y) and Quantity Row
        CreateWindowA("STATIC", "Tile X:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 152, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_X, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            72, 149, 55, 24, hwnd, (HMENU)IDC_EDIT_PEEP_X, nullptr, nullptr);

        CreateWindowA("STATIC", "Tile Y:", WS_CHILD | WS_CLIPSIBLINGS,
            142, 152, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_Y, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            192, 149, 55, 24, hwnd, (HMENU)IDC_EDIT_PEEP_Y, nullptr, nullptr);

        CreateWindowA("STATIC", "Quantity:", WS_CHILD | WS_CLIPSIBLINGS,
            265, 152, 65, 20, hwnd, (HMENU)IDC_LBL_PEEP_QTY, nullptr, nullptr);
        CreateWindowA("EDIT", "1", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            335, 149, 60, 24, hwnd, (HMENU)IDC_EDIT_PEEP_QTY, nullptr, nullptr);

        // Resident Checkbox
        HWND hChkRes = CreateWindowA("BUTTON", "Resident (Hired Staff)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 184, 210, 22, hwnd, (HMENU)IDC_CHK_PEEP_RESIDENT, nullptr, nullptr);
        SendMessageA(hChkRes, BM_SETCHECK, BST_CHECKED, 0);

        // Stats Row: Skill (0-5), Dedication (0-5), Loyalty (0-5)
        CreateWindowA("STATIC", "Skill (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 216, 72, 20, hwnd, (HMENU)IDC_LBL_PEEP_SKILL, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            96, 213, 42, 24, hwnd, (HMENU)IDC_EDIT_PEEP_SKILL, nullptr, nullptr);

        CreateWindowA("STATIC", "Dedication (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            150, 216, 108, 20, hwnd, (HMENU)IDC_LBL_PEEP_DEDICATION, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            260, 213, 42, 24, hwnd, (HMENU)IDC_EDIT_PEEP_DEDICATION, nullptr, nullptr);

        CreateWindowA("STATIC", "Loyalty (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            314, 216, 82, 20, hwnd, (HMENU)IDC_LBL_PEEP_LOYALTY, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            396, 213, 41, 24, hwnd, (HMENU)IDC_EDIT_PEEP_LOYALTY, nullptr, nullptr);

        // Spawn & Debug Buttons
        CreateWindowA("BUTTON", "Spawn Peep(s)", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 252, 415, 34, hwnd, (HMENU)IDC_BTN_PEEP_SPAWN, nullptr, nullptr);
        CreateWindowA("BUTTON", "Print Loaded Map Species IDs to Console", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 294, 415, 28, hwnd, (HMENU)IDC_BTN_PEEP_LOG_LOADED, nullptr, nullptr);

        // --- TAB 2: SPAWN CRATES ---
        CreateWindowA("STATIC", "Crate / Portable Type (CT_* Catalog):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 48, 415, 20, hwnd, (HMENU)IDC_LBL_CRATE_TYPE, nullptr, nullptr);
        HWND hComboCrate = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 72, 415, 280, hwnd, (HMENU)IDC_COMBO_CRATE_TYPE, nullptr, nullptr);
        for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
            SendMessageA(hComboCrate, CB_ADDSTRING, 0, (LPARAM)g_crateCatalog[i].label);
        }
        SendMessageA(hComboCrate, CB_SETCURSEL, 2, 0);

        CreateWindowA("STATIC", "Extra / Sub-Type ID (Tech, Droid, Room Plan, Furniture):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 108, 330, 20, hwnd, (HMENU)IDC_LBL_CRATE_EXTRA, nullptr, nullptr);
        CreateWindowA("EDIT", "0", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER | WS_DISABLED,
            362, 105, 75, 24, hwnd, (HMENU)IDC_EDIT_CRATE_EXTRA, nullptr, nullptr);

        CreateWindowA("STATIC", "Target Deck:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 138, 415, 20, hwnd, (HMENU)IDC_LBL_CRATE_DECK, nullptr, nullptr);
        HWND hComboCrateDeck = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 162, 415, 120, hwnd, (HMENU)IDC_COMBO_CRATE_DECK, nullptr, nullptr);
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 0 - Sub Deck (Engineering / Cargo)");
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 1 - Leisure Deck");
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 2 - Bio Deck");
        SendMessageA(hComboCrateDeck, CB_SETCURSEL, 0, 0);

        CreateWindowA("BUTTON", "Spawn 1x Crate", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 202, 200, 34, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN_1, nullptr, nullptr);
        CreateWindowA("BUTTON", "Spawn 5x Crates (Grid)", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            237, 202, 200, 34, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN_5, nullptr, nullptr);
        CreateWindowA("BUTTON", "Spawn All Standard Crates (Grid Layout)", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 246, 415, 32, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN_ALL_STD, nullptr, nullptr);

        // --- TAB 3: BUILD CHANGERS ---
        CreateWindowA("STATIC", "Station Build & Segment Modifiers:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 48, 415, 20, hwnd, (HMENU)IDC_LBL_BUILD_TITLE, nullptr, nullptr);
        CreateWindowA("BUTTON", "Instant Room & Furniture Construction (WIP)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 80, 415, 24, hwnd, (HMENU)IDC_CHK_BUILD_INSTANT, nullptr, nullptr);
        CreateWindowA("BUTTON", "Unlock All Segment Bulkheads (WIP)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 112, 415, 24, hwnd, (HMENU)IDC_CHK_BUILD_UNLOCK_ALL, nullptr, nullptr);
        CreateWindowA("STATIC",
            "Ready to wire directly to Startopia's build timer & segment\n"
            "ownership flags once offsets are confirmed.",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 152, 415, 45, hwnd, (HMENU)IDC_LBL_BUILD_NOTE, nullptr, nullptr);

        // --- TAB 4: HELP ---
        CreateWindowA("STATIC",
            "STARTOPIA STANDALONE DLL CHEAT MENU\n\n"
            "• [INSERT] : Show / Hide this Menu Overlay\n"
            "• Deck 0 = Sub Deck | Deck 1 = Leisure | Deck 2 = Bio\n"
            "• Tile X / Y are local offsets (0..15) inside your active sector.\n"
            "• Resident is disabled for Droids (01..04) & non-staff species\n"
            "  (Gem Slug, Penitent, Space Vermin, Memau, Skrasher, Spy).\n"
            "• Crate Extra IDs:\n"
            "   - ID 05 (Tech): Tech Theory ID (>= 1)\n"
            "   - ID 14 (Scutter): Peep/Droid ID (1..22)\n"
            "   - ID 15 (Hardplan): Room Type ID (1..41)\n"
            "   - ID 16 (Furniture): Furniture Type ID (1..45)",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 48, 415, 275, hwnd, (HMENU)IDC_LBL_HELP_TEXT, nullptr, nullptr);

        SetChildFont(hwnd);
        UpdatePeepResidentCheckboxState();
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

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // --- Energy Tab Actions ---
        if (id == IDC_BTN_NRG_ADD_50K && code == BN_CLICKED) {
            LogMsg("[UI] Queued +50,000 Energy\n");
            EnqueueCommand({ CMD_ADD_ENERGY, 50000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_BTN_NRG_ADD_500K && code == BN_CLICKED) {
            LogMsg("[UI] Queued +500,000 Energy\n");
            EnqueueCommand({ CMD_ADD_ENERGY, 500000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_BTN_NRG_SUB_50K && code == BN_CLICKED) {
            LogMsg("[UI] Queued -50,000 Energy\n");
            EnqueueCommand({ CMD_ADD_ENERGY, -50000, 0, 0, 0, 0, 0, false, 0, 0, 0 });
        }
        else if (id == IDC_BTN_NRG_APPLY_CUSTOM && code == BN_CLICKED) {
            char buf[64]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_NRG_CUSTOM), buf, sizeof(buf));
            int val = atoi(buf);
            if (val != 0) {
                LogMsg("[UI] Queued Custom Energy: %+d\n", val);
                EnqueueCommand({ CMD_ADD_ENERGY, val, 0, 0, 0, 0, 0, false, 0, 0, 0 });
            }
        }

        // --- Peep Tab Actions ---
        else if (id == IDC_COMBO_PEEP_TYPE && code == CBN_SELCHANGE) {
            UpdatePeepResidentCheckboxState();
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

            if (idx >= 0 && idx < PEEP_CATALOG_COUNT) {
                const auto& entry = g_peepCatalog[idx];
                bool isResident = entry.canBeResident &&
                    (SendMessageA(GetDlgItem(hwnd, IDC_CHK_PEEP_RESIDENT), BM_GETCHECK, 0, 0) == BST_CHECKED);

                LogMsg("[UI] Queued %dx %s at (%d,%d) Deck %d | Res=%d | S/D/L=%u/%u/%u\n",
                    qty, entry.label, baseX, baseY, deck, isResident ? 1 : 0, skill, ded, loy);

                for (int i = 0; i < qty; i++) {
                    int spawnX = (baseX + (i % 5)) & 0x0F;
                    int spawnY = (baseY + (i / 5)) & 0x1F;
                    EnqueueCommand({
                        CMD_SPAWN_PEEP, 0, entry.id, 0,
                        spawnX, spawnY, deck,
                        isResident, skill, ded, loy
                        });
                }
            }
        }
        else if (id == IDC_BTN_PEEP_LOG_LOADED && code == BN_CLICKED) {
            void** speciesTable = (void**)(g_baseAddress + OFFSET_SPECIES_TABLE);
            LogMsg("[SPECIES] Loaded Peep IDs in current map: ");
            for (int i = 1; i <= 23; i++) {
                if (speciesTable[i] != nullptr) LogMsg("%d ", i);
            }
            LogMsg("\n");
        }

        // --- Crate Tab Actions ---
        else if (id == IDC_COMBO_CRATE_TYPE && code == CBN_SELCHANGE) {
            UpdateCrateExtraFieldState();
        }
        else if (id == IDC_BTN_CRATE_SPAWN_1 && code == BN_CLICKED) {
            int idx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_TYPE), CB_GETCURSEL, 0, 0);
            int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_DECK), CB_GETCURSEL, 0, 0);
            char extraBuf[32]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_EXTRA), extraBuf, sizeof(extraBuf));
            unsigned int extraVal = (unsigned int)atoi(extraBuf);

            if (idx >= 0 && idx < CRATE_CATALOG_COUNT) {
                LogMsg("[UI] Queued 1x Crate ID %u (Extra=%u) on Deck %d\n", g_crateCatalog[idx].id, extraVal, deck);
                EnqueueCommand({ CMD_SPAWN_CRATE, 0, g_crateCatalog[idx].id, extraVal, 8, 6, deck, false, 0, 0, 0 });
            }
        }
        else if (id == IDC_BTN_CRATE_SPAWN_5 && code == BN_CLICKED) {
            int idx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_TYPE), CB_GETCURSEL, 0, 0);
            int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_DECK), CB_GETCURSEL, 0, 0);
            char extraBuf[32]{};
            GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_EXTRA), extraBuf, sizeof(extraBuf));
            unsigned int extraVal = (unsigned int)atoi(extraBuf);

            if (idx >= 0 && idx < CRATE_CATALOG_COUNT) {
                LogMsg("[UI] Queued 5x Crate ID %u (Extra=%u) on Deck %d\n", g_crateCatalog[idx].id, extraVal, deck);
                for (int i = 0; i < 5; i++) {
                    EnqueueCommand({ CMD_SPAWN_CRATE, 0, g_crateCatalog[idx].id, extraVal, 6 + i, 6, deck, false, 0, 0, 0 });
                }
            }
        }
        else if (id == IDC_BTN_CRATE_SPAWN_ALL_STD && code == BN_CLICKED) {
            int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_DECK), CB_GETCURSEL, 0, 0);
            LogMsg("[UI] Queued ALL 17 Crate Types on Deck %d\n", deck);
            int gridIdx = 0;
            for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
                const auto& entry = g_crateCatalog[i];
                int gx = 5 + (gridIdx % 6);
                int gz = 5 + (gridIdx / 6);
                gridIdx++;
                EnqueueCommand({ CMD_SPAWN_CRATE, 0, entry.id, entry.defaultExtra, gx, gz, deck, false, 0, 0, 0 });
            }
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void CreateTabbedMenuWindow(HMODULE hModule)
{
    WNDCLASSA wc{};
    wc.lpfnWndProc = MenuWndProc;
    wc.hInstance = hModule;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "StartopiaStandaloneCheatMenu";
    RegisterClassA(&wc);

    g_menuHwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "StartopiaStandaloneCheatMenu",
        "Startopia Cheat Mod (Standalone DLL)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        35, 35, 478, 415,
        nullptr, nullptr, hModule, nullptr
    );

    ShowWindow(g_menuHwnd, SW_SHOW);
    UpdateWindow(g_menuHwnd);
}

// ==============================================================================
// Background Initialization & Menu Message Pump Thread
// ==============================================================================
DWORD WINAPI MainThread(LPVOID lpReserved)
{
    HMODULE hModule = (HMODULE)lpReserved;
    Sleep(2500);

    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    fopen_s(&g_logFile, "mod_crash.log", "w");

    AddVectoredExceptionHandler(1, GlobalVectoredCrashHandler);

    g_baseAddress = (DWORD)GetModuleHandle(NULL);
    LogMsg("Startopia Base Address: 0x%08X\n", g_baseAddress);

    CreateTabbedMenuWindow(hModule);

    for (int attempt = 0; attempt < 20 && g_gameHwnd == nullptr; attempt++) {
        g_gameHwnd = FindStartopiaMainWindow();
        if (g_gameHwnd == nullptr) Sleep(250);
    }

    if (g_gameHwnd != nullptr) {
        pOriginalGameWndProc = (WNDPROC)SetWindowLongA(g_gameHwnd, GWL_WNDPROC, (LONG)SubclassGameWndProc);
        SetTimer(g_gameHwnd, GAME_QUEUE_TIMER_ID, 50, nullptr);
        LogMsg("Attached main-thread queue processor to Startopia HWND: 0x%p\n", g_gameHwnd);
    }
    else {
        LogMsg("[ERROR] Could not locate Startopia main window handle!\n");
    }

    LogMsg("Standalone Tabbed Cheat Menu ready! Press [INSERT] to toggle visibility.\n");

    MSG msg{};
    bool menuVisible = true;

    while (true) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (GetAsyncKeyState(VK_INSERT) & 1) {
            menuVisible = !menuVisible;
            ShowWindow(g_menuHwnd, menuVisible ? SW_SHOW : SW_HIDE);
        }

        Sleep(20);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}