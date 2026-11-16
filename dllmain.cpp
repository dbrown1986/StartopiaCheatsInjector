#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include "MinHook.h"

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
const DWORD OFFSET_WALLET_ADD_ENERGY = 0x001AEC00; // FUN_005aec00
const DWORD OFFSET_SPAWN_PEEP_SP = 0x0022E2B0; // FUN_0062e2b0
const DWORD OFFSET_SPAWN_PEEP_MP = 0x0022E3D0; // FUN_0062e3d0
const DWORD OFFSET_SPAWN_PORTABLE = 0x0018F9D0; // FUN_0058f9d0 (Native Crate/Portable Spawner)
const DWORD OFFSET_GET_PLAYER_SECTOR = 0x0023C680; // FUN_0063c680
const DWORD OFFSET_PACK_COORDS = 0x00061280; // FUN_00461280
const DWORD OFFSET_SPECIES_TABLE = 0x004D9AF8; // DAT_008d9af8
const DWORD OFFSET_FLAG_MP_MODE = 0x007FE1AC; // DAT_00bfe1ac
const DWORD OFFSET_PLAYER_PTR = 0x007F5170; // DAT_00bf5170

DWORD g_baseAddress = 0;
int g_nextPeepIndex = 1;
int g_nextCrateId = 1;
bool g_inCrateHook = false;
FILE* g_logFile = nullptr;

// ==============================================================================
// Dual Console + Immediate-Flush File Logger (mod_crash.log)
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

// ==============================================================================
// Global Vectored Exception Handler
// ==============================================================================
LONG WINAPI GlobalVectoredCrashHandler(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == EXCEPTION_BREAKPOINT) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    DWORD eip = ep->ContextRecord->Eip;
    DWORD ghidraAddr = (g_baseAddress != 0 && eip >= g_baseAddress) ? (eip - g_baseAddress + 0x00400000) : 0;

    LogMsg("\n[FATAL CRASH] =====================================\n");
    LogMsg("Exception Code : 0x%08X\n", code);
    LogMsg("Live EIP       : 0x%08X\n", eip);
    LogMsg("Ghidra Offset  : 0x%08X\n", ghidraAddr);
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        LogMsg("Access Type    : %s memory at 0x%08X\n",
            ep->ExceptionRecord->ExceptionInformation[0] ? "WRITING" : "READING",
            (DWORD)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    LogMsg("EAX: %08X  EBX: %08X  ECX: %08X  EDX: %08X\n",
        ep->ContextRecord->Eax, ep->ContextRecord->Ebx, ep->ContextRecord->Ecx, ep->ContextRecord->Edx);
    LogMsg("ESI: %08X  EDI: %08X  EBP: %08X  ESP: %08X\n",
        ep->ContextRecord->Esi, ep->ContextRecord->Edi, ep->ContextRecord->Ebp, ep->ContextRecord->Esp);
    LogMsg("===================================================\n");

    return EXCEPTION_CONTINUE_SEARCH;
}

// ==============================================================================
// Engine Signatures
// ==============================================================================
typedef void(__thiscall* tWalletAddEnergy)(void* pWallet, int amount);
tWalletAddEnergy pOriginalWalletAddEnergy = nullptr;

// FUN_0058f9d0: uint __fastcall FUN_0058f9d0(uint *param_1, undefined4 param_2, uint param_3, int param_4, uint param_5, int param_6)
typedef unsigned int(__fastcall* tSpawnPortable)(unsigned int* pPackedCoords, unsigned int cargoType, unsigned int angle, void* pPlayer, unsigned int extraData, int flag);
tSpawnPortable pOriginalSpawnPortable = nullptr;

typedef int(__fastcall* tGetPlayerSector)(void* pPlayer, int unusedDeck);
typedef unsigned int* (__thiscall* tPackCoords)(unsigned int* outPacked, char x, unsigned int z, unsigned int deck);
typedef unsigned int(__fastcall* tSpawnPeepSP)(int mode, unsigned int* pPackedCoords, unsigned int peepType, unsigned short angle, void* pPlayer, void* pAttr);
typedef void(__fastcall* tSpawnPeepMP)(int mode, unsigned int* pPackedCoords, int peepType, unsigned int angle, void* pPlayer, int* pAttr);

// ==============================================================================
// Helper: Compute Safe Coordinates Inside Player's Claimed Sector
// ==============================================================================
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

    LogMsg("[DEBUG] Deck=%d | Sector=%d | MinX=%u MaxX=%u | FinalX=%u Z=%d | Packed=0x%08X\n",
        deck, sector, minX, maxX, finalX, localZ, packedCoord);

    return packedCoord;
}

// ==============================================================================
// Peep Spawner (Verified Working on Deck 0)
// ==============================================================================
unsigned int GetNextValidPeepId(unsigned int preferredId)
{
    void** speciesTable = (void**)(g_baseAddress + OFFSET_SPECIES_TABLE);

    if (preferredId >= 1 && preferredId <= 23 && speciesTable[preferredId] != nullptr) {
        return preferredId;
    }

    for (int offset = 0; offset < 23; offset++) {
        int candidate = ((g_nextPeepIndex - 1 + offset) % 23) + 1;
        if (speciesTable[candidate] != nullptr) {
            g_nextPeepIndex = (candidate % 23) + 1;
            return (unsigned int)candidate;
        }
    }
    return 0;
}

