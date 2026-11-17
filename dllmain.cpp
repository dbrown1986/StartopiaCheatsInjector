#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
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
const DWORD OFFSET_ENTITY_TABLE = 0x006A306C; // DAT_00aa306c (Global Entity Slot Array)
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
// Verified Crate, Room Hard-Plan, Furniture Plan & Peep Catalogs (Alphabetical)
// ==============================================================================
struct SubTypeEntry {
    unsigned int id;
    const char* label;
};

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
    // { 2,  "Lift (RT_LIFT)" }, (DO NOT SPAWN)
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
    int subTypeMode;   // 0 = Standard Crate, 1 = Tech (1..45), 2 = Scutter/Peep, 3 = Room Plan, 4 = Furniture Plan
    bool canMaintain;  // True for standard crates; false for Tech/Hardplan/Furniture/Scutter
};

static const CrateCatalogEntry g_crateCatalog[] = {
    { 10, "Alien Artifact",         0, true  },
    { 8,  "Black Market Supplies",  0, true  },
    { 9,  "BOMB!",                  0, true  },
    { 17, "Damaged Goods",          0, true  },
    { 14, "Droid Crate",            2, false },
    { 3,  "Food Crate",             0, true  },
    { 16, "Furniture Crate",        4, false },
    { 15, "Hardplan Crate",         3, false },
    { 4,  "Hardware Crate",         0, true  },
    { 1,  "Industrial Crate",       0, true  },
    { 11, "Junk",                   0, true  },
    { 12, "Litter",                 0, true  },
    { 6,  "Luxury Goods",           0, true  },
    { 2,  "Medical Supplies",       0, true  },
    { 7,  "Ore Crate",              0, true  },
    { 5,  "Technology Crate",       1, false },
    { 13, "Turdite",                0, true  },
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

// ==============================================================================
// Auto-Maintain Rules State (Thread-Safe, Stepped in Increments of 10 up to 999)
// ==============================================================================
struct PeepMaintainRule {
    bool enabled;
    int targetCount;
    int localX;
    int localY;
    int deck;
    bool resident;
    unsigned char skill;
    unsigned char dedication;
    unsigned char loyalty;
};

struct CrateMaintainRule {
    bool enabled;
    int targetCount;
    int localX;
    int localY;
    int deck;
};

PeepMaintainRule  g_peepMaintainRules[24]{};
CrateMaintainRule g_crateMaintainRules[18]{};
std::mutex        g_maintainMutex;

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
// Timestamped Dual Console + Immediate-Flush File Logger (mod_crash.log)
// ==============================================================================
void LogMsg(const char* fmt, ...)
{
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

    printf("%s", finalBuf);
    if (g_logFile) {
        fprintf(g_logFile, "%s", finalBuf);
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

    LogMsg("[EXEC-CRATE] CargoID=%u | Extra=%u | Tile=(%d,%d) Deck=%d | Packed=0x%08X\n",
        cargoTypeId, extraData, localX, localZ, deck, packedCoord);

    unsigned int res = SpawnPortable(&packedCoord, cargoTypeId, 0, pPlayer, extraData, flag);
    LogMsg("[EXEC-CRATE] SpawnPortable returned: %u\n", res);
}

// ==============================================================================
// Exact Intrusive Linked-List Entity Counter (Matches FUN_005ca630 + FUN_005cb510)
// ==============================================================================
int ReadSubTypeFieldFromVTable(BYTE* pEnt)
{
    DWORD* vtable = *(DWORD**)pEnt;
    BYTE* fn = (BYTE*)vtable[0x1C / 4];

    if (fn[0] == 0x8B && fn[1] == 0x81) {
        int disp = *(int*)(fn + 2);
        return *(int*)(pEnt + disp);
    }
    if (fn[0] == 0x8B && fn[1] == 0x41) {
        char disp = *(char*)(fn + 2);
        return *(int*)(pEnt + disp);
    }
    if (fn[0] == 0x0F && fn[1] == 0xB6 && fn[2] == 0x81) {
        int disp = *(int*)(fn + 3);
        return *(BYTE*)(pEnt + disp);
    }
    if (fn[0] == 0x0F && fn[1] == 0xB6 && fn[2] == 0x41) {
        char disp = *(char*)(fn + 3);
        return *(BYTE*)(pEnt + disp);
    }
    if (fn[0] == 0x0F && fn[1] == 0xB7 && fn[2] == 0x81) {
        int disp = *(int*)(fn + 3);
        return *(USHORT*)(pEnt + disp);
    }
    if (fn[0] == 0x0F && fn[1] == 0xB7 && fn[2] == 0x41) {
        char disp = *(char*)(fn + 3);
        return *(USHORT*)(pEnt + disp);
    }

    return *(BYTE*)(pEnt + 0xDD);
}

void ScanEntityCountsPureMemory(void* pPlayer, int* liveResidentCounts, int* liveNonHireableCounts, int* liveCrateCounts)
{
    DWORD* entitySlots = (DWORD*)(g_baseAddress + OFFSET_ENTITY_TABLE);

    __try {
        // 1. Walk pPlayer's exact Peep/Droid linked list (entClass = 1)
        USHORT slotIdx = *(USHORT*)((BYTE*)pPlayer + (1 + 0x22D) * 10);
        int safetyGuard = 0;

        while (slotIdx != 0 && safetyGuard++ < 65535) {
            BYTE* pEnt = (BYTE*)entitySlots[slotIdx * 2];
            if (pEnt == nullptr || (uintptr_t)pEnt < 0x10000) break;

            USHORT nextSlot = *(USHORT*)(pEnt + 0x20);

            BYTE stateFlags = *(BYTE*)(pEnt + 0x3E);
            if ((stateFlags & 0x18) == 0) {
                int speciesId = ReadSubTypeFieldFromVTable(pEnt);
                if (speciesId >= 1 && speciesId <= 23) {
                    liveNonHireableCounts[speciesId]++;

                    BYTE visitorFlag = *(BYTE*)(pEnt + 0x30D);
                    bool isResident = (visitorFlag == 0) || (speciesId >= 1 && speciesId <= 4);
                    if (isResident) {
                        liveResidentCounts[speciesId]++;
                    }
                }
            }

            slotIdx = nextSlot;
        }

        // 2. Walk pPlayer's exact Crate/Portable linked list (entClass = 2)
        slotIdx = *(USHORT*)((BYTE*)pPlayer + (2 + 0x22D) * 10);
        safetyGuard = 0;

        while (slotIdx != 0 && safetyGuard++ < 65535) {
            BYTE* pEnt = (BYTE*)entitySlots[slotIdx * 2];
            if (pEnt == nullptr || (uintptr_t)pEnt < 0x10000) break;

            USHORT nextSlot = *(USHORT*)(pEnt + 0x20);

            BYTE stateFlags = *(BYTE*)(pEnt + 0x3E);
            if ((stateFlags & 0x18) == 0) {
                int cargoId = ReadSubTypeFieldFromVTable(pEnt);
                if (cargoId >= 1 && cargoId <= 17) {
                    liveCrateCounts[cargoId]++;
                }
            }

            slotIdx = nextSlot;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silently skip tick if linked list updates mid-frame
    }
}

void EvaluateAutoMaintainRules(void* pPlayer)
{
    bool anyRuleActive = false;
    {
        std::lock_guard<std::mutex> lock(g_maintainMutex);
        for (int i = 1; i <= 22; i++) {
            if (g_peepMaintainRules[i].enabled && g_peepMaintainRules[i].targetCount > 0) {
                anyRuleActive = true;
                break;
            }
        }
        if (!anyRuleActive) {
            for (int i = 1; i <= 17; i++) {
                if (g_crateMaintainRules[i].enabled && g_crateMaintainRules[i].targetCount > 0) {
                    anyRuleActive = true;
                    break;
                }
            }
        }
    }
    if (!anyRuleActive) return;

    {
        std::lock_guard<std::mutex> lock(g_cmdMutex);
        if (!g_cmdQueue.empty()) return;
    }

    int liveResidentCounts[24]{};
    int liveNonHireableCounts[24]{};
    int liveCrateCounts[18]{};

    ScanEntityCountsPureMemory(pPlayer, liveResidentCounts, liveNonHireableCounts, liveCrateCounts);

    std::lock_guard<std::mutex> lock(g_maintainMutex);

    for (int id = 1; id <= 22; id++) {
        const auto& rule = g_peepMaintainRules[id];
        if (!rule.enabled || rule.targetCount <= 0) continue;

        int currentCount = rule.resident ? liveResidentCounts[id] : liveNonHireableCounts[id];
        if (currentCount < rule.targetCount) {
            int deficit = rule.targetCount - currentCount;
            if (deficit > 10) deficit = 10; // Paced in waves of 10 per 2.5s tick
            LogMsg("[AUTO-PEEP] ID %d %s count=%d < target=%d -> Spawning %d replacement(s)\n",
                id, rule.resident ? "RESIDENT" : "NON-HIREABLE", currentCount, rule.targetCount, deficit);

            for (int i = 0; i < deficit; i++) {
                int sx = (rule.localX + (i % 5)) & 0x0F;
                int sy = (rule.localY + (i / 5)) & 0x1F;
                EnqueueCommand({
                    CMD_SPAWN_PEEP, 0, (unsigned int)id, 0,
                    sx, sy, rule.deck,
                    rule.resident, rule.skill, rule.dedication, rule.loyalty
                    });
            }
        }
    }

    for (int id = 1; id <= 17; id++) {
        const auto& rule = g_crateMaintainRules[id];
        if (rule.enabled && rule.targetCount > 0 && liveCrateCounts[id] < rule.targetCount) {
            int deficit = rule.targetCount - liveCrateCounts[id];
            if (deficit > 10) deficit = 10; // Paced in waves of 10 per 2.5s tick
            LogMsg("[AUTO-CRATE] CargoID %d count=%d < target=%d -> Spawning %d replacement(s)\n",
                id, liveCrateCounts[id], rule.targetCount, deficit);

            for (int i = 0; i < deficit; i++) {
                int sx = (rule.localX + (i % 5)) & 0x0F;
                int sy = (rule.localY + (i / 5)) & 0x1F;
                EnqueueCommand({
                    CMD_SPAWN_CRATE, 0, (unsigned int)id, 0,
                    sx, sy, rule.deck,
                    false, 0, 0, 0
                    });
            }
        }
    }
}

// ==============================================================================
// Subclassed Game Main-Thread WndProc + Timers
// ==============================================================================
#define GAME_QUEUE_TIMER_ID    0x5A70
#define GAME_MAINTAIN_TIMER_ID 0x5A71

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
    if (uMsg == WM_TIMER) {
        if (wParam == GAME_QUEUE_TIMER_ID) {
            ProcessCommandQueueOnMainThread();
        }
        else if (wParam == GAME_MAINTAIN_TIMER_ID) {
            void* pPlayer = *(void**)(g_baseAddress + OFFSET_PLAYER_PTR);
            if (pPlayer != nullptr) {
                EvaluateAutoMaintainRules(pPlayer);
            }
        }
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

    // Tab 0: Energy
    IDC_BTN_NRG_ADD_50K = 1001,
    IDC_BTN_NRG_ADD_500K,
    IDC_BTN_NRG_SUB_50K,
    IDC_EDIT_NRG_CUSTOM,
    IDC_BTN_NRG_APPLY_CUSTOM,
    IDC_LBL_NRG_1,
    IDC_LBL_NRG_2,

    // Tab 1: Peeps
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
    IDC_CHK_PEEP_MAINTAIN,
    IDC_LBL_PEEP_TARGET,
    IDC_EDIT_PEEP_TARGET,
    IDC_BTN_PEEP_SUB_10,
    IDC_BTN_PEEP_ADD_10,
    IDC_BTN_PEEP_CLEAR_MAINTAIN,

    // Tab 2: Crates
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
    IDC_CHK_CRATE_MAINTAIN,
    IDC_LBL_CRATE_TARGET,
    IDC_EDIT_CRATE_TARGET,
    IDC_BTN_CRATE_SUB_10,
    IDC_BTN_CRATE_ADD_10,
    IDC_BTN_CRATE_CLEAR_MAINTAIN,

    // Tab 3: Build Changers
    IDC_LBL_BUILD_TITLE = 4001,
    IDC_CHK_BUILD_INSTANT,
    IDC_CHK_BUILD_UNLOCK_ALL,
    IDC_LBL_BUILD_NOTE,

    // Tab 4: Help
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

    ShowRange(1001, 1020, tabIndex == 0); // Energy
    ShowRange(2001, 2035, tabIndex == 1); // Peeps
    ShowRange(3001, 3035, tabIndex == 2); // Crates
    ShowRange(4001, 4020, tabIndex == 3); // Build Changers
    ShowRange(5001, 5020, tabIndex == 4); // Help
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

    std::lock_guard<std::mutex> lock(g_maintainMutex);
    const auto& rule = g_peepMaintainRules[entry.id];
    SendMessageA(GetDlgItem(g_menuHwnd, IDC_CHK_PEEP_MAINTAIN), BM_SETCHECK, rule.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    char buf[16];
    sprintf_s(buf, "%d", rule.targetCount);
    SetWindowTextA(GetDlgItem(g_menuHwnd, IDC_EDIT_PEEP_TARGET), buf);
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
        for (unsigned int t = 1; t <= 45; t++) {
            char buf[64];
            sprintf_s(buf, "Tech Theory ID %02u", t);
            int itemIdx = (int)SendMessageA(hComboSub, CB_ADDSTRING, 0, (LPARAM)buf);
            SendMessageA(hComboSub, CB_SETITEMDATA, itemIdx, (LPARAM)t);
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

    HWND hChkMaint = GetDlgItem(g_menuHwnd, IDC_CHK_CRATE_MAINTAIN);
    HWND hEditTarget = GetDlgItem(g_menuHwnd, IDC_EDIT_CRATE_TARGET);
    HWND hBtnSub10 = GetDlgItem(g_menuHwnd, IDC_BTN_CRATE_SUB_10);
    HWND hBtnAdd10 = GetDlgItem(g_menuHwnd, IDC_BTN_CRATE_ADD_10);

    EnableWindow(hChkMaint, entry.canMaintain ? TRUE : FALSE);
    EnableWindow(hEditTarget, entry.canMaintain ? TRUE : FALSE);
    EnableWindow(hBtnSub10, entry.canMaintain ? TRUE : FALSE);
    EnableWindow(hBtnAdd10, entry.canMaintain ? TRUE : FALSE);

    std::lock_guard<std::mutex> lock(g_maintainMutex);
    const auto& rule = g_crateMaintainRules[entry.id];
    SendMessageA(hChkMaint, BM_SETCHECK, (entry.canMaintain && rule.enabled) ? BST_CHECKED : BST_UNCHECKED, 0);
    char buf[16];
    sprintf_s(buf, "%d", entry.canMaintain ? rule.targetCount : 0);
    SetWindowTextA(hEditTarget, buf);
}

void UpdatePeepMaintainRuleStep(HWND hwnd, int deltaStep, bool toggleOnly)
{
    int idx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_PEEP_TYPE), CB_GETCURSEL, 0, 0);
    int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_PEEP_DECK), CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= PEEP_CATALOG_COUNT) return;

    char bufX[16]{}, bufY[16]{}, bufSkill[16]{}, bufDed[16]{}, bufLoy[16]{};
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_X), bufX, sizeof(bufX));
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_Y), bufY, sizeof(bufY));
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_SKILL), bufSkill, sizeof(bufSkill));
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_DEDICATION), bufDed, sizeof(bufDed));
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_LOYALTY), bufLoy, sizeof(bufLoy));

    const auto& entry = g_peepCatalog[idx];
    bool maintainAsResident = entry.canBeResident;

    std::lock_guard<std::mutex> lock(g_maintainMutex);
    auto& rule = g_peepMaintainRules[entry.id];

    int newTarget = ClampInt(rule.targetCount + deltaStep, 0, 999);
    bool checked = (SendMessageA(GetDlgItem(hwnd, IDC_CHK_PEEP_MAINTAIN), BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (!toggleOnly && deltaStep > 0) {
        checked = true;
        SendMessageA(GetDlgItem(hwnd, IDC_CHK_PEEP_MAINTAIN), BM_SETCHECK, BST_CHECKED, 0);
    }
    if (newTarget == 0) {
        checked = false;
        SendMessageA(GetDlgItem(hwnd, IDC_CHK_PEEP_MAINTAIN), BM_SETCHECK, BST_UNCHECKED, 0);
    }

    rule = {
        checked && (newTarget > 0),
        newTarget,
        ClampInt(atoi(bufX), 0, 15),
        ClampInt(atoi(bufY), 0, 31),
        deck,
        maintainAsResident,
        (unsigned char)ClampInt(atoi(bufSkill), 0, 5),
        (unsigned char)ClampInt(atoi(bufDed), 0, 5),
        (unsigned char)ClampInt(atoi(bufLoy), 0, 5)
    };

    char buf[16];
    sprintf_s(buf, "%d", newTarget);
    SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_TARGET), buf);

    LogMsg("[RULE-PEEP] %s -> Maintain=%s | Target=%d | Mode=%s\n",
        entry.label, rule.enabled ? "ON" : "OFF", newTarget,
        maintainAsResident ? "RESIDENT ONLY" : "NON-HIREABLE");
}

