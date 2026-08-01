# Chip-Level Operating-Conditions Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cross-board chip library keyed by `part_type` for shared operating conditions; board `PartInfo.operating_conditions` becomes whole-set override; part reads and InfoPane surface effective conditions with source.

**Architecture:** New `ChipStore` under `dataRoot/chips/<sanitized-part_type>.yaml`. Shared merge helper (`board non-empty whole-set > chip`). Board condition CRUD stays board-override by default; `scope=chip` / promote write the library. Spec: `docs/superpowers/specs/2026-08-01-chip-library-operating-conditions-design.md`.

**Tech Stack:** C++20, CMake, `obv_core` + `obv_server` (cpp-httplib), ryml YAML, assert `obv_core_tests`, Vite/React web, `scripts/test_agent_api.py`.

## Global Constraints

- Only board overlay YAML + chip store under `dataRoot/chips/` are writable; board geometry/netlist read-only.
- Merge: **board override whole-set > chip library** when board `operating_conditions` non-empty.
- Default board condition routes **must not** write chip store (compat).
- Lock order when both needed: **board OverlayMutex first, then ChipStore mutex**.
- Errors: `{ "error": { "code", "message" } }`; no absolute server paths in JSON.
- Reuse `OperatingCondition`, `NormalizeOperatingCondition`; extend id allocation to work on `vector<OperatingCondition>`.
- No gtest; use `obv_core_tests` assert + `main`. Debug build for assert RED.
- Skip formatters/linters; no project-wide suite beyond named commands.
- Every task ends with verification + commit.
- Do not auto bulk-migrate historical board conditions.

---

## File structure

```
src/obv_core/
  include/obv_core/
    chip_store.h            # CREATE: ChipRecord, sanitize, ChipStore, merge
  src/
    chip_store.cpp          # CREATE
    pin_resolve.cpp         # AllocateConditionId overload; ExportPartSummaryJson merge optional later via server
  CMakeLists.txt            # add chip_store.cpp

src/obv_core_tests/
  test_parse_export.cpp     # sanitize, merge, YAML round-trip tests
  # OR test_chip_store.cpp + CMake if cleaner — prefer test_chip_store.cpp new file

src/obv_server/
  chip_store_host.h/.cpp    # optional thin wrapper — prefer using obv::ChipStore directly in routes
  routes.h                  # RegisterBoardRoutes(+ ChipStore&) or registry owns ChipStore
  routes.cpp                # chip routes + merge reads + scope/promote + PATCH part
  board_registry.h/.cpp     # own ChipStore instance from dataRoot
  CMakeLists.txt            # if new server files
  main.cpp                  # only if registration signature changes

web/src/
  types/board.ts            # OperatingCondition, conditions block types
  api/client.ts             # chip + part conditions helpers
  ui/InfoPane.tsx           # source badge, conditions list, promote, bind

scripts/
  test_agent_api.py         # chip library smoke cases
```

**Ownership decision (locked):** `BoardRegistry` constructs `obv::ChipStore` at `config.dataRoot / "chips"` and exposes `ChipStore& chips()`. `RegisterBoardRoutes(svr, registry)` uses `registry.chips()`.

---

### Task 1: ChipStore + sanitize + merge + YAML round-trip

**Files:**
- Create: `src/obv_core/include/obv_core/chip_store.h`
- Create: `src/obv_core/src/chip_store.cpp`
- Modify: `src/obv_core/CMakeLists.txt` (add `src/chip_store.cpp`)
- Modify: `src/obv_core/include/obv_core/pin_resolve.h` — overload `AllocateConditionId`
- Modify: `src/obv_core/src/pin_resolve.cpp` — implement vector overload; PartInfo version calls it
- Create: `src/obv_core_tests/test_chip_store.cpp`
- Modify: `src/obv_core_tests/CMakeLists.txt` — add `test_chip_store.cpp`

