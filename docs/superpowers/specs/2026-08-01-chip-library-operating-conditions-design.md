# Chip-Level Operating-Conditions Library — Design Spec

**Date:** 2026-08-01  
**Status:** Pending user review of written spec
**Parent:** `2026-07-31-agent-pin-part-api-design.md`  
**Goal:** Add a cross-board **chip library** keyed by `part_type` for shared operating-condition sets (I/O + enables groups), while board-level `PartInfo.operating_conditions` becomes an optional per-board override. Board UI / part GET automatically surfaces the merged effective set.

---

## 1. Goals and non-goals

### Goals

1. **Cross-board IC database** — store operating-condition groups once per `part_type`, shared by every board placement that binds that type.
2. **Board-level override** — keep `PartInfo.operating_conditions` on the board sidecar YAML as a **whole-set override** when non-empty (`board > chip`).
3. **Automatic board-side surface** — part read APIs and the web right-panel part view show the **effective** conditions with source `board | chip | none`.
4. **Write split** — default board-path condition CRUD still writes **board override**; explicit `scope=chip` or **promote** writes the chip library.
5. Stay inside architecture invariants:
   - Parse only on server
   - Geometry / netlist read-only to clients
   - Board file never mutated; board overlay YAML + new chip store are the only writable surfaces
   - Desktop coexistence: board sidecar YAML shape remains valid; chip store lives under server `dataRoot` (desktop need not load it)

### Explicit non-goals (this change)

- Alias / synonym table for `part_type` (no independent `chipId`)
- Per-condition id merge across board vs chip (MVP is **whole-set** override only)
- Strict validation that input/output/enable labels exist on a placement’s pins
- Automatic bulk migration of historical board conditions into the chip library
- Standalone full chip-library admin page (board-side bind / edit / promote is enough for MVP)
- Auth, multi-tenant, real-time collab, MCP wrapper
- Desktop OpenBoardView loading or editing the chip library

### Success criteria

1. Write conditions into the chip library for `part_type=X` on board A → board B placement with same `part_type` reads them with `source=chip` after refresh/restart.
2. Non-empty board override on B → only B shows `source=board`; A still `source=chip` with library content.
3. Promote copies board override → chip library; with `clearBoard=true`, B then shows `source=chip`.
4. Empty `part_type` still allows board-local conditions; `scope=chip` / promote without `part_type` → `400 PART_TYPE_REQUIRED`.
5. Existing default board condition CRUD does not regress (still writes board override).
6. Errors remain `{error:{code,message}}`; no absolute server filesystem paths in JSON.

---

## 2. Problem statement (current state)

Today operating conditions live only under:

```text
boardPath.yaml → PartInfos[designator].operating_conditions[]
```

API surface is board-scoped:

```text
/api/v1/boards/:ref/parts/:part/operating-conditions...
```

So the same IC on two boards cannot share I/O / enable groups without duplicating YAML per board. `PartInfo.part_type` already exists but does not key any shared store.

---

## 3. Architecture placement

```text
Agent / Web
    │ REST
    ▼
obv_server routes
    │
    ├─► BoardRegistry (board ref, overlay mutex, board YAML)
    │       PartInfo.part_type
    │       PartInfo.operating_conditions   ← board override
    │
    └─► ChipStore (new; dataRoot/chips/)
            key = part_type
            operating_conditions[]         ← shared library
```

| Concern | Location |
|---------|----------|
| Chip file load/save + sanitize | New `ChipStore` in `obv_server` (or thin `obv_core` helpers if unit tests need pure FS without HTTP); routes own HTTP only |
| Merge policy (board > chip) | Shared helper used by part GET + conditions GET + web payload |
| Board overlay RMW | Existing `withPartOverlay` / `SavePartNetYaml` |
| Chip RMW | ChipStore mutex + atomic write pattern (write temp + rename where practical on Windows) |
| Web right panel | Extend part info / conditions UI to show source + promote / scope actions |

**Lock order (mandatory):** when a request touches both board overlay and chip store, acquire **board overlay mutex first**, then **chip store mutex**. Never reverse.

---

## 4. Identity and data model

### 4.1 Chip identity

- Primary key: **`part_type` string** (trim; case-sensitive; non-empty for library ops).
- Board binding: `PartInfo.part_type` on the placement’s designator.
- Empty `part_type` ⇒ placement has **no chip-library mount**; only board override (if any) applies.

### 4.2 Chip record (on disk)