void UpdateCrateMaintainRuleStep(HWND hwnd, int deltaStep, bool toggleOnly)
{
    int idx = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_TYPE), CB_GETCURSEL, 0, 0);
    int deck = (int)SendMessageA(GetDlgItem(hwnd, IDC_COMBO_CRATE_DECK), CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= CRATE_CATALOG_COUNT || !g_crateCatalog[idx].canMaintain) return;

    char bufX[16]{}, bufY[16]{};
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_X), bufX, sizeof(bufX));
    GetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_Y), bufY, sizeof(bufY));

    const auto& entry = g_crateCatalog[idx];

    std::lock_guard<std::mutex> lock(g_maintainMutex);
    auto& rule = g_crateMaintainRules[entry.id];

    int newTarget = ClampInt(rule.targetCount + deltaStep, 0, 999);
    bool checked = (SendMessageA(GetDlgItem(hwnd, IDC_CHK_CRATE_MAINTAIN), BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (!toggleOnly && deltaStep > 0) {
        checked = true;
        SendMessageA(GetDlgItem(hwnd, IDC_CHK_CRATE_MAINTAIN), BM_SETCHECK, BST_CHECKED, 0);
    }
    if (newTarget == 0) {
        checked = false;
        SendMessageA(GetDlgItem(hwnd, IDC_CHK_CRATE_MAINTAIN), BM_SETCHECK, BST_UNCHECKED, 0);
    }

    rule = {
        checked && (newTarget > 0),
        newTarget,
        ClampInt(atoi(bufX), 0, 15),
        ClampInt(atoi(bufY), 0, 31),
        deck
    };

    char buf[16];
    sprintf_s(buf, "%d", newTarget);
    SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_TARGET), buf);

    LogMsg("[RULE-CRATE] %s -> Maintain=%s | Target=%d\n",
        entry.label, rule.enabled ? "ON" : "OFF", newTarget);
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
            8, 8, 455, 420, hwnd, (HMENU)IDC_MAIN_TAB, nullptr, nullptr);

        const char* tabNames[] = {
            "Energy",
            "Peeps",
            "Crates",
            "Build Changers",
            "Help"
        };
        for (int i = 0; i < 5; i++) {
            TCITEMA tie{};
            tie.mask = TCIF_TEXT;
            tie.pszText = (LPSTR)tabNames[i];
            TabCtrl_InsertItem(g_tabHwnd, i, &tie);
        }

        // --- TAB 0: Energy ---
        CreateWindowA("STATIC", "Direct Wallet Energy Controls (No Script Needed):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 48, 420, 20, hwnd, (HMENU)IDC_LBL_NRG_1, nullptr, nullptr);
        CreateWindowA("BUTTON", "+50,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 76, 132, 32, hwnd, (HMENU)IDC_BTN_NRG_ADD_50K, nullptr, nullptr);
        CreateWindowA("BUTTON", "+500,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            164, 76, 140, 32, hwnd, (HMENU)IDC_BTN_NRG_ADD_500K, nullptr, nullptr);
        CreateWindowA("BUTTON", "-50,000 Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            314, 76, 132, 32, hwnd, (HMENU)IDC_BTN_NRG_SUB_50K, nullptr, nullptr);

        CreateWindowA("STATIC", "Custom Energy Amount (Positive or Negative):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 125, 420, 20, hwnd, (HMENU)IDC_LBL_NRG_2, nullptr, nullptr);
        CreateWindowA("EDIT", "100000", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_AUTOHSCROLL,
            22, 150, 215, 26, hwnd, (HMENU)IDC_EDIT_NRG_CUSTOM, nullptr, nullptr);
        CreateWindowA("BUTTON", "Apply Energy", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            247, 148, 199, 30, hwnd, (HMENU)IDC_BTN_NRG_APPLY_CUSTOM, nullptr, nullptr);

        // --- TAB 1: Peeps ---
        CreateWindowA("STATIC", "Peep / Droid Species:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 46, 425, 18, hwnd, (HMENU)IDC_LBL_PEEP_TYPE, nullptr, nullptr);
        HWND hComboPeep = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 66, 425, 280, hwnd, (HMENU)IDC_COMBO_PEEP_TYPE, nullptr, nullptr);
        for (int i = 0; i < PEEP_CATALOG_COUNT; i++) {
            SendMessageA(hComboPeep, CB_ADDSTRING, 0, (LPARAM)g_peepCatalog[i].label);
        }
        SendMessageA(hComboPeep, CB_SETCURSEL, 9, 0); // Default to Salt Hog

        CreateWindowA("STATIC", "Target Deck:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 98, 425, 18, hwnd, (HMENU)IDC_LBL_PEEP_DECK, nullptr, nullptr);
        HWND hComboPeepDeck = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 118, 425, 120, hwnd, (HMENU)IDC_COMBO_PEEP_DECK, nullptr, nullptr);
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 0 - Sub Deck (Engineering / Cargo)");
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 1 - Leisure Deck");
        SendMessageA(hComboPeepDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 2 - Bio Deck");
        SendMessageA(hComboPeepDeck, CB_SETCURSEL, 0, 0);

        CreateWindowA("STATIC", "Tile X:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 152, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_X, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            72, 149, 55, 24, hwnd, (HMENU)IDC_EDIT_PEEP_X, nullptr, nullptr);

        CreateWindowA("STATIC", "Tile Y:", WS_CHILD | WS_CLIPSIBLINGS,
            142, 152, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_Y, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            192, 149, 55, 24, hwnd, (HMENU)IDC_EDIT_PEEP_Y, nullptr, nullptr);

        CreateWindowA("STATIC", "Qty (1-50):", WS_CHILD | WS_CLIPSIBLINGS,
            262, 152, 72, 20, hwnd, (HMENU)IDC_LBL_PEEP_QTY, nullptr, nullptr);
        CreateWindowA("EDIT", "1", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            338, 149, 57, 24, hwnd, (HMENU)IDC_EDIT_PEEP_QTY, nullptr, nullptr);

        HWND hChkRes = CreateWindowA("BUTTON", "Resident (Hired Staff)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 184, 210, 22, hwnd, (HMENU)IDC_CHK_PEEP_RESIDENT, nullptr, nullptr);
        SendMessageA(hChkRes, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowA("STATIC", "Skill (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 214, 72, 20, hwnd, (HMENU)IDC_LBL_PEEP_SKILL, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            96, 211, 42, 24, hwnd, (HMENU)IDC_EDIT_PEEP_SKILL, nullptr, nullptr);

        CreateWindowA("STATIC", "Dedication (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            150, 214, 108, 20, hwnd, (HMENU)IDC_LBL_PEEP_DEDICATION, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            260, 211, 42, 24, hwnd, (HMENU)IDC_EDIT_PEEP_DEDICATION, nullptr, nullptr);

        CreateWindowA("STATIC", "Loyalty (0-5):", WS_CHILD | WS_CLIPSIBLINGS,
            314, 214, 82, 20, hwnd, (HMENU)IDC_LBL_PEEP_LOYALTY, nullptr, nullptr);
        CreateWindowA("EDIT", "5", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            396, 211, 51, 24, hwnd, (HMENU)IDC_EDIT_PEEP_LOYALTY, nullptr, nullptr);

        CreateWindowA("BUTTON", "Spawn Peep(s) Now (Paced Grid)", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 246, 425, 32, hwnd, (HMENU)IDC_BTN_PEEP_SPAWN, nullptr, nullptr);

        // Stepped (+10 / -10) Auto-Maintain Controls for Peeps (0..999)
        CreateWindowA("BUTTON", "Maintain Target", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 292, 128, 22, hwnd, (HMENU)IDC_CHK_PEEP_MAINTAIN, nullptr, nullptr);
        CreateWindowA("STATIC", "Target:", WS_CHILD | WS_CLIPSIBLINGS,
            156, 294, 48, 20, hwnd, (HMENU)IDC_LBL_PEEP_TARGET, nullptr, nullptr);
        CreateWindowA("EDIT", "0", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_READONLY | ES_CENTER,
            206, 291, 48, 24, hwnd, (HMENU)IDC_EDIT_PEEP_TARGET, nullptr, nullptr);
        CreateWindowA("BUTTON", "-10", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            262, 290, 88, 26, hwnd, (HMENU)IDC_BTN_PEEP_SUB_10, nullptr, nullptr);
        CreateWindowA("BUTTON", "+10", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            358, 290, 89, 26, hwnd, (HMENU)IDC_BTN_PEEP_ADD_10, nullptr, nullptr);

        CreateWindowA("BUTTON", "Clear All Peep Auto-Maintain Rules", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 326, 425, 28, hwnd, (HMENU)IDC_BTN_PEEP_CLEAR_MAINTAIN, nullptr, nullptr);

        // --- TAB 2: Crates ---
        CreateWindowA("STATIC", "Crate / Portable Type (Alphabetical):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 46, 425, 18, hwnd, (HMENU)IDC_LBL_CRATE_TYPE, nullptr, nullptr);
        HWND hComboCrate = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 66, 425, 280, hwnd, (HMENU)IDC_COMBO_CRATE_TYPE, nullptr, nullptr);
        for (int i = 0; i < CRATE_CATALOG_COUNT; i++) {
            SendMessageA(hComboCrate, CB_ADDSTRING, 0, (LPARAM)g_crateCatalog[i].label);
        }
        SendMessageA(hComboCrate, CB_SETCURSEL, 5, 0); // Default to Food Crate

        CreateWindowA("STATIC", "Crate Sub-Type (Hardplan, Furniture, Droid, Tech):", WS_CHILD | WS_CLIPSIBLINGS,
            22, 98, 425, 18, hwnd, (HMENU)IDC_LBL_CRATE_SUBTYPE, nullptr, nullptr);
        CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 118, 425, 280, hwnd, (HMENU)IDC_COMBO_CRATE_SUBTYPE, nullptr, nullptr);

        CreateWindowA("STATIC", "Target Deck:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 150, 425, 18, hwnd, (HMENU)IDC_LBL_CRATE_DECK, nullptr, nullptr);
        HWND hComboCrateDeck = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST,
            22, 170, 425, 120, hwnd, (HMENU)IDC_COMBO_CRATE_DECK, nullptr, nullptr);
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 0 - Sub Deck (Engineering / Cargo)");
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 1 - Leisure Deck");
        SendMessageA(hComboCrateDeck, CB_ADDSTRING, 0, (LPARAM)"Deck 2 - Bio Deck");
        SendMessageA(hComboCrateDeck, CB_SETCURSEL, 0, 0);

        CreateWindowA("STATIC", "Tile X:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 206, 48, 20, hwnd, (HMENU)IDC_LBL_CRATE_X, nullptr, nullptr);
        CreateWindowA("EDIT", "8", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            72, 203, 55, 24, hwnd, (HMENU)IDC_EDIT_CRATE_X, nullptr, nullptr);

        CreateWindowA("STATIC", "Tile Y:", WS_CHILD | WS_CLIPSIBLINGS,
            142, 206, 48, 20, hwnd, (HMENU)IDC_LBL_CRATE_Y, nullptr, nullptr);
        CreateWindowA("EDIT", "6", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            192, 203, 55, 24, hwnd, (HMENU)IDC_EDIT_CRATE_Y, nullptr, nullptr);

        CreateWindowA("STATIC", "Qty (1-50):", WS_CHILD | WS_CLIPSIBLINGS,
            262, 206, 72, 20, hwnd, (HMENU)IDC_LBL_CRATE_QTY, nullptr, nullptr);
        CreateWindowA("EDIT", "1", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_NUMBER,
            338, 203, 57, 24, hwnd, (HMENU)IDC_EDIT_CRATE_QTY, nullptr, nullptr);

        CreateWindowA("BUTTON", "Spawn Crate(s) Now", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 238, 205, 32, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN, nullptr, nullptr);
        CreateWindowA("BUTTON", "Spawn All 17 Types", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            237, 238, 210, 32, hwnd, (HMENU)IDC_BTN_CRATE_SPAWN_ALL_STD, nullptr, nullptr);

        // Stepped (+10 / -10) Auto-Maintain Controls for Crates (0..999)
        CreateWindowA("BUTTON", "Maintain Target", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 284, 128, 22, hwnd, (HMENU)IDC_CHK_CRATE_MAINTAIN, nullptr, nullptr);
        CreateWindowA("STATIC", "Target:", WS_CHILD | WS_CLIPSIBLINGS,
            156, 286, 48, 20, hwnd, (HMENU)IDC_LBL_CRATE_TARGET, nullptr, nullptr);
        CreateWindowA("EDIT", "0", WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | ES_READONLY | ES_CENTER,
            206, 283, 48, 24, hwnd, (HMENU)IDC_EDIT_CRATE_TARGET, nullptr, nullptr);
        CreateWindowA("BUTTON", "-10", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            262, 282, 88, 26, hwnd, (HMENU)IDC_BTN_CRATE_SUB_10, nullptr, nullptr);
        CreateWindowA("BUTTON", "+10", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            358, 282, 89, 26, hwnd, (HMENU)IDC_BTN_CRATE_ADD_10, nullptr, nullptr);

        CreateWindowA("BUTTON", "Clear All Crate Auto-Maintain Rules", WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
            22, 318, 425, 28, hwnd, (HMENU)IDC_BTN_CRATE_CLEAR_MAINTAIN, nullptr, nullptr);

        // --- TAB 3: Build Changers ---
        CreateWindowA("STATIC", "Station Build & Segment Modifiers:", WS_CHILD | WS_CLIPSIBLINGS,
            22, 48, 425, 20, hwnd, (HMENU)IDC_LBL_BUILD_TITLE, nullptr, nullptr);
        CreateWindowA("BUTTON", "Instant Room & Furniture Construction (WIP)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 80, 425, 24, hwnd, (HMENU)IDC_CHK_BUILD_INSTANT, nullptr, nullptr);
        CreateWindowA("BUTTON", "Unlock All Segment Bulkheads (WIP)", WS_CHILD | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
            22, 112, 425, 24, hwnd, (HMENU)IDC_CHK_BUILD_UNLOCK_ALL, nullptr, nullptr);
        CreateWindowA("STATIC",
            "Ready to wire directly to Startopia's build timer & segment\n"
            "ownership flags once offsets are confirmed.",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 152, 425, 45, hwnd, (HMENU)IDC_LBL_BUILD_NOTE, nullptr, nullptr);

        // --- TAB 4: Help ---
        CreateWindowA("STATIC",
            "STARTOPIA STANDALONE DLL CHEAT MENU\n\n"
            "• [INSERT] : Show / Hide this Menu Overlay\n"
            "• Deck 0 = Sub Deck | Deck 1 = Leisure | Deck 2 = Bio\n"
            "• Auto-Maintain Rules:\n"
            "   - Use '+10' and '-10' to step your target up to 999 safely\n"
            "     so the engine processes spawns in clean 10-entity waves.\n"
            "   - Peep Auto-Maintain ALWAYS spawns Residents for hireable\n"
            "     species (and non-residents only for droids/creatures).",
            WS_CHILD | WS_CLIPSIBLINGS, 22, 48, 425, 310, hwnd, (HMENU)IDC_LBL_HELP_TEXT, nullptr, nullptr);

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

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // --- Energy Tab Actions ---
        if (id == IDC_BTN_NRG_ADD_50K && code == BN_CLICKED) {
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

        // --- Peep Tab Actions ---
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
        else if (id == IDC_CHK_PEEP_MAINTAIN && code == BN_CLICKED) {
            UpdatePeepMaintainRuleStep(hwnd, 0, true);
        }
        else if (id == IDC_BTN_PEEP_SUB_10 && code == BN_CLICKED) {
            UpdatePeepMaintainRuleStep(hwnd, -10, false);
        }
        else if (id == IDC_BTN_PEEP_ADD_10 && code == BN_CLICKED) {
            UpdatePeepMaintainRuleStep(hwnd, 10, false);
        }
        else if (id == IDC_BTN_PEEP_CLEAR_MAINTAIN && code == BN_CLICKED) {
            std::lock_guard<std::mutex> lock(g_maintainMutex);
            memset(g_peepMaintainRules, 0, sizeof(g_peepMaintainRules));
            SendMessageA(GetDlgItem(hwnd, IDC_CHK_PEEP_MAINTAIN), BM_SETCHECK, BST_UNCHECKED, 0);
            SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_PEEP_TARGET), "0");
            LogMsg("[RULE-PEEP] Cleared all Peep auto-maintain rules.\n");
        }

        // --- Crate Tab Actions ---
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
                unsigned int defExtra = (entry.subTypeMode != 0) ? 1 : 0;
                int gx = (baseX + (i % 6)) & 0x0F;
                int gz = (baseY + (i / 6)) & 0x1F;
                EnqueueCommand({ CMD_SPAWN_CRATE, 0, entry.id, defExtra, gx, gz, deck, false, 0, 0, 0 });
            }
        }
        else if (id == IDC_CHK_CRATE_MAINTAIN && code == BN_CLICKED) {
            UpdateCrateMaintainRuleStep(hwnd, 0, true);
        }
        else if (id == IDC_BTN_CRATE_SUB_10 && code == BN_CLICKED) {
            UpdateCrateMaintainRuleStep(hwnd, -10, false);
        }
        else if (id == IDC_BTN_CRATE_ADD_10 && code == BN_CLICKED) {
            UpdateCrateMaintainRuleStep(hwnd, 10, false);
        }
        else if (id == IDC_BTN_CRATE_CLEAR_MAINTAIN && code == BN_CLICKED) {
            std::lock_guard<std::mutex> lock(g_maintainMutex);
            memset(g_crateMaintainRules, 0, sizeof(g_crateMaintainRules));
            SendMessageA(GetDlgItem(hwnd, IDC_CHK_CRATE_MAINTAIN), BM_SETCHECK, BST_UNCHECKED, 0);
            SetWindowTextA(GetDlgItem(hwnd, IDC_EDIT_CRATE_TARGET), "0");
            LogMsg("[RULE-CRATE] Cleared all Crate auto-maintain rules.\n");
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
        35, 35, 488, 475,
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
        SetTimer(g_gameHwnd, GAME_QUEUE_TIMER_ID, 100, nullptr);
        SetTimer(g_gameHwnd, GAME_MAINTAIN_TIMER_ID, 2500, nullptr);
        LogMsg("Attached paced queue (100ms) & auto-maintain watcher (2500ms) to HWND: 0x%p\n", g_gameHwnd);
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