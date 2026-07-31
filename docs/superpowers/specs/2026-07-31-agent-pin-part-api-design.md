# Agent Pin / Part Operating-Conditions API — Design Spec

**Date:** 2026-07-31  
**Status:** Approved for implementation planning  
**Parent:** `2026-07-27-web-server-refactor-design.md`  
**Goal:** REST tool surface for agents: (1) resolve a part pin to a full snapshot including net-propagated measurements; (2) CRUD named operating-condition groups on a part via overlay `PartInfo`.

---

## 1. Goals and non-goals

### Goals

1. **Pin resolve tool** — given board + part + pin, return identity/geometry, local measurement fields, effective (propagated) values, and propagation sources.
2. **Part operating-conditions CRUD** — each part may store multiple named groups of `{inputs, outputs, enables, note}` describing normal working I/O relationships (one IC can have several output groups with different EN/inputs).
3. **Agent-friendly board addressing** — accept existing `boardId` (sha256 hex) or unique library `sourceName` / relative path under `boardRoot`.
4. Stay inside existing architecture invariants:
   - Parse only on server
   - Geometry / netlist read-only to clients
   - **Only overlay is writable**
   - One board = one `boardId`
   - Desktop OpenBoardView coexistence via shared `obv_core` / YAML PartInfos

### Explicit non-goals (this change)

- Standard Model Context Protocol (stdio/SSE) server — REST only; a future MCP wrapper may call these endpoints.
- Auth / ACL / multi-tenant isolation.
- Real-time collaboration or OT/CRDT.
- Changing browser Canvas rendering or InfoPane UX (optional later consumers).
- Replacing bulk `PUT /overlays` or freeform annotation APIs.
- Uploading boards (library mode remains `boardRoot` scan).

### Success criteria

1. `GET .../parts/{part}/pins/{pin}` returns local + effective diode/voltage/ohm/ohm_black with correct source priority **overlay > board file > same-net propagation**, matching `web/src/scene/pinValues.ts`.
2. Operating-condition groups survive server restart via existing board-sidecar YAML (`PartInfos`).
3. Desktop / web can still load the same YAML without breaking when unknown keys appear (or after dual-write of known keys only — see §5).
4. Ambiguous board path refs return **409**; missing board/part/pin/condition return **404** with unified `{error:{code,message}}`.

---

## 2. Architecture placement

```
Agent / tool client
        │ REST/JSON
        ▼
obv_server routes  ──► BoardRegistry (:ref → boardId + path)
        │                    │
        │                    ▼
        │              BoardSnapshot (geometry/netlist read-only)
        │                    │
        └──────────► Overlay YAML load/save (PartInfos writable)
                     + pin resolve helpers in obv_core
```

| Concern | Location |
|---------|----------|
| Board ref resolution | `obv_server` (`BoardRegistry`) |
| Pin lookup + net propagation | **New** shared helpers in `obv_core` (server-side; same rules as frontend `pinValues.ts`) |
| Operating conditions persistence | Existing overlay YAML via `PartInfo` extension |
| HTTP surface | New routes under `/api/v1/boards/:ref/parts/...` |

**Do not** reimplement propagation only in the agent client. Server is source of truth for the tool response.

---

## 3. Resource model and board ref

### 3.1 Path shape (Approach A)

```
GET    /api/v1/boards/:ref/parts/:part/pins/:pin
GET    /api/v1/boards/:ref/parts/:part
GET    /api/v1/boards/:ref/parts/:part/operating-conditions
POST   /api/v1/boards/:ref/parts/:part/operating-conditions
GET    /api/v1/boards/:ref/parts/:part/operating-conditions/:condId
PUT    /api/v1/boards/:ref/parts/:part/operating-conditions/:condId
DELETE /api/v1/boards/:ref/parts/:part/operating-conditions/:condId
PUT    /api/v1/boards/:ref/parts/:part/operating-conditions   # full replace
```