**Interfaces:**
- Produces:
```cpp
namespace obv {
struct ChipRecord {
  std::string part_type;
  std::string note;
  std::vector<OperatingCondition> operating_conditions;
};

// Returns false + err if empty after trim or maps to empty/./..
bool SanitizePartTypeFilename(const std::string &partType, std::string &outFileStem, std::string &err);

enum class ConditionSource { None, Board, Chip };

struct MergedConditions {
  ConditionSource source = ConditionSource::None;
  std::vector<OperatingCondition> effective;
  std::vector<OperatingCondition> board;
  std::vector<OperatingCondition> chip;
};

MergedConditions MergeOperatingConditions(
  const std::vector<OperatingCondition> *boardOrNull,
  const std::vector<OperatingCondition> *chipOrNull);

// Free helpers for YAML without holding lock (used by ChipStore and tests)
bool LoadChipRecordFile(const filesystem::path &path, ChipRecord &out, std::string &err);
bool SaveChipRecordFile(const filesystem::path &path, const ChipRecord &rec, std::string &err);

class ChipStore {
public:
  explicit ChipStore(filesystem::path rootDir);
  const filesystem::path &root() const;

  // Thread-safe. codes: empty / CHIP_NOT_FOUND / CHIP_PATH_COLLISION / CHIP_STORE_FAILED / INVALID_PART_TYPE
  bool Get(const std::string &partType, ChipRecord &out, std::string &errCode, std::string &errMsg);
  bool List(std::vector<ChipRecord> &out, std::string &errCode, std::string &errMsg);
  bool Put(const ChipRecord &rec, bool replaceConditionsIfPresent, std::string &errCode, std::string &errMsg);
  // PutUpsertConditions: replace conditions array entirely for partType (create if missing)
  bool ReplaceConditions(const std::string &partType, std::vector<OperatingCondition> ocs,
                         std::string &errCode, std::string &errMsg);
  bool Delete(const std::string &partType, std::string &errCode, std::string &errMsg);

  std::mutex &mutex(); // for external lock-order with board overlay
private:
  filesystem::path root_;
  std::mutex mu_;
};

std::string AllocateConditionId(const std::vector<OperatingCondition> &ocs);
std::string AllocateConditionId(const PartInfo &part); // delegates to vector
}
```

- Sanitize: keep `[A-Za-z0-9._+-]`, else `_`; reject empty/`.`/`..`.
- YAML keys: `part_type`, `note`, `operating_conditions` (same OC fields as annotations).
- `SaveChipRecordFile`: write `path.string()+".tmp"` then rename over target; on Windows if rename fails, write in place then reload-verify.
- `Get`: open sanitized path; require content `part_type ==` requested trim; mismatch → `CHIP_NOT_FOUND` (do not return wrong chip).
- `Put` collision: file exists with different content `part_type` → `CHIP_PATH_COLLISION`.

- [ ] **Step 1: Write failing tests** in `test_chip_store.cpp`:

```cpp
#include "obv_core/chip_store.h"
#include "obv_core/pin_resolve.h"
#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = filesystem;

static void test_sanitize_part_type() {
  std::string stem, err;
  assert(obv::SanitizePartTypeFilename("MP3398E", stem, err));
  assert(stem == "MP3398E");
  assert(obv::SanitizePartTypeFilename("  A/B\\C  ", stem, err));
  assert(stem == "A_B_C");
  assert(!obv::SanitizePartTypeFilename("   ", stem, err));
  assert(!obv::SanitizePartTypeFilename("...", stem, err) || stem == "___");
  // "..." maps to "___" which is allowed; "." alone must fail:
  assert(!obv::SanitizePartTypeFilename(".", stem, err));
  assert(!obv::SanitizePartTypeFilename("..", stem, err));
  std::cout << "sanitize ok\n";
}

static void test_merge_conditions() {
  OperatingCondition b; b.id = "oc_b"; b.outputs = {"Y"};
  OperatingCondition c; c.id = "oc_c"; c.inputs = {"A"};
  std::vector<OperatingCondition> board{b}, chip{c};

  auto m1 = obv::MergeOperatingConditions(&board, &chip);
  assert(m1.source == obv::ConditionSource::Board);
  assert(m1.effective.size() == 1 && m1.effective[0].id == "oc_b");
  assert(m1.board.size() == 1 && m1.chip.size() == 1);

  std::vector<OperatingCondition> empty;
  auto m2 = obv::MergeOperatingConditions(&empty, &chip);
  assert(m2.source == obv::ConditionSource::Chip);
  assert(m2.effective[0].id == "oc_c");

  auto m3 = obv::MergeOperatingConditions(nullptr, nullptr);
  assert(m3.source == obv::ConditionSource::None);
  assert(m3.effective.empty());
  std::cout << "merge ok\n";
}

static void test_chip_yaml_roundtrip() {
  const auto dir = fs::temp_directory_path() / "obv_chip_store_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  assert(!ec);

  obv::ChipStore store(dir);
  obv::ChipRecord rec;
  rec.part_type = "MP3398E";
  rec.note = "led driver";
  OperatingCondition oc;
  oc.id = "oc_0001";
  oc.name = "pwm";
  oc.inputs = {"PWM"};
  oc.outputs = {"CH1"};
  oc.enables = {"EN"};
  oc.note = "en high";
  rec.operating_conditions.push_back(oc);

  std::string code, msg;
  assert(store.Put(rec, true, code, msg));

  obv::ChipRecord got;
  assert(store.Get("MP3398E", got, code, msg));
  assert(got.part_type == "MP3398E");
  assert(got.note == "led driver");
  assert(got.operating_conditions.size() == 1);
  assert(got.operating_conditions[0].inputs[0] == "PWM");

  std::vector<obv::ChipRecord> list;
  assert(store.List(list, code, msg));
  assert(list.size() == 1);

  // restart store instance
  obv::ChipStore store2(dir);
  assert(store2.Get("MP3398E", got, code, msg));
  assert(got.operating_conditions[0].id == "oc_0001");

  assert(store2.Delete("MP3398E", code, msg));
  assert(!store2.Get("MP3398E", got, code, msg));
  assert(code == "CHIP_NOT_FOUND");

  fs::remove_all(dir, ec);
  std::cout << "chip yaml ok\n";
}

static void test_allocate_from_vector() {
  std::vector<OperatingCondition> ocs;
  OperatingCondition a; a.id = "oc_0001";
  ocs.push_back(a);
  assert(obv::AllocateConditionId(ocs) == "oc_0002");
  std::cout << "allocate vector ok\n";
}

int main() {
  test_sanitize_part_type();
  test_merge_conditions();
  test_chip_yaml_roundtrip();
  test_allocate_from_vector();
  std::cout << "ok\n";
  return 0;
}
```

