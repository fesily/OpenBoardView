# Board Part-Layout Fingerprint API — Design Spec

**Date:** 2026-08-03  
**Status:** Approved for implementation  
**Goal:** Read-only agent API listing per-part geometric fingerprints (`name`, `center`, `pinCount`) so multiple board files of the same PCB can be aligned when part names differ.

---

## 1. Goals / non-goals

### Goals

1. `GET /api/v1/boards/:ref/part-layout` returns all (filtered) parts with stable center + pinCount.
2. Optional `minPins` query filters dummy / single-pad noise.
3. Centers match existing board JSON component center semantics (`deriveCompGeom`).
4. Read-only; no overlay writes; no cross-board matching in this MVP.

### Non-goals

- Server-side boardA↔boardB matching  
- Overlay sync / rename propagation  
- Pin-level fingerprint in this endpoint (use pin-grid per part later)  
- Using deprecated `part.angle`

### Success criteria

1. Endpoint 200 on sample boards; `partCount == parts.length`.  
2. `minPins=2` excludes 0–1 pin parts.  
3. HTTP suite case green.  
4. Same board re-fetch yields stable sort order.

---

## 2. HTTP

```
GET /api/v1/boards/:ref/part-layout?minPins=1
```

| query | default | rules |
|-------|---------|--------|
| `minPins` | `1` | integer ≥ 0; part kept iff `pinCount >= minPins`. `0` = include empty. Invalid → 400 |

**200 JSON:**

```json
{
  "boardId": "<64 hex>",
  "sourceName": "ha-jcl-main-01-track.bvr",
  "minPins": 1,
  "partCount": 200,
  "bounds": { "minX": 0, "minY": 0, "maxX": 0, "maxY": 0 },
  "parts": [
    { "name": "U12", "center": { "x": 1.0, "y": 2.0 }, "pinCount": 48 }
  ]
}
```

- `bounds`: axis-aligned box of **included** part centers (empty parts list → zeros).  
- Sort: ascending `center.y`, then `center.x`, then `name` (lexicographic).  
- Errors: standard board ref / parse (`NOT_FOUND`, `PARSE_FAILED`, `BOARD_REF_AMBIGUOUS`, `BAD_REQUEST`).

---

## 3. Implementation

- Core helper preferred: `ExportPartLayoutJson(board, boardId, sourceName, minPins)` in `obv_core` (or inline in routes if tiny).  
- Center: reuse same logic as `board_json` component center (`deriveCompGeom` / pin bbox midpoint).  
- `pinCount`: count pins with `component->name == part` (or `component.pins.size()` if authoritative).  
- Route next to other agent board GETs.  
- Tests: unit optional; HTTP in `scripts/test_agent_api.py` (`part_layout_shape`, `minPins` filter).

---

## 4. Agent usage (informative)

1. Fetch `part-layout` for board A and B.  
2. Nearest-neighbor / RANSAC on centers (optionally filter by pinCount).  
3. Produce correspondence map; then call pin/net overlay APIs to sync.

---

## 5. Approval

Approved 2026-08-03: fingerprint only + `minPins` filter.