`:part` and `:pin` and `:condId` are URL path segments; encode reserved characters (`encodeURIComponent`). Comparison is **case-sensitive**, matching board JSON `component.name` / pin keys.

### 3.2 `:ref` resolution order

1. If `:ref` matches `^[0-9a-f]{64}$` → treat as `boardId` (existing rule).
2. Else treat as library relative path or file name under `boardRoot`:
   - Exact relative path match → that board
   - Else unique basename match among scanned entries → that board
   - Zero matches → `404 NOT_FOUND`
   - Multiple basename matches → `409 BOARD_REF_AMBIGUOUS` (message lists candidates: id + path, **no absolute server path** beyond library-relative)
3. Successful responses that need board identity include canonical `boardId` and library-relative `sourceName` / `path` where already exposed by list API.

**Invariant:** responses never leak absolute server filesystem paths (`sourceName` policy from prior reviews).

### 3.3 Concurrency

Reuse per-board overlay mutex (`registry.OverlayMutex(id)`) for all read-modify-write condition endpoints, same as existing overlay PUT / annotations.

---

## 4. Pin full snapshot (feature 1)

### 4.1 Endpoint

`GET /api/v1/boards/:ref/parts/:part/pins/:pin`

### 4.2 Pin matching

Among `board.pins` where `component == part` (string equality):

1. `pin.name == :pin`
2. else `pin.number == :pin`
3. else `pin.id == :pin`
4. else overlay key form used by desktop / frontend: `name || number || id` equals `:pin`

Zero matches → `404 PIN_NOT_FOUND`.  
Multiple matches after the same priority tier → pick **first in board pin array order** (stable, document in response as-is; no 409 for pins).

### 4.3 Value resolution (per mode: `diode` | `voltage` | `ohm` | `ohm_black`)

Aligned with `web/src/scene/pinValues.ts`:

1. **local.overlay** — `partInfos[part].pins[overlayKey][mode]` if non-empty after trim  
2. **local.board** — board JSON pin field if non-empty  
3. **local** = overlay if set else board  
4. **effective** = local if set, else first non-empty **local** among pins with the same `netId` (board pin iteration order), excluding empty/`null` netId  
5. **source** for effective when propagated: `{ component, pinKey, pinId }` of the seed pin; when local, source is self  

Also return:

- `note` / `show_name` / `voltage_flag` from overlay pin info when present (overlay overrides display name semantics already used by UI; include both board `show_name` and overlay override if any)
- Net identity: `netId`, net `name` if present in `board.nets`

GND/NC do **not** get special HTTP behavior beyond existing type/net fields; propagation still runs if `netId` is set (frontend highlight rules are UI-only).

### 4.4 Response schema (200)

```json
{
  "boardId": "<64 hex>",
  "sourceName": "<library-relative or public name>",
  "part": "U12",
  "pinKey": "1",
  "pin": {
    "id": "...",
    "component": "U12",
    "number": "1",
    "name": "...",
    "show_name": "...",
    "type": "component",
    "netId": 42,
    "netName": "N1234",
    "side": "top",
    "pos": { "x": 0, "y": 0 },
    "shape": "circle",
    "diameter": 0,
    "size": { "x": 0, "y": 0 },
    "angle": 0
  },
  "measurements": {
    "diode": {
      "local": { "value": "", "source": "none" },
      "effective": {
        "value": "0.7",
        "source": "propagated",
        "from": { "component": "R1", "pinKey": "2", "pinId": "..." }
      },
      "board": "",
      "overlay": ""
    },
    "voltage": { "...": "same shape" },
    "ohm": { "...": "same shape" },
    "ohm_black": { "...": "same shape" }
  },
  "overlay": {
    "note": "",
    "show_name": "",
    "voltage_flag": "unknown"
  }
}
```

`local.source` ∈ `overlay` | `board` | `none`.  
`effective.source` ∈ `overlay` | `board` | `propagated` | `none`.  
`from` only when `effective.source === "propagated"`.