If `test_chip_store.cpp` is a separate executable, give it its own `main`. Prefer **one** `obv_core_tests` binary: add tests as functions and call from existing `test_parse_export.cpp` main **or** merge into `test_parse_export.cpp` to avoid multi-main. **Locked choice:** put the four tests into `test_parse_export.cpp` and call them from existing `main` (no new exe). Skip creating `test_chip_store.cpp` if that keeps CMake simpler — **implementer: add tests to `test_parse_export.cpp` only**.

- [ ] **Step 2: Build Debug and confirm RED**

```
cmake --build build-core-only --target obv_core_tests --config Debug
```

Expected: compile error missing `chip_store.h` / symbols.

- [ ] **Step 3: Implement headers + cpp + CMake + AllocateConditionId overload**

YAML: implement with simple hand-written YAML emitter/parser **or** ryml like `annotations.cpp`. Prefer hand-rolled minimal YAML for chip files (only known keys) to avoid coupling ChipStore to Annotations — but reusing ryml write/read for `OperatingCondition` via including annotations adapters is OK if it compiles cleanly.

Minimal hand YAML write example shape:

```yaml
part_type: "MP3398E"
note: "led driver"
operating_conditions:
  - id: oc_0001
    name: pwm
    inputs: [PWM]
    outputs: [CH1]
    enables: [EN]
    note: en high
```

If hand parser is risky, load via temp Annotations-style ryml in chip_store.cpp by duplicating the small `write`/`read` for OperatingCondition (copy from annotations.cpp namespace) — **do not** break desktop annotations.

`ChipStore::Put`:
1. lock
2. sanitize
3. ensure root exists
4. if file exists, load; if content part_type != rec.part_type → collision
5. if `replaceConditionsIfPresent` false and file exists, keep old conditions when caller only updates note — follow interface: `Put(rec, replaceConditions)` when false and file exists, preserve existing conditions and update note/part_type only
6. save + verify reload

`ReplaceConditions(partType, ocs)`:
1. Get or empty record
2. set part_type, replace ocs, Put with replace true

- [ ] **Step 4: Build Debug + run tests GREEN**

```
cmake --build build-core-only --target obv_core_tests --config Debug
build-core-only\src\obv_core_tests\Debug\obv_core_tests.exe
```

Expected: prints sanitize/merge/chip yaml/allocate + existing tests + `ok`.

- [ ] **Step 5: Commit**

```bash
git add src/obv_core/include/obv_core/chip_store.h src/obv_core/src/chip_store.cpp \
  src/obv_core/CMakeLists.txt src/obv_core/include/obv_core/pin_resolve.h \
  src/obv_core/src/pin_resolve.cpp src/obv_core_tests/test_parse_export.cpp
git commit -m "feat(core): ChipStore YAML library and condition merge helper"
```