Path: `{dataRoot}/chips/{sanitize(part_type)}.yaml`

```yaml
part_type: "MP3398E"
note: ""
operating_conditions:
  - id: oc_0001
    name: "PWM dim path"
    inputs: ["PWM"]
    outputs: ["CH1", "CH2"]
    enables: ["EN"]
    note: "EN high"
```

| Field | Rules |
|-------|--------|
| `part_type` | Required; authoritative key inside file |
| `note` | Optional free text; max 2048 (same spirit as condition note) |
| `operating_conditions` | Same `OperatingCondition` schema as board: `id`, `name`, `inputs`, `outputs`, `enables`, `note` with existing normalize limits (`NormalizeOperatingCondition`, max 256 conditions) |

Pin labels in conditions remain **placement-style name/number strings** (status quo). Same `part_type` across boards is assumed to share naming vocabulary; no chip-pin remapping in MVP.

### 4.3 Sanitize rules for filenames

1. Trim `part_type`.
2. Empty → reject `INVALID_PART_TYPE`.
3. Map each char: keep `[A-Za-z0-9._+-]`, else `_`.
4. Result empty, `.`, or `..` → `INVALID_PART_TYPE`.
5. Every non-kept character — including `/`, `\\`, and other path punctuation — is replaced with `_` by step 3. After mapping, still reject empty / `.` / `..`. Do not keep raw path separators in the filename.

**Collision note:** distinct `part_type` values may sanitize to the same filename. On write, if target file exists and its content `part_type` ≠ requested type → `409 CHIP_PATH_COLLISION` (message includes both types). On read by type, open sanitized path and require content `part_type` equality; mismatch → treat as not found / store error, do not return wrong chip.

### 4.4 Board overlay (unchanged keys, new meaning)

```yaml
PartInfos:
  U12:
    part_type: "MP3398E"          # binds chip library
    angle: 0
    pins: { ... }                 # still board-local measurements / show_name
    operating_conditions: []      # empty or absent → inherit chip library
    # non-empty → whole-set board override
```

`pins` / `angle` stay board-local forever. Only conditions participate in chip sharing.

---

## 5. Merge semantics (read path)

For placement `part` on board `boardId`:

```text
pt   = trim(PartInfo.part_type)           # may be empty
board_ocs = PartInfo.operating_conditions # may be empty / missing
chip  = pt empty ? null : ChipStore.Get(pt)
chip_ocs = chip ? chip.operating_conditions : []

if board_ocs non-empty:
  effective = board_ocs
  source = "board"
else if chip_ocs non-empty:
  effective = chip_ocs
  source = "chip"
else:
  effective = []
  source = "none"
```

**Whole-set override only.** No per-`id` merge in MVP (avoids half-applied groups).

Responses that historically returned only board conditions must return **effective** in the legacy field, and expose raw layers for UI/agents:

```json
"partInfo": {
  "part_type": "MP3398E",
  "angle": 0,
  "operating_conditions": [ /* === conditions.effective */ ]
},
"conditions": {
  "source": "board|chip|none",
  "effective": [ ... ],
  "board": [ ... ],
  "chip": [ ... ]
}
```

---

## 6. Write semantics

| Intent | Where | How |
|--------|--------|-----|
| Default board condition CRUD (existing routes, no scope) | Board override | Unchanged path: mutate `PartInfo.operating_conditions`, save board YAML |
| Shared library edit from chip API | Chip store | `/api/v1/chips/:partType/operating-conditions...` |
| Shared library edit from board context | Chip store | Same board routes with `scope=chip` (query or body); requires non-empty `part_type`; creates chip record if missing |
| Promote board → chip | Chip store (+ optional clear board) | `POST .../parts/:part/operating-conditions:promote` body `{ "clearBoard"?: bool }` |
| Bind / rebind type | Board overlay | `PATCH .../parts/:part` with `{ "part_type": "..." }` (empty string clears bind); does **not** move conditions |

Rules:

- Never silently write the chip store on default board CRUD.
- `scope=chip` without `part_type` → `400 PART_TYPE_REQUIRED`.
- Promote with empty board override → `400 BAD_REQUEST` with message that there is nothing to promote.
- Promote with empty `part_type` → `400 PART_TYPE_REQUIRED`.
- Deleting a chip record does **not** clear board overrides on any board.
- Changing `part_type` does not auto-delete old chip data or board override.

---

## 7. HTTP API

### 7.1 Chip library