### 4.5 Related read: part summary

`GET /api/v1/boards/:ref/parts/:part`

Returns:

- Component record from board JSON (name, side, mount, type, mfgcode, center, outline, pins[])
- Resolved pin list (id, number, name, type, netId, netName) — light, no full measurements
- `partInfo` overlay subset: `part_type`, `angle`, `operating_conditions`
- `404 PART_NOT_FOUND` if no component with that name

---

## 5. Operating conditions (feature 2)

### 5.1 Data model (extends `PartInfo`)

Stored in existing overlay YAML under `PartInfos[partName]`:

```yaml
PartInfos:
  U12:
    part_type: ...
    angle: 0
    pins: { ... }          # unchanged
    operating_conditions:  # NEW optional array
      - id: oc_01
        name: "UART0 TX path"
        inputs: ["RXD0"]
        outputs: ["TXD0"]
        enables: ["UART_EN"]
        note: "EN high, VCC 3.3"
```

JSON shape for API:

```json
{
  "id": "oc_01",
  "name": "UART0 TX path",
  "inputs": ["RXD0"],
  "outputs": ["TXD0"],
  "enables": ["UART_EN"],
  "note": "EN high, VCC 3.3"
}
```

| Field | Type | Rules |
|-------|------|--------|
| `id` | string | Stable within part; server-generated on create if omitted (`oc_` + short unique suffix); client may supply on create if unique |
| `name` | string | Display label; may be empty; **not** required unique |
| `inputs` | string[] | Pin names/numbers as author labels (same vocabulary as board pin name/number); empty array allowed |
| `outputs` | string[] | Same |
| `enables` | string[] | Same (user wording: `en`) |
| `note` | string | Free text |

**MVP validation:**

- Arrays must be arrays of strings (trim entries; drop empty strings).
- **Do not** require referenced pins to exist on the part (authors may document external nets / future silkscreen names). Optional later: `?strictPins=1` warning mode — **out of scope**.
- Empty group (all arrays empty and empty note/name) is allowed but discouraged; still accepted.
- Max lengths (server-enforced): `id` ≤ 64, `name`/`note` ≤ 2048, each pin label ≤ 128, each array ≤ 256 entries, conditions per part ≤ 256.

### 5.2 YAML / desktop compatibility

- Desktop `annotations` YAML readers that ignore unknown keys continue to work.
- Implement write/read of `operating_conditions` in `obv_core` overlay load/save paths used by the server (same code path as `PartInfos` / `SavePinInfos` or adjacent structured extension).
- If desktop YAML serializer cannot yet round-trip the array, **server-side extension must still round-trip** for web/agent; document any desktop gap. Prefer extending the shared YAML schema so desktop does not strip the field on next desktop save (risk: desktop SavePinInfos rewrite dropping unknown keys). **Mitigation:** verify desktop save path; if it drops unknowns, fix shared annotations write to preserve `operating_conditions` (required for coexistence invariant).

### 5.3 Endpoints

| Method | Path | Behavior |
|--------|------|----------|
| GET | `.../operating-conditions` | `{ boardId, part, operating_conditions: Condition[] }` |
| POST | `.../operating-conditions` | Body: condition without required `id`; 201 + created object |
| GET | `.../operating-conditions/:condId` | Single condition |
| PUT | `.../operating-conditions/:condId` | Replace one condition (`id` in body must match path or be omitted) |
| DELETE | `.../operating-conditions/:condId` | Remove; **404 if missing** (no silent success) |
| PUT | `.../operating-conditions` | Body: `{ operating_conditions: Condition[] }` full replace of the array only (does not wipe `pins` / `part_type`) |

All writes:

1. Lock overlay mutex  
2. Load overlay  
3. Ensure `partInfos[part]` exists (create empty PartInfo if missing **only when part exists on board**)  
4. Mutate `operating_conditions`  
5. Save YAML with existing verify-after-write discipline  
6. Reload + return persisted state  

