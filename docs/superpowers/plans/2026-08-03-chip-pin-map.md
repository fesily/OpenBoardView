# Chip Library Pin Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add datasheet-style pin map (`id` ↔ `name` + aliases) to chip library YAML/API, enrich board part/conditions reads with `chipPins` + `resolved`, and show `B3 (VBAT)` in InfoPane.

**Architecture:** Extend `ChipRecord` with `pins[]` in `obv_core` ChipStore (same `boardRoot/chips/*.yaml`). Pure helpers for normalize, uniqueness, resolve. Server chip REST for pins + resolve; board GET enrichment. Web read-only label display. Spec: `docs/superpowers/specs/2026-08-03-chip-pin-map-design.md`.

**Tech Stack:** C++20, existing ChipStore hand YAML, `obv_server` routes, React InfoPane, `scripts/test_agent_api.py`.

## Global Constraints

- Chip root remains **`boardRoot/chips/`** (not dataRoot).
- Case-sensitive keys for id/name/alias.
- Conditions do **not** require pins membership.
- Promote / default board condition writes **must not** mutate pins.
- Chip PUT: omit `pins` → preserve; `"pins":[]` → clear; present array → replace.
- Dir enum strict: `""|in|out|io|power|ground|nc|analog|other` else 400.
- Max 1024 pins/chip; alias ≤32; id/name/alias ≤64; note ≤2048.
- Errors `{error:{code,message}}`; `PIN_KEY_CONFLICT` / `PIN_NOT_FOUND`.
- No auto `show_name` write; no pin editor UI.
- UI display: hit with both id+name → `"{id} ({name})"` else `label`.
- TDD with `obv_core_tests` Debug asserts; skip formatters; commit per task.
- Tests in `test_parse_export.cpp` (existing binary).

---

## File structure

```
src/obv_core/include/obv_core/chip_store.h   # ChipPin, resolve, normalize, ReplacePins, Put flags
src/obv_core/src/chip_store.cpp              # YAML pins + helpers + store methods
src/obv_core_tests/test_parse_export.cpp     # pin map unit tests
src/obv_server/routes.cpp                    # chip pins routes + board enrichment + chipRecordJson
web/src/types/board.ts
web/src/ui/InfoPane.tsx
scripts/test_agent_api.py
docs/superpowers/specs/2026-08-03-chip-pin-map-design.md  # status at end
```

**Put API change (locked):**

```cpp
bool Put(const ChipRecord &rec,
         bool replaceConditionsIfPresent,
         bool replacePinsIfPresent,
         std::string &errCode, std::string &errMsg);
```

All existing call sites pass `replacePinsIfPresent` equal to whether they intend to replace pins (condition-only updates → `false`).

---

### Task 1: ChipPin model, YAML, normalize, resolve, uniqueness

**Files:**
- Modify: `src/obv_core/include/obv_core/chip_store.h`
- Modify: `src/obv_core/src/chip_store.cpp`
- Modify: `src/obv_core_tests/test_parse_export.cpp`

**Interfaces:**

```cpp
struct ChipPin {
  std::string id;
  std::string name;
  std::vector<std::string> aliases;
  std::string dir;
  std::string note;
};

struct ChipRecord {
  std::string part_type;
  std::string note;
  std::vector<ChipPin> pins;
  std::vector<OperatingCondition> operating_conditions;
};

enum class ChipPinMatch { None, Id, Name, Alias };

struct ChipPinResolveResult {
  ChipPinMatch matched = ChipPinMatch::None;
  const ChipPin *pin = nullptr; // non-owning into record.pins
};

bool NormalizeChipPin(ChipPin &pin, std::string &err);
// trim; drop empty aliases; limits; dir enum; id non-empty

bool ValidateChipPinTable(const std::vector<ChipPin> &pins, std::string &err);
// uniqueness of all keys; max 1024; err mentions conflict key

ChipPinResolveResult ResolveChipPin(const ChipRecord &rec, const std::string &label);
// trim label; search id, then name, then aliases in table order

// ChipStore::Put gains replacePinsIfPresent
// ChipStore::ReplacePins(partType, pins, errCode, errMsg) — create if missing, replace pins only
```

YAML emit: after `note`, emit `pins:` sequence (or `pins:\n  []\n`), then `operating_conditions`.  
YAML parse: accept `pins` key; missing → empty.

- [ ] **Step 1: Failing tests** in `test_parse_export.cpp`:

