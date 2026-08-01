# Part Pin Grid (row/col inference) — Design Spec

**Date:** 2026-08-01  
**Status:** Approved for implementation  
**Parent:** `2026-07-31-agent-pin-part-api-design.md`  
**Goal:** Read-only agent API that maps a part’s pin board coordinates to a regular matrix of `(row, col)` indices for layout understanding (rename/screenshot helpers). Does **not** write overlay or mutate geometry.

---

## 1. Goals and non-goals

### Goals

1. Infer a pin grid from pin `(x,y)` positions under one part.
2. Expose `GET /api/v1/boards/:ref/parts/:part/pin-grid`.
3. Return per-pin `row`/`col`, pitch estimates, kind, fill ratio, warnings.
4. Keep geometry/netlist read-only; no overlay writes.

### Non-goals

- Auto `show_name` / silk rename  
- QFP four-edge specialized model (only `sparse` warning)  
- Hungarian assignment refinement  
- PCA/part-angle rotation (MVP: board coordinates only)  
- Web UI visualization  

### Success criteria

1. Regular R×C synthetic / real grid parts get stable `(r,c)` with `fillRatio` near 1.  
2. Single-row / single-column classified as `row` / `column`.  
3. Missing part → 404; part with zero pins → 400 `PART_NO_PINS`.  
4. Unit tests for clustering; HTTP case in `scripts/test_agent_api.py`.

---

## 2. Algorithm (MVP)

1. Collect all pins belonging to `part` (`component->name == part`).
2. If empty → `PART_NO_PINS`. If one pin → `kind=single`, `rows=1,cols=1`, `(0,0)`.
3. **1D projection clustering** on Y → rows; on X → cols:
   - Sort values; gaps between consecutive unique-ish samples.
   - `pitch ≈ median` of positive adjacent gaps (fallback: max-min if single gap set empty).
   - Split when gap `> 0.6 * pitch` (min pitch epsilon `1e-6`).
   - Cluster center = median of members.
4. Sort row clusters by center Y ascending → `row 0 = min_y`.
5. Sort col clusters by center X ascending → `col 0 = min_x`.
6. Assign each pin to nearest row center and col center (Euclidean on 1D).
7. `pitchX` / `pitchY` = median spacing between consecutive cluster centers (0 if count&lt;2).
8. `origin` = `{ x: col0.center, y: row0.center }`.
9. `fillRatio = pinCount / (rows * cols)` (guard div0).
10. `kind`:
    - `single` if pins==1  
    - `row` if rows==1 && cols&gt;1  
    - `column` if cols==1 && rows&gt;1  
    - `grid` if rows&gt;1 && cols&gt;1 && fillRatio ≥ 0.5  
    - `sparse` if rows&gt;1 && cols&gt;1 && fillRatio &lt; 0.5 (warning: possible peripheral package)  
11. Collision: two pins same `(r,c)` → keep both with same indices; add warning `duplicate_cells`.

Display labels on pins: same priority as elsewhere (`PinDisplayLabel` with overlay if loaded).

---

## 3. HTTP

```
GET /api/v1/boards/:ref/parts/:part/pin-grid
```

**200 JSON** — see design discussion; fields:

| field | meaning |
|-------|---------|
| boardId, sourceName, part | identity |
| kind | single\|row\|column\|grid\|sparse |
| rows, cols | cluster counts |
| pitchX, pitchY | median center spacing |
| origin | {x,y} col0/row0 centers |
| row0 | `"min_y"` |
| col0 | `"min_x"` |
| fillRatio | pins/(rows*cols) |
| warnings | string[] |
| pins[] | key,id,number,name,displayLabel,board{x,y},row,col |

Errors: `NOT_FOUND`, `PART_NOT_FOUND`, `PART_NO_PINS`, `PARSE_FAILED`, `BOARD_REF_AMBIGUOUS` — unified `{error:{code,message}}`.

---

## 4. Implementation placement

- `obv_core/include/obv_core/pin_grid.h` + `src/pin_grid.cpp` — pure inference + JSON export  
- `obv_server/routes.cpp` — GET route (load overlay optional for displayLabel)  
- Tests: unit in `obv_core_tests`; HTTP in `scripts/test_agent_api.py`

---

## 5. Approval

Approved 2026-08-01: read-only pin-grid via 1D projection clustering; no rename side effects.