```text
GET    /api/v1/chips
GET    /api/v1/chips/:partType
PUT    /api/v1/chips/:partType
DELETE /api/v1/chips/:partType

GET    /api/v1/chips/:partType/operating-conditions
POST   /api/v1/chips/:partType/operating-conditions
GET    /api/v1/chips/:partType/operating-conditions/:condId
PUT    /api/v1/chips/:partType/operating-conditions/:condId
DELETE /api/v1/chips/:partType/operating-conditions/:condId
PUT    /api/v1/chips/:partType/operating-conditions
```

- `:partType` is URL-encoded; server decodes + trims.
- **List 200:** `{ "chips": [ { "part_type", "conditionCount", "note"? } ] }` sorted by `part_type`.
- **Get chip 200:** `{ "part_type", "note", "operating_conditions": [ ... ] }`.
- **PUT chip:** upsert metadata; if body includes `operating_conditions`, replace whole condition array (same normalize rules); if omitted, leave existing conditions.
- **DELETE chip:** remove file; 404 if missing (no silent success).
- Condition CRUD mirrors board agent API: allocate `oc_NNNN` if id empty on POST; 409 on id conflict; max 256; normalize via existing helper.
- Register more-specific `.../operating-conditions/:condId` routes before collection routes.

### 7.2 Board part read (extended)

**`GET /api/v1/boards/:ref/parts/:part`**

Add `conditions` object as in §5; set `partInfo.operating_conditions` to **effective**.

**`GET /api/v1/boards/:ref/parts/:part/operating-conditions`**

```json
{
  "boardId": "...",
  "part": "U12",
  "part_type": "MP3398E",
  "source": "chip",
  "operating_conditions": [ /* effective */ ],
  "board": [ ... ],
  "chip": [ ... ]
}
```

Always return `source`, `operating_conditions` (effective), `board`, and `chip`. No `raw=` query in MVP.

**`GET .../operating-conditions/:condId`**

Lookup in **effective** set by id; 404 if not in effective. (Does not invent cross-layer id search beyond effective.)

### 7.3 Board part write (split)

Existing:

```text
POST|PUT|DELETE /api/v1/boards/:ref/parts/:part/operating-conditions...
PUT             /api/v1/boards/:ref/parts/:part/operating-conditions
```

- Default: board override only (backward compatible).
- `scope=chip` as query parameter **or** JSON field on body: write chip library instead; response includes `"scope":"chip"`.
- Single-condition GET still reads effective (not scope-sensitive).

New:

```text
POST /api/v1/boards/:ref/parts/:part/operating-conditions:promote
```

Body:

```json
{ "clearBoard": false }
```

Behavior:

1. Lock board overlay, load PartInfo.
2. Require non-empty `part_type` and non-empty board `operating_conditions`.
3. Lock chip store; upsert chip record with those conditions (replace chip conditions entirely with the promoted set).
4. If `clearBoard` true, clear board `operating_conditions` and save board YAML.
5. Return `{ part_type, source, operating_conditions, board, chip }` post-state.

New (bind):

```text
PATCH /api/v1/boards/:ref/parts/:part
```

Body may include `{ "part_type": "MP3398E" }` (empty string clears). Only updates overlay `part_type` (and optionally other future safe fields); **must not** wipe pins/conditions unless explicitly specified later. 404 if part not on board.

### 7.4 Errors (additive)

| HTTP | code | When |
|------|------|------|
| 400 | `INVALID_PART_TYPE` | Empty/illegal part_type for chip ops |
| 400 | `PART_TYPE_REQUIRED` | scope=chip / promote without binding |
| 400 | `BAD_REQUEST` | Malformed JSON, normalize failure, promote with empty board set |
| 404 | `CHIP_NOT_FOUND` | Chip library miss |
| 404 | `CONDITION_NOT_FOUND` | Same as today |
| 404 | `PART_NOT_FOUND` / `NOT_FOUND` | Existing |
| 409 | `CONDITION_ID_CONFLICT` | Existing |
| 409 | `CHIP_PATH_COLLISION` | sanitize path occupied by different part_type |
| 500 | `CHIP_STORE_FAILED` | Chip FS read/write failure |
| 500 | `OVERLAY_*` | Existing board overlay errors |

---

## 8. Persistence and concurrency

### 8.1 ChipStore

