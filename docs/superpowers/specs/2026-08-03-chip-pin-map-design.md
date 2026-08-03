# Chip Library Pin Map (Datasheet Ball ↔ Signal) — Design Spec

**Date:** 2026-08-03  
**Status:** Approved — implementation in progress
**Parent:** `2026-08-01-chip-library-operating-conditions-design.md`  
**Goal:** Extend the chip library (`boardRoot/chips/<part_type>.yaml`) with a datasheet-style **pin map**: bidirectional lookup between package pin/ball id and signal name (plus aliases). Surface the table on chip APIs and board part reads; resolve operating-condition labels for agents/UI. Do **not** auto-write board overlay `show_name`.

---

## 1. Goals and non-goals

### Goals

1. **Structured pin table on `ChipRecord`** — each package pin: `id` (ball/number), primary `name`, optional `aliases[]`, optional `dir`, optional `note`.
2. **Bidirectional lookup** — resolve a label to a pin via `id` **or** `name` **or** any `alias` (case-sensitive, same vocabulary as existing condition labels).
3. **Chip REST** — CRUD/list/get pins; single-label resolve endpoint; chip GET/PUT include `pins`.
4. **Board part reads** — when placement has `part_type` bound to a chip record, return `chipPins` and per-condition label `resolved` annotations on the effective operating-condition groups.
5. **UI (MVP)** — InfoPane condition list shows resolved signal next to labels (e.g. `B3 (VBAT)`); no pin-table editor.
6. Stay inside architecture invariants:
   - Chip data under **`boardRoot/chips/`** (current root)
   - Board geometry/netlist read-only
   - Board overlay remains the only board-local writable layer; pin map is **chip-level only**
   - Operating conditions still allow labels that are **not** in the pin table (no strict referential integrity)

### Explicit non-goals

- Automatic bulk `show_name` write from datasheet names onto board pins
- Full mux mode tables (ALT0/ALT1 matrices) — aliases cover multi-function for MVP
- Separate pins sidecar file
- Forcing migration of existing condition labels from ball id → signal name
- Desktop OpenBoardView loading the chip pin map
- Auth / multi-tenant / OCR of datasheets

### Success criteria

1. PUT chip pins for `part_type=X` persists in `boardRoot/chips/<sanitized>.yaml` and survives restart.
2. `GET .../chips/X/resolve?label=B3` and `?label=VBAT` both return the same pin when configured with `id=B3`, `name=VBAT`.
3. Duplicate keys within a chip (two pins sharing id/name/alias) → **400 `PIN_KEY_CONFLICT`**.
4. Board part / conditions GET with bound `part_type` includes `chipPins` and `conditions.resolved` for effective groups.
5. Old chip YAML without `pins` still loads; conditions unchanged; `pins: []`.
6. Promote / board condition default writes do **not** mutate the pin table.
7. Errors remain `{error:{code,message}}`.

---

## 2. Problem statement

Today `ChipRecord` is:

```yaml
part_type: "..."
note: "..."
operating_conditions: [ { id, name, inputs, outputs, enables, note } ]
```

Datasheet knowledge (ball ↔ function) is stuffed into free-text `note` (e.g. `B3=VBAT`). Agents cannot reliably:

- map ball → signal or signal → ball
- validate / display human names next to condition labels
- share a single pin dictionary across boards for the same `part_type`

---

## 3. Architecture placement

```text
Agent / Web
    │ REST
    ▼
obv_server routes
    │
    ├─► ChipStore (boardRoot/chips/*.yaml)
    │       ChipRecord.pins[]          ← NEW
    │       ChipRecord.operating_conditions
    │       ResolveChipPin(label)
    │
    └─► Board part/conditions GET
            merge conditions (existing)
            + chipPins + resolved labels
```

| Concern | Location |
|---------|----------|
| `ChipPin` model + YAML IO | `obv_core` `chip_store` |
| Normalize + uniqueness | `obv_core` helpers |
| Resolve label | pure helper + optional HTTP |
| Chip pins routes | `obv_server` routes |
| Board read enrichment | part summary / conditions JSON builders |
| InfoPane display | `web` InfoPane only (read-only) |

---

## 4. Data model

### 4.1 `ChipPin`

```cpp
struct ChipPin {
  std::string id;       // package pin/ball; required; unique per chip
  std::string name;     // primary signal name; may be empty
  std::vector<std::string> aliases;
  std::string dir;      // in|out|io|power|ground|nc|analog|other|"" 
  std::string note;
};
```

On `ChipRecord`:

```cpp
struct ChipRecord {
  std::string part_type;
  std::string note;
  std::vector<ChipPin> pins;                      // NEW
  std::vector<OperatingCondition> operating_conditions;
};
```

### 4.2 YAML shape