If `part` not on board → `404 PART_NOT_FOUND` (do not create orphan PartInfo for typos).  
If condition id missing → `404 CONDITION_NOT_FOUND`.  
Duplicate id on POST → `409 CONDITION_ID_CONFLICT`.

### 5.4 Interaction with bulk overlay PUT

Existing `PUT /api/v1/boards/:id/overlays` continues to replace `partInfos` / `netInfos` wholesale. Clients that PUT overlays **must include** `operating_conditions` if they had any, or they will wipe them — same class of risk as wiping pin notes today. Document this. No change to annotation SQLite paths.

---

## 6. Errors

Unified body (existing convention):

```json
{ "error": { "code": "NOT_FOUND", "message": "human readable" } }
```

| HTTP | code | When |
|------|------|------|
| 400 | `BAD_REQUEST` | Malformed JSON, wrong types, over limits |
| 400 | `INVALID_BOARD_ID` | Existing sha256 validation failures when ref looks like id |
| 404 | `NOT_FOUND` | Board missing |
| 404 | `PART_NOT_FOUND` | Component name not on board |
| 404 | `PIN_NOT_FOUND` | Pin not under part |
| 404 | `CONDITION_NOT_FOUND` | Condition id missing |
| 409 | `BOARD_REF_AMBIGUOUS` | Path/name matches multiple boards |
| 409 | `CONDITION_ID_CONFLICT` | Create with existing id |
| 500 | `OVERLAY_LOAD_FAILED` / `OVERLAY_SAVE_FAILED` | Existing overlay I/O failures |
| 500 | `PARSE_FAILED` | Board parse failed when geometry required |

Pin resolve and part GET require a successfully parsed board snapshot. Condition **read** may load overlay without full parse **only if** part existence can still be checked — **decision: require parse** for all these routes so part membership is authoritative.

---

## 7. Implementation sketch (not a plan)

1. **`obv_core`**
   - Pin overlay key helper (shared semantics with frontend `pinOverlayKey`)
   - `ResolvePinMeasurements(snapshot, overlay, part, pinRef) → result | not_found`
   - `operating_conditions` on `PartInfo`: load/save YAML + JSON apply/export
2. **`obv_server`**
   - `ResolveBoardRef(ref) → boardId | ambiguous | not_found`
   - Register part/pin/condition routes; reuse overlay lock + error helpers
3. **Frontend (optional, not required for agent MVP)**
   - No UI required; may later show conditions in InfoPane
4. **Docs**
   - This spec; parent design may link under “Agent tool API”

No automated test suite required by project convention; verification via manual agent-style HTTP checks and review gates.

---

## 8. Architecture invariant checklist

| Invariant | How this design upholds it |
|-----------|----------------------------|
| Parse only on server | Pin resolve uses server snapshot only |
| Geometry/netlist read-only | Agent cannot PATCH pin coordinates/nets |
| Only overlay writable | Conditions live in PartInfo YAML |
| One board = one boardId | Responses always echo canonical boardId |
| Desktop coexistence | Extend shared PartInfo YAML; preserve on desktop save |

---

## 9. Open follow-ups (out of scope)

- Real MCP protocol adapter wrapping these REST tools  
- Strict pin-name validation / autocomplete for condition arrays  
- Web UI editor for operating conditions  
- Propagation of `note` / `voltage_flag` across nets (not in current UI model)  
- Authn for LAN agents  

---

## 10. Approval record

| Section | Status |
|---------|--------|
| Resource model + board ref | Approved 2026-07-31 |
| Pin response schema | Approved 2026-07-31 |
| Operating-conditions CRUD | Approved 2026-07-31 |
| Errors / compatibility / non-goals | Approved 2026-07-31 |
| Approach | A — routes under `/api/v1/boards/:ref/parts/...` |