```cpp
static void test_chip_pin_normalize_and_resolve() {
  obv::ChipPin p;
  p.id = "  B3  ";
  p.name = " VBAT ";
  p.aliases = {" BAT ", "", "VBAT"}; // VBAT dup with name → Validate fails later; normalize keeps
  p.dir = "power";
  std::string err;
  assert(obv::NormalizeChipPin(p, err));
  assert(p.id == "B3" && p.name == "VBAT");
  // aliases: empty dropped; "BAT" kept; "VBAT" still present until Validate
  p.aliases = {"BAT"};
  assert(obv::NormalizeChipPin(p, err));

  obv::ChipPin bad; bad.id = "X"; bad.dir = "nope";
  assert(!obv::NormalizeChipPin(bad, err));

  std::vector<obv::ChipPin> table;
  obv::ChipPin a; a.id = "B3"; a.name = "VBAT"; a.aliases = {"BAT"}; a.dir = "power";
  assert(obv::NormalizeChipPin(a, err));
  table.push_back(a);
  obv::ChipPin b; b.id = "H4"; b.name = "UART_TXD"; b.dir = "out";
  assert(obv::NormalizeChipPin(b, err));
  table.push_back(b);
  assert(obv::ValidateChipPinTable(table, err));

  obv::ChipPin c; c.id = "Z1"; c.name = "B3"; // name conflicts with a.id
  assert(obv::NormalizeChipPin(c, err));
  table.push_back(c);
  assert(!obv::ValidateChipPinTable(table, err));
  table.pop_back();

  obv::ChipRecord rec;
  rec.pins = table;
  auto r1 = obv::ResolveChipPin(rec, "B3");
  assert(r1.matched == obv::ChipPinMatch::Id && r1.pin && r1.pin->name == "VBAT");
  auto r2 = obv::ResolveChipPin(rec, "VBAT");
  assert(r2.matched == obv::ChipPinMatch::Name && r2.pin->id == "B3");
  auto r3 = obv::ResolveChipPin(rec, "BAT");
  assert(r3.matched == obv::ChipPinMatch::Alias);
  auto r4 = obv::ResolveChipPin(rec, "nope");
  assert(r4.matched == obv::ChipPinMatch::None && !r4.pin);
  std::cout << "chip pin resolve ok\n";
}

static void test_chip_pins_yaml_roundtrip() {
  const auto dir = filesystem::temp_directory_path() / "obv_chip_pins_test";
  std::error_code ec;
  filesystem::remove_all(dir, ec);
  filesystem::create_directories(dir, ec);
  obv::ChipStore store(dir);
  obv::ChipRecord rec;
  rec.part_type = "MP3398E";
  rec.note = "n";
  obv::ChipPin pin; pin.id = "B3"; pin.name = "VBAT"; pin.aliases = {"BAT"}; pin.dir = "power";
  rec.pins.push_back(pin);
  OperatingCondition oc; oc.id = "oc_0001"; oc.inputs = {"B3"};
  rec.operating_conditions.push_back(oc);
  std::string code, msg;
  assert(store.Put(rec, true, true, code, msg));

  obv::ChipRecord got;
  assert(store.Get("MP3398E", got, code, msg));
  assert(got.pins.size() == 1 && got.pins[0].name == "VBAT");
  assert(got.operating_conditions.size() == 1);

  // preserve pins when replacePins false
  got.note = "n2";
  got.pins.clear();
  assert(store.Put(got, false, false, code, msg));
  obv::ChipRecord got2;
  assert(store.Get("MP3398E", got2, code, msg));
  assert(got2.note == "n2");
  assert(got2.pins.size() == 1);
  assert(got2.operating_conditions.size() == 1);

  // clear pins
  got2.pins.clear();
  assert(store.Put(got2, false, true, code, msg));
  obv::ChipRecord got3;
  assert(store.Get("MP3398E", got3, code, msg));
  assert(got3.pins.empty());
  assert(got3.operating_conditions.size() == 1);

  filesystem::remove_all(dir, ec);
  std::cout << "chip pins yaml ok\n";
}
```

Call from `main`.

- [ ] **Step 2: Build Debug RED** — missing symbols / Put arity.

- [ ] **Step 3: Implement** model, YAML, helpers, Put(3 bools)/ReplacePins; update **all** `Put(` call sites in `chip_store.cpp` tests and later server in Task 2 — for Task 1 only fix core + tests. Grep `\.Put\(` and fix compile in core tests; server still broken until Task 2 if Put signature changes — **must update all server Put call sites in same commit as signature change** to keep tree building.

**Locked:** Task 1 updates `Put` signature and fixes every `registry.chips().Put` / `store.Put` in repo so `obv_server` still compiles (pass `replacePinsIfPresent=false` for condition-only paths, `true` only when replacing whole record with pins intent).

- [ ] **Step 4: Debug tests GREEN**

```
cmake --build build-core-only --target obv_core_tests --config Debug
build-core-only\src\obv_core_tests\Debug\obv_core_tests.exe
```

Also:

```
cmake --build build-web --target obv_server --config Release
```

- [ ] **Step 5: Commit**

```bash
git commit -am "feat(core): chip pin map model, YAML, resolve, Put pin flags"
```

---

### Task 2: Chip REST pins + resolve; chip GET/PUT/list JSON

**Files:**
- Modify: `src/obv_server/routes.cpp` only (large file; keep helpers near existing `chipRecordJson`)

**Changes:**

1. `chipRecordJson` include `"pins":[...]`
2. List items add `"pinCount": N`
3. `parseChipPutBody` also parse optional `pins` → `hasPins`
4. PUT chip: `Put(rec, hasConditions, hasPins, ...)`
5. Routes (before generic chip GET if needed; more specific first):

