# Task 4 Report: Overlay store (YAML + SQLite)

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `ddf813d` (Task 3 complete)  
**Commit:** `246392e` — `feat(core): overlay load/save and JSON for API`

---

## Summary

Implemented `obv::LoadOverlayForBoard`, `SavePartNetYaml`, `ExportOverlayJson`, and `ApplyOverlayJson` on top of existing desktop `Annotations` (YAML Version **0.0.2** PartInfos/NetInfos; optional SQLite freeform annotations when `HAVE_SQLITE3` / `ENABLE_SQLITE3`).

On-disk naming matches desktop:

| Artifact | Path rule |
|----------|-----------|
| YAML | `boardPath.string() + ".yaml"` |
| SQLite | last `.` in board path replaced with `_`, then `+ ".sqlite3"` (inside `Annotations::Load`) |

JSON uses hand-rolled ostringstream writer + minimal parser (same style as Task 3 board export; no nlohmann dependency).

---

## Files created

| Path | Purpose |
|------|---------|
| `src/obv_core/include/obv_core/overlay_store.h` | OverlayDocument DTO + load/save/JSON API |
| `src/obv_core/src/overlay_store.cpp` | Implementation |

## Files modified

| Path | Change |
|------|--------|
| `src/obv_core/CMakeLists.txt` | Add `src/overlay_store.cpp` to `OBV_CORE_SOURCES` |
| `src/obv_core_tests/test_parse_export.cpp` | YAML round-trip + JSON apply tests under `filesystem::temp_directory_path` |

---

## Interfaces

```cpp
namespace obv {
struct OverlayAnnotation {
  int id, side; double x, y;
  std::string net, part, pin, note;
  bool visible; // true for in-memory rows (desktop filters visible=1)
};
struct OverlayDocument {
  std::vector<OverlayAnnotation> annotations;
  std::map<std::string, PartInfo> partInfos;
  std::map<std::string, NetInfo> netInfos;
};

bool LoadOverlayForBoard(const filesystem::path &boardPath, Annotations &out, std::string &err);
bool SavePartNetYaml(const filesystem::path &boardPath, const Annotations &ann, std::string &err);
std::string ExportOverlayJson(const Annotations &ann);
bool ApplyOverlayJson(Annotations &ann, const std::string &json, std::string &err);
}
```

### Behavior notes

- **Load:** `Close` previous sqlite, clear maps/list, `SetFilename`, `Load` (sqlite), `RefreshPinInfos` (yaml). Missing yaml is OK (empty maps).
- **SavePartNetYaml:** copies `partInfos`/`netInfos` only (never shares caller's sqlite handle), sets filename, `SavePinInfos`. Fails if yaml file not present after write.
- **ExportOverlayJson:** `{annotations, partInfos, netInfos}`. Pin fields omit empties; `voltage_flag` as `"input"|"output"|"unknown"`; `angle` as enum int; annotations always `"visible":true`.
- **ApplyOverlayJson (PUT):** replaces `partInfos` / `netInfos` only when the corresponding key is present (`partInfos`/`PartInfos`, `netInfos`/`NetInfos`). `annotations` and other keys are skipped. Freeform CRUD remains `Annotations::Add`/`Update`/`Remove` + `GenerateList`.

---

## TDD evidence

1. **Always-on:** `test_overlay_yaml_roundtrip` — absolute temp dir under `filesystem::temp_directory_path`, save PartInfos/NetInfos, assert yaml contains `0.0.2` / PartInfos / NetInfos, load and assert fields, empty save/load no crash, JSON export keys, ApplyOverlayJson full replace + partial netInfos-only leave partInfos.
2. **Optional:** `#ifdef HAVE_SQLITE3` block Add → GenerateList → reload count 1 (skipped when `ENABLE_SQLITE3=OFF`, current default build).

### Build / run

```text
cmake --build build-core-only --target obv_core_tests -j 8
./build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
# skip export
# overlay yaml ok
# ok
# exit:0
```

---

## Out of scope (not done)

- Task 5+ server/web
- Enabling SQLite in default CI build
- Vendoring nlohmann/json

---

## Concerns

1. **SQLite path untested in default `build-core-only`** (`ENABLE_SQLITE3=OFF`). YAML + JSON paths covered; freeform annotation persistence needs a build with SQLite.
2. **`annotations.h` lacks `<string>`/`<vector>` includes** — overlay_store.h includes them first as a workaround; long-term fix belongs on the desktop header.
3. **Hand-rolled JSON parser** is intentionally minimal (overlay PUT shape only). Malformed deep nesting may yield generic error strings; sufficient for controlled API clients.
4. **Empty yaml still written** with `Version: 0.0.2` only — matches desktop `serialize` when maps empty after prune.

---

## Fix: SavePartNetYaml success detection (`fix(core): verify overlay YAML write success`)

**Finding:** `SavePartNetYaml` returned true when the yaml path merely existed after `SavePinInfos`, even if the write failed (e.g. read-only / existing file). Desktop `serialize` voids `file_write_text`'s bool; `SavePinInfos` cannot signal I/O failure.

**Fix:** Before `SavePinInfos`, record existence + size + mtime. After save require:
1. path exists,
2. newly created **or** mtime changed **or** size changed,
3. re-read content contains `0.0.2` (serialize always writes `Version: 0.0.2`).

Header documents that `SavePinInfos` cannot signal write failure and that overlay_store verifies via create/mtime/size + Version re-read.