void SpawnPeepAt(void* pPlayer, unsigned int preferredPeepId, int localOffsetX, int localZ, int deck)
{
    unsigned int validId = GetNextValidPeepId(preferredPeepId);
    if (validId == 0) return;

    int mpFlag = *(int*)(g_baseAddress + OFFSET_FLAG_MP_MODE);
    unsigned int packedCoord = GetSafePlayerTile(pPlayer, localOffsetX, localZ, deck);

    LogMsg("[MOD] Spawning Peep ID %u at (%d, %d) Deck %d...\n", validId, localOffsetX, localZ, deck);

    if (mpFlag == 0) {
        auto SpawnSP = (tSpawnPeepSP)(g_baseAddress + OFFSET_SPAWN_PEEP_SP);
        SpawnSP(0, &packedCoord, validId, 0, pPlayer, nullptr);
    }
    else {
        auto SpawnMP = (tSpawnPeepMP)(g_baseAddress + OFFSET_SPAWN_PEEP_MP);
        SpawnMP(0, &packedCoord, (int)validId, 0, pPlayer, nullptr);
    }
}

// ==============================================================================
// Crate / Portable Spawner Helper
// ==============================================================================
void SpawnCrateAt(void* pPlayer, unsigned int cargoTypeId, int localOffsetX, int localZ, int deck)
{
    unsigned int packedCoord = GetSafePlayerTile(pPlayer, localOffsetX, localZ, deck);
    int flag = (cargoTypeId == 0xB || cargoTypeId == 0xC) ? 0 : 1;

    LogMsg("[MOD] Calling SpawnCrateAt | CargoID=%u (0x%X) | Flag=%d\n", cargoTypeId, cargoTypeId, flag);
    unsigned int res = pOriginalSpawnPortable(&packedCoord, cargoTypeId, 0, pPlayer, 0, flag);
    LogMsg("[MOD] SpawnPortable returned: %u\n", res);
}

// ==============================================================================
// Hook 1: Intercept Native Crate Spawns (FUN_0058f9d0) When Dine-O-Mat Runs
// ==============================================================================
unsigned int __fastcall hkSpawnPortable(unsigned int* pPackedCoords, unsigned int cargoType, unsigned int angle, void* pPlayer, unsigned int extraData, int flag)
{
    unsigned int packedVal = (pPackedCoords != nullptr) ? *pPackedCoords : 0;
    LogMsg("[HOOK-CRATE] Native placeportable fired! CargoID=%u (0x%X) | Packed=0x%08X | Angle=%u | pPlayer=0x%p | Extra=%u | Flag=%d\n",
        cargoType, cargoType, packedVal, angle, pPlayer, extraData, flag);

    // Let the game spawn its original Dine-O-Mat crate first
    unsigned int res = pOriginalSpawnPortable(pPackedCoords, cargoType, angle, pPlayer, extraData, flag);

    // Prevent recursion, then spawn an extra custom crate right next to it (at 8, 6 on Sub Deck)
    if (!g_inCrateHook && pPlayer != nullptr) {
        g_inCrateHook = true;
        LogMsg("[MOD] Piggybacking custom Crate ID %d on Sub Deck (Deck 0)...\n", g_nextCrateId);
        SpawnCrateAt(pPlayer, g_nextCrateId, 8, 6, 0);
        g_nextCrateId++;
        if (g_nextCrateId > 16) g_nextCrateId = 1;
        g_inCrateHook = false;
    }

    return res;
}

// ==============================================================================
// Hook 2: Wallet AddEnergy (Verified Working Peep Trigger)
// ==============================================================================
void __fastcall hkWalletAddEnergy(void* pWallet, void* edxDummy, int amount)
{
    void* pPlayer = *(void**)(g_baseAddress + OFFSET_PLAYER_PTR);

    if (amount == -500000 && pPlayer != nullptr) {
        SpawnPeepAt(pPlayer, 1, 6, 6, 0);
        LogMsg("[MOD] Peep spawn call completed!\n");
        return;
    }

    pOriginalWalletAddEnergy(pWallet, amount);
}

// ==============================================================================
// Background Initialization Thread (Console stays open permanently)
// ==============================================================================
DWORD WINAPI MainThread(LPVOID lpReserved)
{
    Sleep(2000);

    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    fopen_s(&g_logFile, "mod_crash.log", "w");

    AddVectoredExceptionHandler(1, GlobalVectoredCrashHandler);

    g_baseAddress = (DWORD)GetModuleHandle(NULL);
    DWORD liveWalletAddr = g_baseAddress + OFFSET_WALLET_ADD_ENERGY;
    DWORD livePortableAddr = g_baseAddress + OFFSET_SPAWN_PORTABLE;

    LogMsg("Startopia Base Address : 0x%08X\n", g_baseAddress);
    LogMsg("Hooking AddEnergy at    : 0x%08X\n", liveWalletAddr);
    LogMsg("Hooking SpawnPortable at: 0x%08X\n", livePortableAddr);

    if (MH_Initialize() != MH_OK) {
        LogMsg("MinHook init failed.\n");
        return 1;
    }

    if (MH_CreateHook((LPVOID)liveWalletAddr, &hkWalletAddEnergy, reinterpret_cast<LPVOID*>(&pOriginalWalletAddEnergy)) != MH_OK) {
        LogMsg("Failed to create AddEnergy hook.\n");
        return 1;
    }

    if (MH_CreateHook((LPVOID)livePortableAddr, &hkSpawnPortable, reinterpret_cast<LPVOID*>(&pOriginalSpawnPortable)) != MH_OK) {
        LogMsg("Failed to create SpawnPortable hook.\n");
        return 1;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogMsg("Failed to enable hooks.\n");
        return 1;
    }

    LogMsg("Ready! Build a Dine-O-Mat and accept its prompt to intercept native crate spawns.\n");

    // Keep thread alive permanently with no END key exit
    while (true) {
        Sleep(1000);
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