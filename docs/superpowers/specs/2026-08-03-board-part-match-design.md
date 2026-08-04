# Cross-Board Part Match (rot + region, no offset) — Design Spec

**Date:** 2026-08-03  
**Status:** Approved for implementation  
**Parent:** `2026-08-03-part-layout-fingerprint-design.md`  
**Goal:** Stateless API to match parts across two board files of related PCBs when names differ, using per-board rotation and region masks (half-board compare). **No manual offset in MVP** — filtered-region centroids are auto-aligned on a shared canvas.

---

## 1. Goals / non-goals

### Goals

1. `POST /api/v1/boards/match-parts` compares two boards.
2. Each board: `ref`, `rot` ∈ {0,90,180,270}, `region` ∈ {all,left,right,top,bottom}.
3. Optional `split` ∈ {none,vertical,horizontal} (documentation / validation hint with region).
4. Auto-align: after rot + region filter, translate so **centroids of remaining parts** coincide.
5. Match by nearest neighbor with equal `pinCount` and `dist ≤ maxDist`.
6. Read-only.

### Non-goals

- Manual `offset` (deferred)  
- Overlay sync  
- Continuous rotation angles  
- Session/canvas persistence  
- Using `part.angle`

### Success criteria

1. HTTP 200 with matches array on hac-cpu-10 vs hac-cpu-20 with rot=90 + region.  
2. Invalid rot/region → 400.  
3. Unknown board → 404.  
4. Test case in `scripts/test_agent_api.py`.

---

## 2. Coordinate pipeline (per board)

1. Load part fingerprints (same as part-layout: name, center, pinCount), apply `minPins`.
2. Board frame bounds = AABB of those centers (if empty after filter later, error).
3. **Rotate** around board bounds center `C`:
   - 0: (x,y)
   - 90 CW: (cx + (y-cy), cy - (x-cx))  i.e. relative (x',y')=(y,-x) then +C  
   - 180: (cx - (x-cx), cy - (y-cy))
   - 270 CW (=90 CCW): relative (-y,x)
4. Recompute AABB of rotated centers → `rotatedBounds`.
5. **Region mask** on rotatedBounds midlines:
   - all: keep all  
   - left: x ≤ midX  
   - right: x ≥ midX  
   - bottom: y ≤ midY  
   - top: y ≥ midY  
   (board Y as exported; document in response `regionAxes`)
6. If no parts remain → 400 `EMPTY_REGION`.
7. **Auto-align (both boards):** let `G` = centroid of board A remaining parts; for each board translate by `-centroid` so both centroids at origin (shared canvas). Equivalently: translate B by `centroidA - centroidB` after both in rotated space with A unshifted — implementation: put both at origin via subtracting own centroid.

## 3. Matching

- Greedy: sort A parts by pinCount descending; each takes nearest unused B part with **same pinCount** and Euclidean dist ≤ `maxDist` (in aligned canvas units = board units).
- Default `maxDist`: 50 (query/body override; range 0.1 … 1e6).
- Default `minPins`: 2.

## 4. Request / response

```http
POST /api/v1/boards/match-parts
Content-Type: application/json
```

```json
{
  "a": { "ref": "hac-cpu-10-track.bvr", "rot": 90, "region": "left" },
  "b": { "ref": "hac-cpu-20-track.bvr", "rot": 0, "region": "left" },
  "split": "vertical",
  "minPins": 2,
  "maxDist": 50
}
```

Defaults if omitted: `rot=0`, `region=all`, `split=none`, `minPins=2`, `maxDist=50`.

**200:**

```json
{
  "a": { "boardId": "...", "sourceName": "...", "rot": 90, "region": "left", "partCount": 100 },
  "b": { "boardId": "...", "sourceName": "...", "rot": 0, "region": "left", "partCount": 100 },
  "split": "vertical",
  "minPins": 2,
  "maxDist": 50,
  "align": "region_centroid",
  "matchCount": 40,
  "matches": [
    {
      "partA": "N14615",
      "partB": "N877",
      "pinCount": 2,
      "dist": 1.12,
      "canvasA": { "x": 0, "y": 0 },
      "canvasB": { "x": 0, "y": 0 }
    }
  ],
  "unmatchedA": ["..."],
  "unmatchedB": ["..."]
}
```

Errors: `BAD_REQUEST`, `NOT_FOUND`, `PARSE_FAILED`, `EMPTY_REGION`, `BOARD_REF_AMBIGUOUS`.

## 5. Implementation placement

- `obv_core`: `MatchBoardParts(...)` using layout rows (reuse ExportPartLayout logic / shared helper).  
- Prefer extract `CollectPartFingerprints(board, minPins)` used by ExportPartLayoutJson and match.  
- `routes.cpp`: POST handler, parse JSON with MiniJson.  
- Tests: synthetic unit optional; HTTP on two library boards if present, else skip-friendly shape test with same board rot0/all self-match.

## 6. Approval

Approved 2026-08-03: match API with rot + region; **no offset**; centroid auto-align.
