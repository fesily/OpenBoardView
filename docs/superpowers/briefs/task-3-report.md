# Task 3 Report: Board JSON export

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `2c665fe` / `fc6faa6` (Task 2 complete)  
**Commit:** `8c2df8b` — `feat(core): export boardSchemaVersion 1 JSON`

---

## Summary

Implemented `obv::ExportBoardJson` and `obv::ExportMetaJson` for `BoardSnapshot`, producing boardSchemaVersion **1** JSON matching design spec §5.2 field set. Manual JSON writer via `std::ostringstream` (plan allows nlohmann **or** ostringstream; chose ostringstream to avoid vendoring a large single-header dependency while still escaping strings correctly).

---

## Files created

| Path | Purpose |
|------|---------|
| `src/obv_core/include/obv_core/board_json.h` | `ExportBoardJson` / `ExportMetaJson` declarations |
| `src/obv_core/src/board_json.cpp` | Exporter + pin-id rule comment; enum→string maps |

## Files modified

| Path | Change |
|------|--------|
| `src/obv_core/CMakeLists.txt` | Add `src/board_json.cpp` to `OBV_CORE_SOURCES` |
| `src/obv_core_tests/test_parse_export.cpp` | Failed-snap empty export test; optional `OBV_TEST_BOARD` schema/pins assertions; meta structure checks |

---

## Interfaces

```cpp
namespace obv {
// boardSchemaVersion = 1
// Full board document (§5.2). Returns empty string when snap is not ok().
std::string ExportBoardJson(const BoardSnapshot &snap, const std::string &boardId);

// Meta-only: schema version, boardId, sourceName, bounds, sides.
// Returns empty string when snap is not ok().
std::string ExportMetaJson(const BoardSnapshot &snap, const std::string &boardId);
}
```

### Full board fields (§5.2)

`boardSchemaVersion`, `boardId`, `sourceName`, `bounds`, `sides`, `outline` (`points`/`segments`), `nets`, `components`, `pins`, `tracks`, `vias`, `arcs`.

### Meta fields

`boardSchemaVersion`, `boardId`, `sourceName`, `bounds`, `sides` only (no geometry arrays).

### Pin id rule (locked)

Documented at top of `board_json.cpp`:

- `id = componentName + "." + pin.number` when `pin.component` is present
- else `id = "nail." + pin.number + "." + index` (global pin list index)

### Enum maps

| Domain | JSON strings |
|--------|----------------|
| `EBoardSide` | `top`, `bottom`, `both`, `s2`…`s16` |
| mount | `smd`, `dip`, `unknown` |
| component type | lowercase: `resistor`, `capacitor`, `ic`, … |
| pin shape | `circle`, `rect`, `fold` |

Geometry is raw board space. Overlay / annotation fields are **not** included.

### Failed snapshot policy

**Choice: return empty `std::string`** for both exporters when `!snap.ok()`.  
Documented in header comments and enforced by unit test. Alternative (error object) rejected so HTTP layer can treat empty/failed parse separately without ambiguous JSON shapes.

---

## TDD evidence

1. **RED (design):** Brief required `test_export_has_schema` asserting `"boardSchemaVersion":1` and `"pins"`; symbols did not exist before implementation.
2. **GREEN:** Implemented header + exporter + CMake wire-up; tests compile and pass.
3. **Always-on test:** `test_export_failed_snap_empty` — parse garbage → `ExportBoardJson` / `ExportMetaJson` return empty.
4. **Optional:** `OBV_TEST_BOARD` env → full schema + pins + meta structure; skips with `skip export` when unset (no redistributable fixture in repo).

### Build / run

```text
cmake --build build-core-only --target obv_core_tests -j 8
./build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
# skip export
# ok
# exit:0
```

---

## Out of scope (not done)

- Overlay export / store (Task 4)
- HTTP server / web client
- nlohmann/json vendoring (ostringstream used instead)

---

## Concerns

- Without `OBV_TEST_BOARD`, full-board schema path is not exercised at runtime (only empty-export path). Recommend CI set `OBV_TEST_BOARD` to a private fixture when available.
- Component geometry now falls back at export time (pin bbox center + rect outline) when parse never ran DrawParts.

---

## Fix pass

**Commit:** `fix(core): board JSON pin/net ids and component geometry`

Addressed Task 3 Important review findings in `src/obv_core/src/board_json.cpp` only (no Task 4 work).

### 1. Pin id nail fallback for dummy components
- **Bug:** `pinId` treated any non-null `pin.component` as a real part. BRDBoard attaches nails/test pads to dummy components and strips `"..."` names to `""`, so IDs became `.1` and collided.
- **Fix:** Nail path when component is null, name empty, or `component_type == kComponentTypeDummy`: `nail.<number>.<globalPinIndex>`.
- Component.pins and global pins[] share a `Pin*` → global index map so the same pin gets the same id.

### 2. Component geometry defaults
- **Bug:** `centerpoint` / `outline_done` / `hull` are filled lazily in `BoardView::DrawParts`; pure parse export left center `0,0` and empty outline for most parts.
- **Fix:** `deriveCompGeom` at export: if special_outline / outline_done / hull present, keep them; else compute pin-bbox center and axis-aligned rect outline with pin-diameter/size margin. Recompute center from pins when stored center is still `(0,0)`.

### 3. Unique net IDs for name-only nets
- **Bug:** Export used `Net::number`, which BRDBoard often leaves unset → many nets shared `id`/`netId` 0.
- **Fix:** Sequential export-local map `Net* → id` starting at 1 in `Nets()` order; same map for pin/track/via/arc `netId`.

### Verification

```text
cmake --build build-core-only --target obv_core_tests -j 8
./build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
# skip export
# ok
# exit:0
```