---

### Task 2: BoardRegistry owns ChipStore; part GET returns merged conditions

**Files:**
- Modify: `src/obv_server/board_registry.h` — `ChipStore` member + `chips()` accessor
- Modify: `src/obv_server/board_registry.cpp` — construct `chips_(cfg.dataRoot / "chips")`
- Modify: `src/obv_core/include/obv_core/pin_resolve.h` — extend `ExportPartSummaryJson` **or** add overload with chip conditions
- Modify: `src/obv_core/src/pin_resolve.cpp`
- Modify: `src/obv_server/routes.cpp` — part GET + conditions GET list/one use merge

**Interfaces:**
- Prefer overload:
```cpp
std::string ExportPartSummaryJson(const Board &board, const Annotations &ann,
  const std::string &boardId, const std::string &sourceName, const std::string &part,
  const std::vector<OperatingCondition> *chipConditionsOrNull);
```
Old 5-arg version can call new with `nullptr` for chip (source board/none only) — or update single signature and all call sites.

JSON additions on part summary:
```json
"partInfo": { "part_type", "angle", "operating_conditions": [/* effective */] },
"conditions": {
  "source": "board"|"chip"|"none",
  "effective": [...],
  "board": [...],
  "chip": [...]
}
```

Conditions list GET response:
```json
{
  "boardId", "part", "part_type",
  "source",
  "operating_conditions": [/* effective */],
  "board": [],
  "chip": []
}
```

GET one `:condId`: search **effective** only.

Helper in routes (anonymous namespace):
```cpp
bool loadChipConditions(BoardRegistry &registry, const std::string &partType,
  std::vector<OperatingCondition> &out) {
  out.clear();
  if (partType.empty()) return true;
  obv::ChipRecord rec;
  std::string code, msg;
  if (!registry.chips().Get(partType, rec, code, msg)) {
    if (code == "CHIP_NOT_FOUND") return true; // empty chip layer
    return false; // real error — caller maps to 500 CHIP_STORE_FAILED
  }
  out = std::move(rec.operating_conditions);
  return true;
}
```

- [ ] **Step 1: Wire BoardRegistry ChipStore**

```cpp
// board_registry.h
#include "obv_core/chip_store.h"
// member:
obv::ChipStore chips_;
// accessor:
obv::ChipStore &chips();
```

Ctor init: `chips_(cfg.dataRoot / "chips")`.

- [ ] **Step 2: Extend ExportPartSummaryJson**

Build `MergedConditions` from board partInfo ocs + chip vector; emit `partInfo.operating_conditions` = effective; emit `conditions` object; map source enum to strings `none|board|chip`.

- [ ] **Step 3: Update routes part GET** to load chip by part_type after overlay load and pass into export.

- [ ] **Step 4: Update conditions GET list + GET one** for merge/effective.

- [ ] **Step 5: Build server**

```
cmake --build build-web --target obv_server --config Release
```

Expected: success.

- [ ] **Step 6: Commit**

```bash
git add src/obv_server/board_registry.h src/obv_server/board_registry.cpp \
  src/obv_core/include/obv_core/pin_resolve.h src/obv_core/src/pin_resolve.cpp \
  src/obv_server/routes.cpp
git commit -m "feat(server): merge chip library into part condition reads"
```

---

### Task 3: Chip library REST routes

**Files:**
- Modify: `src/obv_server/routes.cpp`
- Modify: `src/obv_server/routes.h` (comment only if needed)

**Routes** (register before or after board routes; chip paths do not conflict):

```
GET    /api/v1/chips
GET    /api/v1/chips/:partType
PUT    /api/v1/chips/:partType
DELETE /api/v1/chips/:partType
GET|POST|PUT|DELETE .../operating-conditions[/:condId]
```

Reuse existing `operatingConditionObjectJson`, `parseOperatingConditionBody`, `parseOperatingConditionsReplaceBody`, `NormalizeOperatingCondition`, `AllocateConditionId(vector)`.

PUT chip body:
```json
{ "note"?: string, "operating_conditions"?: Condition[] }
```
part_type from path. If `operating_conditions` omitted, preserve existing conditions (`Put` with replaceConditions=false path).

Map store err codes to HTTP:
- INVALID_PART_TYPE → 400
- CHIP_NOT_FOUND → 404
- CHIP_PATH_COLLISION → 409
- CONDITION_ID_CONFLICT → 409
- CHIP_STORE_FAILED → 500

