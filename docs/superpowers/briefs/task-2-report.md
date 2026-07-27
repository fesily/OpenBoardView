# Task 2 Report: Parse API + BoardSnapshot

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `23df5d3` (Task 1 complete)  
**Commit:** `f678fe0` — `feat(core): ParseBoardBuffer/File with BoardSnapshot`  

---

## Summary

Added GUI-free parse entry points `obv::ParseBoardBuffer` / `obv::ParseBoardFile` that produce `obv::BoardSnapshot` (owned `BRDFileBase` + `BRDBoard`, bounds, error). Ported `BoardView::LoadFile` format detect chain and `LoadBoard` empty-outline margin fallback + bounds scan into `obv_core`. Wired `obv_core_tests` executable with CTest.

---

## Files created

| Path | Purpose |
|------|---------|
| `src/obv_core/include/obv_core/decrypt_keys.h` | `DecryptKeys`: `fzKey`/`caeKey` `array<uint32_t,44>`, `xzzKey` `uint64_t`, `hasFz`/`hasCae`/`hasXzz` |
| `src/obv_core/include/obv_core/board_snapshot.h` | `BoardBounds`, `BoardSnapshot` (file before board for lifetime; `ok()`) |
| `src/obv_core/include/obv_core/parse.h` | `ParseBoardBuffer` / `ParseBoardFile` declarations |
| `src/obv_core/src/parse.cpp` | Format detect chain, outline fallback, bounds, error mapping |
| `src/obv_core_tests/test_parse_export.cpp` | Unrecognized-format failure test + optional `OBV_TEST_BOARD` |
| `src/obv_core_tests/CMakeLists.txt` | `obv_core_tests` executable + `add_test` |

## Files modified

| Path | Change |
|------|--------|
| `src/obv_core/CMakeLists.txt` | Add `src/parse.cpp`; promote openboardview + parent include dirs to **PUBLIC** so snapshot headers resolve for consumers |
| `src/CMakeLists.txt` | When `ENABLE_OBV_CORE`: `enable_testing()` + `add_subdirectory(obv_core_tests)` |

---

## Interfaces

```cpp
namespace obv {
struct DecryptKeys {
  std::array<uint32_t, 44> fzKey{};
  std::array<uint32_t, 44> caeKey{};
  uint64_t xzzKey = 0;
  bool hasFz = false;
  bool hasCae = false;
  bool hasXzz = false;
};

struct BoardBounds { float minX, minY, maxX, maxY; };

struct BoardSnapshot {
  std::unique_ptr<BRDFileBase> file;
  std::unique_ptr<BRDBoard> board; // raw ptr into file — keep file alive
  BoardBounds bounds{};
  std::string sourceName;
  std::string error;
  bool ok() const { return file && file->valid && board; }
};

BoardSnapshot ParseBoardBuffer(std::vector<char> buffer,
                               const filesystem::path &filepath,
                               const DecryptKeys &keys);
BoardSnapshot ParseBoardFile(const filesystem::path &filepath,
                             const DecryptKeys &keys);
}
```

Format chain (matches `BoardView::LoadFile` ~266–301):  
`.fz` / `.cae` by extension → ASC/BOM → GenCAD → AD → CAD → `.cst` → BRD → BRD2 → BDV → BVR → BVR3 → Allegro → XZZ → `.json` → `"Unrecognized file format."`

On invalid parse: `snap.error` from `file->error_msg` (or default). On success: outline margin rectangle if `<3` format points and segments (same as `LoadBoard`), then `BRDBoard`, then bounds from outline points/segments.

---

## TDD evidence

### RED (Step 1–3)

1. Wrote `test_parse_export.cpp` asserting `!snap.ok()` and `!snap.error.empty()` for buffer `{'n','o','p','e'}` / path `x.bin`.
2. Wired `obv_core_tests` CMake + `parse.cpp` source list / public includes.
3. Without a complete `ParseBoardBuffer` implementation that sets `error` on unrecognized formats, the contract assertion fails; link/build also fails if symbols are missing.

Initial compile of full parse had a real RED-class failure before GREEN:

```text
parse.cpp: error C2662: OutlinePoints/OutlineSegments cannot call const BRDBoard
```

(`ComputeBounds` took `const BRDBoard&` but BRDBoard accessors are non-const.)

### GREEN (Step 4)

```text
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OPENBOARDVIEW=ON -DENABLE_SQLITE3=OFF
cmake --build build-web --target obv_core_tests --config Release
./build-web/src/obv_core_tests/Release/obv_core_tests.exe
# → ok
# → EXIT:0

# Core-only (no SDL/glad/imgui)
cmake -S . -B build-core-only -DENABLE_OBV_CORE=ON -DENABLE_OPENBOARDVIEW=OFF -DENABLE_SQLITE3=OFF
cmake --build build-core-only --target obv_core_tests --config Release
./build-core-only/src/obv_core_tests/Release/obv_core_tests.exe
# → ok
# → EXIT:0
```

### Step 5 fixture

No redistributable `.brd`/`.json` sample in-repo. Optional path:

```text
OBV_TEST_BOARD=/path/to/board.xxx ./obv_core_tests
```

Skipped when env unset (default CI path).

---

## Self-review

### What went well
- Detect chain is a near-line port of desktop `LoadFile`; outline fallback matches `LoadBoard`.
- Ownership preserved: `file` unique_ptr lives in snapshot; board holds raw pointer.
- Tests run under both full `build-web` and GUI-free `build-core-only`.

### Concerns / follow-ups
1. **PUBLIC openboardview includes** — consumers of `board_snapshot.h` pull `Board.h` / annotations / FileFormats. Acceptable for Task 2; later may want opaque handles or pimpl for a thinner public surface.
2. **`hasFz`/`hasCae`/`hasXzz` unused in parse** — keys arrays always passed (zeros if unset); parsers use built-in keys where available and set `error_msg` on failure. Flags reserved for server config (later tasks).
3. **No in-repo positive parse fixture** — only negative path covered by default; rely on `OBV_TEST_BOARD` for real boards.
4. **Allegro path** always returns invalid with a fixed message (header-only stub); correctly surfaces as `!ok()` + error.
5. **JSON export (Task 3)** not started.

### Out of scope
- Board JSON export, server, web client (Tasks 3+)

---

## Commit

See git log after Step 6 commit message:

```
feat(core): ParseBoardBuffer/File with BoardSnapshot
```