```yaml
part_type: "CYW20734"
note: "..."
pins:
  - id: "B3"
    name: "VBAT"
    aliases: ["BAT"]
    dir: "power"
    note: "1.62-3.6 V"
  - id: "H4"
    name: "UART_TXD"
    aliases: []
    dir: "out"
    note: ""
operating_conditions:
  - id: "oc_0001"
    name: "Power / Reset"
    inputs: ["B3"]
    outputs: ["E1"]
    enables: ["H3"]
    note: "..."
```

### 4.3 Field rules

| Field | Rules |
|-------|--------|
| `id` | Required after trim; length 1–64; unique among all index keys for the chip |
| `name` | Optional; trim; max 64; if non-empty, unique among index keys |
| `aliases` | Max 32 entries; each trim, drop empty; each max 64; each unique among index keys |
| `dir` | Optional; allowed: `""`, `in`, `out`, `io`, `power`, `ground`, `nc`, `analog`, `other`. Any other value → **400** on write |
| `note` | Max 2048 |

**Case sensitivity:** all of `id` / `name` / `aliases` comparisons are **case-sensitive** (matches existing condition label / board pin key style).

### 4.4 Index / uniqueness

Build a map of key → pin index for:

- every `id`
- every non-empty `name`
- every `alias`

On write of one pin or full table: if any key maps to two different pins → **400 `PIN_KEY_CONFLICT`** (message lists the key).

A pin may list its `name` equal to `id` (rare); that is one key, not a conflict with itself.

### 4.5 Resolve

```text
ResolveChipPin(record, label) ->
  trim label; empty → miss
  if key in index → hit with matched ∈ { id, name, alias }
  else miss
```

Miss is not an error for condition annotation (`matched: "none"`).  
HTTP resolve endpoint uses **404 `PIN_NOT_FOUND`** on miss.

### 4.6 Relation to operating conditions

- Condition `inputs` / `outputs` / `enables` remain free string labels (ball ids, signal names, or free text).
- **No** write-time requirement that labels exist in `pins`.
- Read-time resolution is best-effort annotation only.
- Promote copies conditions only; **never** overwrites `pins`.

---

## 5. Persistence (ChipStore)

### 5.1 File root

Unchanged from relocation: **`config.boardRoot / "chips"`**.

### 5.2 Load / save

- Extend hand YAML (or current chip YAML IO) to read/write `pins` sequence.
- Missing `pins` key → empty vector (backward compatible).
- `Put(rec, replaceConditionsIfPresent)` semantics extended:
  - **Conditions:** if `replaceConditionsIfPresent == false` and file exists, keep old conditions (existing).
  - **Pins:** if body/API omits pins on chip PUT, **preserve existing pins** (mirror conditions omit behavior). Implement via explicit flags or optional presence tracking in the HTTP layer (parse “pins key present?”).
- New helpers as needed:
  - `ReplacePins(partType, pins, ...)`
  - `NormalizeChipPin` / `ValidateChipPinTable`

### 5.3 Limits

- Max pins per chip: **1024** (server-enforced).
- Same atomic write pattern as today (tmp + rename / Windows fallback + reload verify); verify must include pin count and a sample of ids/names.

---

## 6. HTTP API

### 6.1 Chip resource changes

**`GET /api/v1/chips`**

Each list item gains `pinCount` (int).

**`GET /api/v1/chips/:partType`**

```json
{
  "part_type": "CYW20734",
  "note": "...",
  "pins": [ { "id", "name", "aliases", "dir", "note" } ],
  "operating_conditions": [ ... ]
}
```

**`PUT /api/v1/chips/:partType`**

Body may include `note`, `operating_conditions`, `pins` independently:

| Field in body | Behavior |
|---------------|----------|
| omitted `pins` | keep existing pins |
| `"pins": []` | clear pin table |
| `"pins": [ ... ]` | replace entire table after normalize/unique check |
| omitted `operating_conditions` | keep existing (existing rule) |

### 6.2 Pins subresource

```text
GET    /api/v1/chips/:partType/pins
PUT    /api/v1/chips/:partType/pins              # body: { "pins": [ ... ] } full replace
GET    /api/v1/chips/:partType/pins/:pinKey
PUT    /api/v1/chips/:partType/pins/:pinKey      # upsert
DELETE /api/v1/chips/:partType/pins/:pinKey      # 404 if missing
GET    /api/v1/chips/:partType/resolve?label=...
```

**`:pinKey` matching order:** exact `id`, else exact `name`, else exact `alias` (first pin in table order if somehow duplicated — should be impossible after validation).

**PUT one pin:**

1. Parse body as pin fields (`id` optional).
2. If `:pinKey` resolves to an existing pin → update that row. New `id` = body `id` if non-empty after trim, else keep existing `id`. Then re-check whole-table uniqueness.
3. If miss → create; require `id` in body or use `:pinKey` as `id`.
4. Re-validate whole table uniqueness after change.