- [ ] **Step 1: Implement routes** with `:condId` before collection; lock `registry.chips().mutex()` around multi-step mutate (Get-modify-Put). Prefer ChipStore methods that already lock internally — avoid double-lock. **ChipStore methods take the mutex internally**; do not lock externally unless promoting with board lock held — then use internal methods only (ChipStore locks itself after board lock is held by caller — OK, no nested board inside chip).

- [ ] **Step 2: Build server Release**

- [ ] **Step 3: Manual curl smoke** (or defer full smoke to Task 6):

```
# with server running --data <tmpdir>
curl -s http://127.0.0.1:PORT/api/v1/chips
curl -s -X PUT http://127.0.0.1:PORT/api/v1/chips/TestChip -H "Content-Type: application/json" -d "{\"note\":\"n\",\"operating_conditions\":[{\"id\":\"oc_0001\",\"inputs\":[\"A\"],\"outputs\":[\"Y\"],\"enables\":[],\"name\":\"g\",\"note\":\"\"}]}"
curl -s http://127.0.0.1:PORT/api/v1/chips/TestChip
```

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(server): REST CRUD for chip operating-conditions library"
```

---

### Task 4: Board write split — scope=chip, promote, PATCH part_type

**Files:**
- Modify: `src/obv_server/routes.cpp`

**Behaviors:**

1. Existing POST/PUT/DELETE conditions: if query `scope=chip` **or** JSON body field `"scope":"chip"` → write chip library using placement `part_type`; else board override (unchanged).

2. `POST /api/v1/boards/:ref/parts/:part/operating-conditions:promote`  
   Body: `{ "clearBoard": false }`  
   Lock board overlay first; require part_type + non-empty board ocs; `ReplaceConditions` on chip; optional clear board ocs + save.

3. `PATCH /api/v1/boards/:ref/parts/:part`  
   Body: `{ "part_type": "..." }` (empty clears). Only updates `part_type`; preserves pins/conditions/angle.

Helper:

```cpp
bool wantsChipScope(const httplib::Request &req, const OperatingCondition *bodyScopeIgnored) {
  if (req.has_param("scope") && req.get_param_value("scope") == "chip") return true;
  // optional: parse "scope" from JSON root — for POST body that is the condition object,
  // scope as query is primary; for PUT replace body, allow "scope":"chip" sibling key.
  return false;
}
```

For condition object POST, **query param only** is enough to avoid polluting OC schema. For PUT collection, accept query `scope=chip`.

Promote path registration: literal path with `:promote` may need:

```
R"(/api/v1/boards/:ref/parts/:part/operating-conditions:promote)"
```

If httplib treats `:promote` badly, use:

```
R"(/api/v1/boards/:ref/parts/:part/operating-conditions/promote)"
```

**Locked for implementation:** use `/operating-conditions/promote` (slash) to avoid cpp-httplib param issues; update spec note in commit message. Spec said `:promote` — **slash form is acceptable equivalent**; document in route comment.

- [ ] **Step 1: Implement scope=chip on POST/PUT one/PUT collection/DELETE** (DELETE with scope=chip deletes from chip library by id).

- [ ] **Step 2: Implement promote + PATCH part_type**

- [ ] **Step 3: Build server**

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(server): board condition scope=chip, promote, PATCH part_type"
```

---

### Task 5: Web InfoPane — auto surface + bind + promote

**Files:**
- Modify: `web/src/types/board.ts`
- Modify: `web/src/api/client.ts`
- Modify: `web/src/ui/InfoPane.tsx`
- Modify: `web/src/index.css` (minimal badge styles if needed)

**Types:**

```ts
export interface OperatingCondition {
  id?: string;
  name?: string;
  inputs?: string[];
  outputs?: string[];
  enables?: string[];
  note?: string;
}

export type ConditionSource = 'board' | 'chip' | 'none';

export interface PartConditionsView {
  source: ConditionSource;
  effective: OperatingCondition[];
  board: OperatingCondition[];
  chip: OperatingCondition[];
}
```

Also add `operating_conditions?: OperatingCondition[]` on `PartInfo`.

**API client:**

```ts
export async function getPart(boardId: string, part: string): Promise<any> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}`);
  return parseJson(res);
}

export async function patchPart(boardId: string, part: string, body: { part_type?: string }): Promise<any> { ... }

