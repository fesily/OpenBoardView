# Part Pin Grid v2 — Classification + PCA + Peripheral

**Date:** 2026-08-01  
**Status:** Approved for implementation (user: continue; **do not use `part.angle`**)  
**Parent:** `2026-08-01-part-pin-grid-design.md`  
**Goal:** Stronger generic pin layout inference: axis alignment via **PCA only**, package classification, peripheral (QFP-like) side indexing, keep grid path for dense matrices. Read-only.

---

## 1. Goals

1. Align pin cloud with **PCA principal axis** (not `PartInfo.angle` — deprecated / unused).
2. Classify layout: `single | row | column | grid | peripheral | unordered`.
3. **Peripheral packages:** assign each pin `{side, index}` (top/bottom/left/right/thermal).
4. **Grid packages:** keep improved 1D clustering `(row, col)` in local axes.
5. Always emit board-space coordinates; also emit local coords after PCA for debugging.
6. Backward compatible JSON: keep `row`/`col`/`kind`; extend fields.

## 2. Non-goals

- Using `part.angle` / overlay angle for rotation  
- Auto `show_name` writes  
- Hungarian refinement (optional later)  
- ML / OCR  

## 3. Pipeline

```
pins board (x,y)
  → PCA: centroid + principal angle θ (if ≥3 pins and anisotropy strong enough)
  → local (lx, ly) = rotate(board - centroid, -θ)
  → classify(local pins)
  → solver:
       single/row/column/grid → 1D cluster on lx/ly → (row,col)
       peripheral → edge assignment + per-side 1D index + thermal
       unordered → no matrix; empty row/col or -1; warning
  → export
```

### 3.1 PCA rotation rules

- Pins &lt; 3: θ = 0  
- Compute 2×2 covariance of (x,y); θ = atan2 of principal eigenvector  
- Snap θ to nearest 0/90° if within 15° of axis (numerical stability for already-aligned parts)  
- **Never read `part.angle`**

Expose: `rotationDeg` (applied PCA angle in degrees), `centroid {x,y}`.

### 3.2 Classification

After local transform:

1. Run grid clustering on (lx, ly) → provisional rows/cols/fillRatio  
2. Compute bbox of local pins; margin = max(0.15 * min(w,h), 0.5 * min(pitchX,pitchY) if pitch&gt;0 else 0.15*min(w,h))  
3. `edgeFrac` = fraction of pins within `margin` of at least one bbox edge  
4. `interiorFrac` = fraction farther than margin from all edges  

Rules (first match):

| condition | layout/kind |
|-----------|-------------|
| n==1 | single |
| provisional rows==1 && cols&gt;1 | row |
| provisional cols==1 && rows&gt;1 | column |
| edgeFrac ≥ 0.75 && interiorFrac ≤ 0.25 && n ≥ 8 | **peripheral** |
| rows&gt;1 && cols&gt;1 && fillRatio ≥ 0.5 | grid |
| rows&gt;1 && cols&gt;1 && fillRatio &lt; 0.5 | sparse (legacy) **or** if edgeFrac≥0.6 → peripheral |
| else | unordered |

Prefer **peripheral** over sparse when edge-dominated (fixes QFP hollow lattice).

### 3.3 Peripheral solver

1. Bbox in local space: minLx,maxLx,minLy,maxLy  
2. For each pin, distances to four edges; assign nearest edge if dist ≤ margin  
3. Else if distance to centroid &lt; 0.25 * min(w,h) → `thermal`  
4. Else → nearest edge anyway + warning `ambiguous_side`  
5. Per side, sort:
   - top/bottom: by lx ascending → index 0..n-1  
   - left/right: by ly ascending → index 0..n-1  
6. Also set `row`/`col` as **derived presentation** (optional):  
   - map thermal → center cell  
   - or keep grid clustering result for row/col while side/index is authoritative for peripheral  

**Authoritative for peripheral:** `side` + `index`  
**row/col:** still filled via local 1D clustering for compatibility, but `layout=peripheral` tells clients to prefer side/index.

### 3.4 Grid solver

Existing mode-pitch + 0.4×pitch 1D clustering on **local** lx/ly; assign row/col; side empty.

## 4. JSON extensions

```json
{
  "kind": "peripheral",
  "layout": "peripheral",
  "rotationDeg": 0.0,
  "centroid": { "x": 0, "y": 0 },
  "rows": 9,
  "cols": 9,
  "pins": [
    {
      "key": "1",
      "board": { "x": 0, "y": 0 },
      "local": { "x": 0, "y": 0 },
      "row": 0,
      "col": 1,
      "side": "top",
      "index": 0
    },
    {
      "key": "25",
      "side": "thermal",
      "index": 0,
      "row": 4,
      "col": 4
    }
  ]
}
```

- `side`: `top|bottom|left|right|thermal|""`  
- `index`: ≥0 along side; thermal usually 0  
- `layout` mirrors refined kind; keep `kind` equal to `layout` for simplicity  

## 5. Tests

- N65102: `layout/kind=peripheral`, 24 edge pins with sides, 1 thermal, **no duplicate (row,col) if grid still run** OR unique side+index  
- Existing grid HTTP case still passes  
- Unit: PCA no-op on axis-aligned dual row  

## 6. Approval notes

User 2026-08-01: proceed; **do not use part.angle for rotation** — PCA only.