```
GET/PUT/DELETE /api/v1/chips/:partType/pins/:pinKey
GET/PUT         /api/v1/chips/:partType/pins
GET             /api/v1/chips/:partType/resolve
```

6. Helpers: `chipPinObjectJson`, `chipPinsArrayJson`, `parseChipPinObject`, `parseChipPinsReplaceBody`, `mapChipStoreError` add `PIN_KEY_CONFLICT` if used from store — or map validate errors to 400 in route.

Validate in route before Put: `NormalizeChipPin` each + `ValidateChipPinTable`.

Resolve: query `label`; 400 empty; 404 PIN_NOT_FOUND.

- [ ] **Step 1: Implement JSON + routes**
- [ ] **Step 2: Build Release obv_server**
- [ ] **Step 3: Curl smoke** (start server if needed against temp data + boardRoot with chips dir):

```
PUT /api/v1/chips/TestPinMap {"pins":[{"id":"B3","name":"VBAT","aliases":["BAT"],"dir":"power","note":""}]}
GET /api/v1/chips/TestPinMap/resolve?label=VBAT
GET /api/v1/chips/TestPinMap/pins/B3
```

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(server): chip pins REST CRUD and resolve"
```

---

### Task 3: Board part/conditions enrichment (`chipPins` + `resolved`)

**Files:**
- Modify: `src/obv_server/routes.cpp` — `operatingConditionsMergedListJson`, part GET path, single condition GET
- Optionally extend `ExportPartSummaryJson` in pin_resolve — **prefer enrich in routes** after export or extend export to accept chip pins; simplest: build merged conditions JSON entirely in routes (already does for conditions list) and for part GET either post-process or extend `ExportPartSummaryJson` signature.

**Locked approach for part GET:** extend `ExportPartSummaryJson` overload:

```cpp
std::string ExportPartSummaryJson(..., const ChipRecord *chipOrNull);
```

When chip non-null, emit `chipPins` and `conditions.resolved` from effective conditions after merge (merge still uses chip conditions vector; pass full ChipRecord for pins+resolve).

Or keep merge in routes and pass `chipPins` + prebuilt resolved JSON strings — implementer chooses minimal change; **must** match spec field names.

Resolved entry:

```json
{"label":"B3","matched":"id","id":"B3","name":"VBAT"}
```

`matched` strings: `none|id|name|alias`.

- [ ] **Step 1: Implement enrichment**
- [ ] **Step 2: Build server**
- [ ] **Step 3: Commit**

```bash
git commit -am "feat(server): chipPins and resolved labels on part condition reads"
```

---

### Task 4: Web InfoPane display

**Files:**
- Modify: `web/src/types/board.ts` — `ChipPin`, resolved types on `PartConditionsView`
- Modify: `web/src/ui/InfoPane.tsx` — format labels via resolved
- Modify: `web/src/api/client.ts` only if types imported there

Display helper:

```ts
function formatResolvedLabel(
  label: string,
  resolved?: { label: string; matched: string; id?: string; name?: string }
): string {
  if (resolved && resolved.matched && resolved.matched !== 'none' &&
      resolved.id && resolved.name) {
    return `${resolved.id} (${resolved.name})`;
  }
  return label;
}
```

Wire to inputs/outputs/enables list rendering.

- [ ] **Step 1: Types + UI**
- [ ] **Step 2: `npm run build` in web/**
- [ ] **Step 3: Commit**

```bash
git commit -am "feat(web): show resolved chip pin names on conditions"
```

---

### Task 5: API smoke tests

**Files:**
- Modify: `scripts/test_agent_api.py`

Add group `chip_pins` (or extend `chip`):

1. PUT pins on unique `part_type`
2. resolve by id and by name → same pin
3. conflict second pin with same name → 400 PIN_KEY_CONFLICT
4. bind part_type on fixture part; GET conditions → chipPins non-empty + resolved hit for a label used in a chip condition (write one condition with input B3 via scope=chip or chip conditions API)
5. promote conditions does not clear pins (GET pins still present)
6. chip PUT omit pins preserves table

- [ ] **Step 1: Add cases**
- [ ] **Step 2: `python scripts/test_agent_api.py -k chip`** (or full suite)
- [ ] **Step 3: Commit**

```bash
git commit -am "test: smoke chip pin map resolve and board enrichment"
```

---

### Task 6: Spec status + final verify

- Mark design **Implemented**
- Run Debug `obv_core_tests` + `python scripts/test_agent_api.py`
- Commit docs

```bash
git commit -am "docs: mark chip pin map design implemented"
```

---

## Spec coverage

| Requirement | Task |
|-------------|------|
| ChipPin + YAML | 1 |
| Normalize/unique/resolve | 1 |
| Put preserve/replace pins | 1 |
| Chip REST pins + resolve | 2 |
| chip GET/list pin fields | 2 |
| Board chipPins + resolved | 3 |
| UI display rule | 4 |
| Smoke + promote preserves pins | 5 |
| Docs status | 6 |

## Type consistency

- `ChipPinMatch` / `matched` strings aligned
- `Put(..., replaceConditions, replacePins, ...)`
- `PIN_KEY_CONFLICT` / `PIN_NOT_FOUND`
