# Task 7 Report: Overlay HTTP API

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `c28ed4f` (Task 6 complete)  
**Commit:** `feat(server): overlay and annotation CRUD` (see `git log -1 --oneline`)  
**Date:** 2026-07-27

---

## Summary

Implemented overlay + freeform annotation CRUD on `obv_server`, backed by `obv::LoadOverlayForBoard` / `SavePartNetYaml` / `ExportOverlayJson` / `ApplyOverlayJson` and desktop `Annotations` mutators.

| Method | Path | Behavior |
|--------|------|----------|
| GET | `/api/v1/boards/:id/overlays` | Full overlay JSON `{annotations,partInfos,netInfos}`; 404 if board id unknown |
| PUT | `/api/v1/boards/:id/overlays` | Apply partInfos/netInfos from body (annotations ignored), save YAML, return full overlay |
| POST | `/api/v1/boards/:id/annotations` | Create freeform annotation → 201 + created object |
| PATCH | `/api/v1/boards/:id/annotations/:annId` | Update note → updated object |
| DELETE | `/api/v1/boards/:id/annotations/:annId` | Soft-delete (`visible=0`) → 204 |

- Per-board mutex: `BoardRegistry::OverlayMutex(id)` serializes overlay load/mutate/save (last-write-wins).
- Error envelope matches Task 6: `{"error":{"code","message"}}`.
- Overlay routes require only `BoardPath` presence (parse failure still allows YAML/SQLite sidecars).
- Freeform annotation routes need `HAVE_SQLITE3`; without it they return 501 `SQLITE_REQUIRED`.
- Build verified with `-DENABLE_SQLITE3=ON` (bundled `src/sqlite3`).

---

## Files modified

| Path | Change |
|------|--------|
| `src/obv_server/board_registry.h` / `.cpp` | `OverlayMutex(id)` + process-long per-id mutex map |
| `src/obv_server/routes.h` / `.cpp` | Overlay + annotation routes; minimal JSON body parsers for POST/PATCH |
| (build) | Reconfigure `build-web` with `ENABLE_SQLITE3=ON` for freeform notes |

No web client. No auth.

---

## Interfaces

```cpp
// BoardRegistry
std::mutex &OverlayMutex(const std::string &id);

// Handlers use:
// obv::LoadOverlayForBoard, ExportOverlayJson, ApplyOverlayJson, SavePartNetYaml
// Annotations::Add / Update / Remove / GenerateList / Close
```

On-disk (desktop-compatible):
- YAML: `<boardPath>.yaml` (PartInfos/NetInfos Version 0.0.2)
- SQLite: last `.` of boardPath → `_` then `.sqlite3` freeform annotations

---

## Verification

```text
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OBV_SERVER=ON -DENABLE_SQLITE3=ON
cmake --build build-web --target obv_server -j 8 --config Release
# → build-web/src/obv_server/Release/obv_server.exe

obv_server --host 127.0.0.1 --port 18083 --data tmp-data-t7

# unknown id
GET /api/v1/boards/0000…0000/overlays
# HTTP 404 {"error":{"code":"NOT_FOUND","message":"board not found"}}

# upload fake board (parse fail ok for overlay path)
POST X-Filename: fake.bin body=nope
# id=ca3704aa0b06f5954c79ee837faa152d84d6b2d42838f0637a15eda8337dbdce

GET  …/overlays → {"annotations":[],"partInfos":{},"netInfos":{}}
PUT  …/overlays {"partInfos":{"U1":{"part_type":"ic"}},"netInfos":{"N1":{"note":"hello"}}}
     → 200 with partInfos/netInfos set; disk: …_fake.bin.yaml
POST …/annotations {"side":0,"x":10,"y":20,"net":"NET1","part":"U1","pin":"1","note":"persist-me"}
     → 201 {"id":1,…,"note":"persist-me","visible":true}; disk: …_fake_bin.sqlite3
PATCH …/annotations/1 {"note":"updated-note"} → 200 note updated
DELETE …/annotations/1 → 204; GET overlays annotations=[]
POST  …/annotations {"side":1,"x":5,"y":6,"note":"after-restart"} → 201 id=2
# restart server
GET  …/overlays → annotation id=2 note "after-restart" + partInfos/netInfos still present
```

---

## Out of scope

- Task 8 web scaffold / API client
- Auth, optimistic concurrency tokens
- Full board parse-success fixture (overlays work on unparsed board paths)

---

## Concerns

1. **SQLite recommended for server builds** — freeform annotation CRUD is a no-op without `HAVE_SQLITE3`; routes return 501 rather than silently dropping notes.
2. **Annotation body parsers are hand-rolled** — sufficient for the fixed POST/PATCH shapes; PUT reuses `ApplyOverlayJson`.
3. **Per-board mutex map never shrinks** — process-long entries; fine for LAN board counts.
4. **C4819** avoided by keeping server comments ASCII-only.

---

## Critical fix follow-up (Task7Fix)

**Commit:** `fix(server): sqlite annotations and SQL escape`

### Fixes
1. **`ENABLE_SQLITE3` default ON** in `src/CMakeLists.txt` so default server builds define `HAVE_SQLITE3` and freeform annotation routes no longer return 501. Bundled `src/sqlite3` still used when system SQLite is missing.
2. **SQL escape at source** in `Annotations::Add`: `net`/`part`/`pin`/`note` all use `sqlite3_snprintf` `%q` (was `%s` for net/part/pin). `Update` already used `%q` for note; `Remove` only uses integer id.

### Verification
```text
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OBV_SERVER=ON
# ENABLE_SQLITE3:BOOL=ON; HAVE_SQLITE3 on obv_core
cmake --build build-web --target obv_server -j 8 --config Release

obv_server --host 127.0.0.1 --port 18084 --data tmp-data-t7fix
POST board fake.bin → id ca3704aa…
PUT overlays → 200
POST annotations net="O'Brien" → 201 stored as O'Brien
POST annotations net="x'); DROP TABLE annotations; --" → 201 literal string (table intact)
PATCH/DELETE/GET → 200/204/200 as expected
```

---

## Critical fix follow-up (Task7Fix2)

**Commit:** `fix(build): use SQLite3_FOUND for bundled sqlite fallback`

### Problem
`find_package(SQLite3)` sets `SQLite3_FOUND`, but `src/CMakeLists.txt` checked `SQLITE3_FOUND` (wrong case/name). That made the bundled `add_subdirectory(sqlite3)` always run even when system SQLite was found, redefining `SQLite::SQLite3` and breaking configure.

### Fix
- `src/CMakeLists.txt` ENABLE_SQLITE3 block: `if(NOT SQLite3_FOUND)` before `add_subdirectory(sqlite3)`.
- `CMakeModules/FindSQLite3.cmake` already uses `SQLite3_FOUND` and creates `SQLite::SQLite3` — no change.
- Bundled `src/sqlite3` still provides `SQLite::SQLite3` alias when system package is missing.

### Verification
```text
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OBV_SERVER=ON -DENABLE_SQLITE3=ON
# configure OK; system SQLite not present → bundled src/sqlite3 used
cmake --build build-web --target obv_server -j 8 --config Release
# → obv_server.exe; HAVE_SQLITE3 on obv_core / server
```