**Resolve:**

```json
{
  "part_type": "CYW20734",
  "label": "VBAT",
  "matched": "name",
  "pin": { "id": "B3", "name": "VBAT", "aliases": ["BAT"], "dir": "power", "note": "" }
}
```

Miss → 404 `PIN_NOT_FOUND`. Empty label → 400 `BAD_REQUEST`.

Register more-specific routes (`.../pins/:pinKey`, `.../resolve`) before collection routes; keep existing condition routes.

### 6.3 Board part / conditions enrichment

**`GET /api/v1/boards/:ref/parts/:part`**

Add:

```json
"chipPins": [ /* from chip library if part_type bound and chip exists; else [] */ ],
"conditions": {
  "source": "board|chip|none",
  "effective": [ ... ],
  "board": [ ... ],
  "chip": [ ... ],
  "resolved": {
    "<condId>": {
      "inputs":  [ { "label", "matched", "id", "name" } ],
      "outputs": [ ... ],
      "enables": [ ... ]
    }
  }
}
```

For each effective condition label:

| Field | Meaning |
|-------|---------|
| `label` | original string in the condition |
| `matched` | `id` \| `name` \| `alias` \| `none` |
| `id` | pin id if hit, else `""` |
| `name` | pin primary name if hit, else `""` |

**`GET .../operating-conditions`** (list) **must** include `chipPins` and `resolved` for the effective set.
**`GET .../operating-conditions/:condId`** **must** include `resolved` for that one condition (object or inline fields).

Chip store load failure still maps to 500 `CHIP_STORE_FAILED` (existing). Missing chip → empty `chipPins` / all `matched: none`.

### 6.4 Errors (additive)

| HTTP | code | When |
|------|------|------|
| 400 | `BAD_REQUEST` | normalize failure, over limits, empty resolve label |
| 400 | `PIN_KEY_CONFLICT` | duplicate id/name/alias in table |
| 404 | `PIN_NOT_FOUND` | pinKey or resolve miss |
| 404 | `CHIP_NOT_FOUND` | existing |
| 500 | `CHIP_STORE_FAILED` | existing |

---

## 7. Web UI (MVP)

In InfoPane **Operating conditions** list:

- Display uses the locked rule below only (no alternate formats).
- **Locked display rule:**  
  - `matched != none` and both id and name non-empty → `"{id} ({name})"`  
  - else show `label`
- No pin-table editor; no promote changes.

Client: extend types for `ChipPin`, `ResolvedLabel`, parse new fields from conditions GET (already fetched).

---

## 8. Compatibility / migration

- Existing chip files without `pins` load as empty pins; no rewrite until next save.
- Existing CYW20734 conditions keep ball numbers; agents may gradually fill `pins` from datasheet.
- No automatic extraction from condition notes.
- Board overlays unchanged.

---

## 9. Testing / verification

1. **Core unit tests** (`obv_core_tests`):
   - YAML round-trip with pins
   - uniqueness conflict detection
   - resolve by id/name/alias
   - omit-pins preserve on Put flag path (if exposed at store layer)
2. **API smoke** (extend `scripts/test_agent_api.py` group `chip`):
   - PUT pins → GET pins → resolve both ways
   - conflict 400
   - board part GET shows `chipPins` + resolved hit
   - promote does not clear pins
3. **Manual:** InfoPane shows `B3 (VBAT)` after pins filled for a bound part.

---

## 10. Implementation sketch

1. `ChipPin` + YAML IO + normalize/unique/resolve helpers + tests  
2. ChipStore preserve/replace pins semantics  
3. Chip REST pins + resolve; extend chip GET/PUT/list  
4. Board part/conditions JSON enrichment  
5. Web InfoPane label display  
6. Smoke cases + spec status  

---

## 11. Approach decision record

| Option | Decision |
|--------|----------|
| A. `pins[]` on same chip YAML | **Accepted** |
| B. Side-car pins file | Rejected |
| C. Force conditions to signal names | Rejected |

User locks:

- Bidirectional ball ↔ signal  
- Primary name + aliases for mux  
- MVP: chip API + board resolve enrichment + read-only UI  
- Case-sensitive keys  
- No auto `show_name` write  

---

## 12. Closed points

| Topic | Resolution |
|-------|------------|
| Case folding | Sensitive |
| Strict dir enum | Yes; illegal → 400 |
| Condition must reference pins | No |
| Promote vs pins | Promote never touches pins |
| Chip PUT omit pins | Preserve existing |
| Resolve batch API | Out of MVP (single label only) |

---

**End of design.** After user approval of this file, proceed to `writing-plans` for implementation under `docs/superpowers/plans/`.
