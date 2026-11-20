# Startopia Cheeseburger Menu

**Current Version:** 1.1.0
**Build Date:** 11/19/2026

An in-game cheat and sandbox manipulation overlay for **Startopia**. Rather than relying on static mission text scripts (`mishXX.txt`), this mod uses a `version.dll` proxy and **MinHook** to execute internal engine functions directly on the main game thread via a **Dear ImGui** overlay (with a Classic Win32 GDI fallback).

The menu is designed as external, ImGui does not have native support for DirectX7, you will need to run the beta builds with /r or use DXWnd with the stable release to force windowed mode. You can force Borderless with third-party tools. Currently seeking reliable alternatives.

## Supported Versions (Crossed out means still in development)

~~1.0.0 - 1.0.1b (SafeDisc)~~
~~1.01 (GOG)~~
~~1.01 (Steam)~~
1.01.1-b3 (beta)
1.01.1-b3 (beta_debug)

## Dedicated to Chris Nelson

A good friend who passed the night this mod was finished.

## Origin of the Name

The name **"Cheeseburger Menu"** is a direct nod to the famous debug/cheat menu in *Deus Ex: Human Revolution*—originally uncovered by **Gibbed** (Rick)—which was affectionately dubbed the **"Hamburger Menu"**. In keeping with that tradition, this project earned the title **Cheeseburger Menu**.

## Features

### Energy & Technology (`Energy/Tech`)
* **Direct Wallet Energy Controls:** Instantly add (`+50,000`, `+500,000`), subtract (`-50,000`), or apply any custom positive/negative energy amount directly to the player's balance.
* **Unlock All Tech:** Executes `@alltech` (`0xFFFFFFFF`) to immediately discover all theories and blueprints across the tech tree. *(Includes a disclaimer to avoid using it during the Turakken research mission unless you want an instant win.)*

### Entity Spawning (`Peeps` & `Crates`)
* **Peep & Droid Spawner:** Spawn any loaded alien species, Maintenance Scutter (Mk I–III), Security Scutter, or special entity onto Deck 0 (Sub Deck), Deck 1 (Leisure Deck), or Deck 2 (Bio Deck).
  * Configurable **Resident (Hired Staff)** toggle (automatically disabled for non-hirable entities) and custom **Skill**, **Dedication**, and **Loyalty** ratings (`0–5`).
* **Crate & Portable Spawner:** Spawn all 17 crate/portable types (alphabetized), with dynamic, alphabetized sub-type dropdowns for:
  * **Technology Crates:** All 95 researchable tech theories mapped to their exact internal engine IDs.
  * **Hardplan Crates:** All 40 spawnable room flat-packs.
  * **Furniture Crates:** All 43 furniture plans.
  * **Droid Crates:** All 19 peep/scutter types.
  * **Spawn All 17 Types:** One-click batch spawn of every crate category.
* **Paced Grid Batch Spawning:** Spawn `1` to `50` entities per click on a 6-column coordinate grid (`Tile X`, `Tile Y`) paced at `100 ms` intervals on the main game thread to prevent engine stalls or swallowed spawns.

### Live Engine Variables (`Build Changers`)
* **Real-Time Memory Indexing:** Automatically scans Startopia's live `BUILD SLONGS` and `BUILD FLOATS` linked lists in memory.
* **Filtering & Live Editing:** Filter by category (*Segments & Station*, *Factory & Cargo*, *Rooms & Furniture*, *Droids & Peeps*, *Combat & AI*, *Economy & Costs*, *Environment & Misc*) or search by variable name, adjust values in real time via sliders or numeric input boxes, and reset individual or filtered variables back to their defaults.

### Interface & Configuration
* **Dual UI Modes:** Uses an external DirectX 11 **Dear ImGui** overlay by default (`UseImGUI=true`), with automatic fallback to a **Classic Win32** tabbed window (`UseImGUI=false`).
* **Always on Top Toggle:** Keep the menu pinned above the game window or let it sit behind when unfocused.
* **Auto-Generated INI (`StartopiaCheeseburger.ini`):** Customize `ToggleConsole`, `UseImGUI`, `Hotkey` (default `F12`), `OutputCrashLog`, and `ShowMenuOnStart`.

## Requirements & Installation

1. **Game Version:** Requires the latest Steam beta build of Startopia (`1.01.1-b3`).
2. **Display Mode:** Run the game in **Windowed** mode (or use a third-party borderless window utility). Exclusive full-screen mode is not supported.
3. **Install:** Place the compiled `version.dll` into your main Startopia installation folder (next to `startopia.exe`).
4. **Launch:** Start the game. `StartopiaCheeseburger.ini` will be generated automatically on first run. Press **F12** (or your configured hotkey) at any time to toggle the menu.

## Known Bugs

* **Occasional Black Screen on Map Load:** When starting a new mission or sandbox game, the screen may occasionally remain black. If this happens, close/restart the game and try loading the mission or sandbox map again.

## Credits & Special Thanks

* **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut — Used for the DirectX 11 overlay interface.
* **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu — Used for x86 API/function hooking (such as the segment unlock refund detour).
* **Mucky Foot Productions** — Special thanks for creating *Startopia* and releasing the official *Startopia Mod Scripting Guide*, which made documenting the game's internal structures possible.
* **Gibbed (Rick)** — Inspiration for the menu's title via the *Deus Ex: Human Revolution* "Hamburger Menu".