export async function getPartOperatingConditions(boardId: string, part: string): Promise<...> { ... }

export async function promotePartOperatingConditions(
  boardId: string, part: string, clearBoard = false
): Promise<...> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}/operating-conditions/promote`,
    { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ clearBoard }) }
  );
  return parseJson(res);
}

// optional chip helpers if UI edits shared via scope
export async function postPartOperatingCondition(
  boardId: string, part: string, body: OperatingCondition, scope?: 'chip' | 'board'
) { ... }
```

**InfoPane UX (MVP):**
- Keep existing part_type field; on save part overlay still works via putOverlays.
- New section **Operating conditions** when a part is selected:
  - Fetch `getPartOperatingConditions` or use part GET when selection changes.
  - Show badge: 共享(chip) / 本板(board) / 无
  - List effective groups (name, inputs, outputs, enables, note) read-only list first is OK if edit is heavy — **minimum:** list + promote button + note that editing still via overlay/agent for board; **prefer** simple add form writing via API with scope based on source.
- Promote button when `board.length > 0` && part_type set.
- If part_type empty, show hint for shared.

Keep UI small: list effective + source badge + promote (clearBoard checkbox) + refresh. Full CRUD forms can call existing overlay path for board; chip write via `scope=chip` POST.

- [ ] **Step 1: Types + client**
- [ ] **Step 2: InfoPane section**
- [ ] **Step 3: `npm run build` in web/**
- [ ] **Step 4: Commit**

```bash
git add web/src/types/board.ts web/src/api/client.ts web/src/ui/InfoPane.tsx web/src/index.css
git commit -m "feat(web): show merged chip/board operating conditions on part"
```

---

### Task 6: API smoke tests

**Files:**
- Modify: `scripts/test_agent_api.py`
- Optional: `scripts/test_agent_api.ps1` if it only wraps python

**Cases to add** (group `chip`):

1. `PUT /api/v1/chips/{type}` with one condition → GET returns it  
2. Bind part_type on a real multi-pin part via PATCH or overlay PUT  
3. GET part conditions → `source=chip`, effective matches library  
4. POST board condition (default scope) → source becomes `board`; chip GET unchanged  
5. promote with clearBoard true → source `chip` again; board empty  
6. scope=chip POST adds to library  
7. Default board POST does not change chip file content (GET chip before/after)  
8. scope=chip without part_type → 400 PART_TYPE_REQUIRED  

Use unique `part_type` per run: `TestChip_{timestamp}` to avoid collisions.

Server start already uses `--data` temp or project data — ensure chips dir under that data root is writable. If script starts server with `--data`, chip files land there.

- [ ] **Step 1: Add cases to ALL_CASES**
- [ ] **Step 2: Run**

```
python scripts/test_agent_api.py -k chip
```

Expected: all chip cases pass; full suite still green if time allows:

```
python scripts/test_agent_api.py
```

- [ ] **Step 3: Commit**

```bash
git add scripts/test_agent_api.py
git commit -m "test: smoke chip library merge and promote API"
```

---

### Task 7: Spec status + final verification

**Files:**
- Modify: `docs/superpowers/specs/2026-08-01-chip-library-operating-conditions-design.md` — Status → Implemented (or Approved+implemented); note promote path `/operating-conditions/promote`

- [ ] **Step 1: Update spec status line + promote path note if slash form used**
- [ ] **Step 2: Run core tests Debug + agent api suite**
- [ ] **Step 3: Commit docs**

```bash
git commit -am "docs: mark chip library design implemented"
```

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| dataRoot/chips YAML store | 1 |
| sanitize + collision | 1 |
| merge board > chip whole-set | 1, 2 |
| Chip REST CRUD | 3 |
| Part GET conditions block | 2 |
| Conditions GET effective | 2 |
| Default board write unchanged | 4 (preserve) + 6 |
| scope=chip | 4 |
| promote + clearBoard | 4 |
| PATCH part_type | 4 |
| Web auto surface | 5 |
| Smoke / success criteria | 6 |
| No auto bulk migrate | (no task — intentional) |
| Lock order board then chip | 4 promote |

## Placeholder scan

None intentional. Promote URL uses `/promote` slash form (httplib-safe).

## Type consistency

- `ConditionSource` / `MergedConditions` / `ChipRecord` names used consistently across tasks.
- `AllocateConditionId(vector)` then PartInfo delegates.
- `BoardRegistry::chips()` returns `obv::ChipStore&`.
