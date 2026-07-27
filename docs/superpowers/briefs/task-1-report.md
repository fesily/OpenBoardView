# Task 1 Report: Scaffold `obv_core` (no GUI deps)

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `98fb8bc`  
**Commit:** `d514392` — `build: extract obv_core library without GUI deps`  
**Date:** 2026-07-27

---

## Summary

Created static CMake target `obv_core` that compiles board domain + format parsers without linking SDL/ImGui/OpenGL/glad. Replaced `ImVec2` outline fields on `Component` with `obv::Vec2` so `Board.h` no longer includes ImGui. Desktop `openboardview` still builds after conversion helpers in `BoardView.cpp`.

---

## Files created

| Path | Purpose |
|------|---------|
| `src/obv_core/CMakeLists.txt` | STATIC lib: core_utils, Board, BRDBoard, annotations, utils, FileFormats, des.c; GenCAD grammar custom_command; links mpc/zlib/filesystem; optional SQLite |
| `src/obv_core/include/obv_core/vec2.h` | `struct obv::Vec2 { float x,y; }` |
| `src/obv_core/include/obv_core/core_utils.h` | `obv::file_as_buffer` / `obv::check_fileext` (SDL-free API for later parse path) |
| `src/obv_core/src/core_utils.cpp` | Implementation using `fprintf(stderr)` instead of SDL_Log |

## Files modified

| Path | Change |
|------|--------|
| `src/CMakeLists.txt` | `option(ENABLE_OBV_CORE … ON)` + `add_subdirectory(obv_core)` after openboardview (so iguana FetchContent runs first) |
| `src/openboardview/Board.h` | Drop `#include "imgui/imgui.h"`; include `obv_core/vec2.h`; `outline`/`special_outline`/`hull`/`omin`/`omax`/`centerpoint` → `obv::Vec2` |
| `src/openboardview/BoardView.cpp` | `ToObv()` helpers; convert ImVec2 hull/outline assignments into `obv::Vec2` storage |
| `src/openboardview/CMakeLists.txt` | Include `../obv_core/include` for `Board.h`; iguana FetchContent policy floor for CMake 4.x |
| `src/openboardview/utils.h` | Remove `#include <SDL.h>`; `ENSURE*` logs via `fprintf` |
| `src/openboardview/utils.cpp` | Replace `SDL_LogError` with `fprintf` (compiled into both desktop and core) |
| `FileFormats/GenCADFile.cpp` | Drop SDL include; `SDL_LogWarn` → `fprintf` |
| `FileFormats/XZZPCBFile.cpp` | `SDL_LogWarn` → `fprintf` |
| `FileFormats/XJsonFile.cpp` | Drop `SDL_stdinc.h`; local `M_PI` guard |
| `FileFormats/ADFile.cpp`, `BVR3File.cpp` | Local `M_PI` guards (previously satisfied via SDL transitively through `utils.h`) |

---

## CMake / link surface

`obv_core` **does not** link: imgui, SDL2, glad, OpenGL.

`obv_core` **does** link/include: mpc, zlib, filesystem libs, utf8 headers, rapidyaml via `../rapidyaml.hpp`, iguana headers (for XJsonFile), optional SQLite when `ENABLE_SQLITE3`.

GenCAD grammar: same `generate_grammar_header.py` pattern as desktop; output under `obv_core` binary dir `build-generated/GenCADFileGrammar.h`.

Sources shared by path (not object-lib): FileFormats `*.cpp`, `Board.cpp`, `BRDBoard.cpp`, `annotations.cpp`, `utils.cpp`, `Crypto/des.c` plus new `core_utils.cpp`.

---

## Verification

```text
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_SQLITE3=OFF
cmake --build build-web --target obv_core --config Release
# → build-web/src/obv_core/Release/obv_core.lib

cmake --build build-web --target openboardview --config Release
# → build-web/src/openboardview/Release/openboardview.exe
```

Both targets succeeded on Windows / MSVC (VS 2022 generator).

Note: plan mentioned `-DENABLE_OBV_SERVER=OFF`; that option does not exist yet (Task for server later) — configure without it.

---

## Self-review

### What went well
- Domain header is ImGui-free; core library builds as STATIC without GUI deps.
- Desktop kept working with minimal `ToObv` adapters rather than rewriting vectorhulls.
- SDL logging removed from the parse/utils path used by FileFormats.

### Concerns / follow-ups
1. **`utils.cpp` is dual-built** into both `openboardview` and `obv_core` (same source twice). Fine for Task 1; later optional desktop cutover can link `obv_core` and drop duplicate objects.
2. **`annotations.cpp` still includes `platform.h`** for `file_read_text` / `file_write_text` declarations; implementations live in `utils.cpp` on non-Android. Core does not link platform pickers.
3. **`vectorhulls` remains desktop-only** (ImVec2 API). Outline geometry for web will come from a later parse/export path, not VH* yet.
4. **Iguana CMake deprecation**: `CMAKE_POLICY_VERSION_MINIMUM=3.5` workaround for FetchContent; upstream iguana still declares old `cmake_minimum_required`.
5. **`ENABLE_OBV_SERVER`** not present; ignored until server task.

### Out of scope (not done)
- Parse API / BoardSnapshot / JSON export / server / web client (Tasks 2+)

---

## Commit

```
d514392 build: extract obv_core library without GUI deps
```

---

## Fix pass

**Date:** 2026-07-27  
**Commit:** `db52808` — `fix(core): GUI-free cmake path and public filesystem includes`

### Changes

1. **Critical — GUI-free CMake path**
   - Early `option(ENABLE_OBV_CORE)` + `option(ENABLE_OPENBOARDVIEW "Build desktop OpenBoardView application" ON)`.
   - Core deps only before `obv_core`: utf8, zlib, optional SQLite, filesystem detection, mpc.
   - `add_subdirectory(obv_core)` runs before glad/SDL2/imgui/stb/openboardview.
   - Desktop stack (glad, SDL2, imgui, stb, openboardview) gated behind `if(ENABLE_OPENBOARDVIEW)`.
   - Defaults: both ON → full desktop + core (unchanged DX).

2. **Important — public filesystem includes**
   - Added `src/obv_core/include/obv_core/filesystem_impl.h` (public shim, mirrors openboardview).
   - `core_utils.h` includes `obv_core/filesystem_impl.h` (resolves via PUBLIC include dir).
   - `obv_core` propagates `WITH_STD_FILESYSTEM` PUBLIC when not using ghc.

### Verification

```text
# Core-only (no SDL/glad/imgui)
cmake -S . -B build-core-only -DENABLE_OBV_CORE=ON -DENABLE_OPENBOARDVIEW=OFF -DENABLE_SQLITE3=OFF
# → no glad/SDL2/imgui in CMakeCache; targets: mpc, zlib, obv_core only
cmake --build build-core-only --target obv_core --config Release
# → build-core-only/src/obv_core/Release/obv_core.lib  OK

# Full tree still works
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OPENBOARDVIEW=ON -DENABLE_SQLITE3=OFF
cmake --build build-web --target obv_core --config Release
# → build-web/src/obv_core/Release/obv_core.lib  OK
cmake --build build-web --target openboardview --config Release
# → build-web/src/openboardview/Release/openboardview.exe  OK

# Consumer header resolution (public includes only)
cl /std:c++20 /EHsc /Zc:__cplusplus /DWITH_STD_FILESYSTEM /I src/obv_core/include /c build-core-only/consumer_test.cpp
# → consumer_test.obj OK (#include "obv_core/core_utils.h" resolves filesystem_impl.h)
```