- Root: `config.dataRoot / "chips"`; create on server start or first use.
- One YAML file per chip; list = directory scan of `*.yaml`; skip unreadable/invalid files with log, do not fail whole list.
- Write path: serialize full record → write `{file}.tmp` → rename over target (best-effort on Windows; if rename replace fails, document fallback write-in-place + verify reload).
- After write: reload file and compare critical fields (part_type, conditions size/ids) similar to `SavePartNetYaml` discipline where practical.

### 8.2 Concurrency

- Process-wide `ChipStore` mutex for MVP (library is small).
- Board overlay: existing per-board `OverlayMutex`.
- Lock order: **board then chip** when both needed.
- last-write-wins; no OT/CRDT.

### 8.3 Desktop coexistence

- Board sidecar YAML continues to store per-board override conditions; desktop readers that already understand `operating_conditions` keep working.
- Chip library is **server `dataRoot` only**; desktop OpenBoardView is not required to read it.
- Bulk `PUT /overlays` still replaces board `partInfos` wholesale — clients that omit `operating_conditions` still wipe **board override** only; chip library untouched.

---

## 9. Web UI (board-side automatic surface)

MVP changes on the existing right-hand part / conditions area (no new top-level admin app):

1. Show editable **`part_type`** binding for the selected part.
2. List **effective** operating conditions with a source badge: `共享(chip)` / `本板(board)` / `无`.
3. Edit actions:
   - Default save targets the **current source** (chip → chip store; board → override; none + has part_type → prefer chip; none + no part_type → board).
   - Explicit menu: 「仅本板」 / 「写入共享库」 mapping to board write vs `scope=chip`.
4. 「提升到芯片库」 when board override non-empty → promote API; optional checkbox clear board override.
5. If `part_type` empty, disable shared write / promote with short hint to bind first.

Out of scope for MVP: dedicated multi-chip browser page, bulk import, diff viewer across boards.

---

## 10. Migration / compatibility

- **No automatic bulk promote** of historical board conditions (avoids polluting library with wrong/empty `part_type`).
- Old agents reading `partInfo.operating_conditions` receive **effective** — correct for “what applies now”.
- Old agents writing board condition routes keep writing **override** only — no surprise global publishes.
- New power: chip REST, `scope=chip`, promote, `conditions.source`, UI badge.

Optional later (not this spec): report tool “boards with override for part_type=X”.

---

## 11. Testing / verification

Project still has no formal large framework; follow existing patterns:

1. **Core/store unit-style** in `obv_core_tests` (or small server-side test binary): sanitize, YAML round-trip, path collision, merge helper truth table (board empty/non-empty × chip empty/non-empty).
2. **API smoke script** (extend `scripts/test_agent_api.py` or sibling):
   - create chip via API
   - two board placements same `part_type` (or one real board + synthetic second path if only one fixture — minimum: same board two parts sharing type, **or** write chip then open second library board if available)
   - board override isolation
   - promote + clearBoard
   - default board CRUD does not mutate chip file
3. **Manual UI**: bind type, see badge, edit shared, open another board with same type, confirm inheritance; set override, confirm isolation.

---

## 12. Implementation sketch (for planning, not binding task list)

1. `ChipStore` + sanitize + YAML IO + mutex  
2. Merge helper + extend part GET / conditions GET JSON  
3. Chip REST routes  
4. Board write `scope=chip` + promote + PATCH part_type  
5. Web right-panel source badge + bind + promote  
6. Smoke script + docs cross-links  

Prefer reusing `OperatingCondition`, `NormalizeOperatingCondition`, `AllocateConditionId` without forking a second condition type.

---

## 13. Approach decision record

| Option | Summary | Decision |
|--------|---------|----------|
| A. `dataRoot/chips/` file library + board merge | YAML per `part_type`; board override whole-set | **Accepted** |
| B. SQLite chips table | Stronger query; extra schema | Rejected for MVP footprint |
| C. Chip API only, no board merge | Thinnest; fails auto-surface | Rejected |

User choices locked in brainstorming:

- Identity: `part_type` string  
- Ownership: chip library primary, board may override  
- Pin vocabulary: placement-style labels (status quo)  
- MVP scope: backend + board-side automatic surface  

---

## 14. Open points intentionally closed

| Topic | Resolution |
|-------|------------|
| Per-id merge vs whole-set | Whole-set override |
| Default board write target | Board override (compat) |
| Empty part_type board conditions | Allowed; local only |
| Auto migration | None |
| Standalone chip admin page | Deferred |

---

**End of design.** After user approval of this file, proceed to `writing-plans` for a phased implementation plan under `docs/superpowers/plans/`.
