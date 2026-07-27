# Task 9 Report: View transform + Canvas outline/pins

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `ae216c1` (Task 8 scaffold)  
**Commit:** `feat(web): canvas board view with pan zoom flip`  
**Date:** 2026-07-27

---

## Summary

Ported desktop `BoardView` view-transform semantics to TypeScript and rendered boards on Canvas 2D:

- `ViewState`: `scale`, `mx`/`my` (look-at), `rotation` 0–3 CW, `side`, `flipY`, `mirror`
- `boardToScreen` / `screenToBoard` invert each other (Y flip default true like `m_flipVertically`)
- `centerOnBounds` matches `CenterView` 1.1× padding + min(sx,sy)
- Layers: fill → outline → parts → pins → highlights
- Wheel zoom toward cursor, drag pan, flip / rotate / reset toolbar
- Nearest-pin hit-test in board space (desktop-style diameter threshold)
- App loads full board JSON on list click / successful upload and shows selection readout

---

## Files

| Path | Role |
|------|------|
| `web/src/scene/transform.ts` | ViewState + board/screen transforms + pan/zoom/flip/rotate helpers |
| `web/src/scene/transform.test.ts` | Vitest roundtrip + zoom/pan/flip cases |
| `web/src/scene/draw.ts` | Canvas 2D layered draw |
| `web/src/scene/hitTest.ts` | Nearest pin pick |
| `web/src/scene/BoardCanvas.tsx` | Interactive canvas + toolbar |
| `web/src/ui/App.tsx` | Select board → `getBoard` → BoardCanvas |
| `web/src/index.css` | Canvas layout styles |
| `web/package.json` / lock | `vitest` + `npm test` |
| `web/vite.config.ts` | Vitest include |
| `web/tsconfig.node.json` | `vitest/config` types |

---

## Transform notes (from BoardView.cpp)

- Bottom side mirrors X relative to look-at (`side * (x - mx)`).
- Default `flipY: true` negates Y (board Y up → screen Y down).
- Rotation cases match `CoordToScreen` 0..3 clockwise quarters; look-at stays canvas center (desktop `m_dx`/`m_dy` pan folded into `mx`/`my`).
- Flip: toggle side + rotate +2 when `flipY` (desktop `FlipBoard` + `m_flipVertically`).
- Geometry `Mirror()` (mutate board X) not applied to JSON; client `mirror` flag available for X mirror if needed later.

---

## Verification

```text
cd web && npm test
# → 6 passed (transform roundtrip, center, rotate/flip, zoomAt, panByScreen, flipBoard)

cd web && npm run build
# → tsc -b && vite build OK
```

Manual: start `obv_server`, `npm run dev`, upload/select board → outline/parts/pins, pan/zoom/flip/rotate/reset, click pin → selection pane.

---

## Out of scope

- Search UI / highlight sets (Task 10)
- Overlay editor / annotations (Task 11)
- Tracks/vias/arcs full draw (optional later; pins/outline/parts only for MVP)
- Desktop pixel-perfect color scheme parity

---

## Important fix follow-up (Task9Fix)

**Status:** FIXED  
**Commit:** `fix(web): canvas rotate pan, highlight side, load race`  
**Date:** 2026-07-27

### 1. Preserve panned viewport on rotate
- **Bug:** `rotateView` only bumped `rotation`, so after pan the on-screen region spun around look-at without an explicit center-preservation contract (desktop `BoardView::Rotate` adjusts pan offsets).
- **Fix:** Capture board point under screen center, apply rotation, recompute `mx`/`my` so that point maps back to center (`web/src/scene/transform.ts`). `BoardCanvas` passes canvas size into `rotateView`.
- **Test:** `rotateView keeps panned screen-center board point fixed` in `transform.test.ts`.

### 2. Hide selected-pin highlight on non-visible side
- **Bug:** `drawPins` skipped other-side pins via `sideVisible`, but `drawHighlights` still drew the selection halo for a selected pin on the flipped-away side.
- **Fix:** Early-return in `drawHighlights` when `!sideVisible(pin.side, view.side)`.

### 3. Ignore stale board-load responses
- **Bug:** Rapid board switches could apply an older `getBoard` result after a newer selection.
- **Fix:** Generation counter in `openBoard`; only `setBoard` / error / loading clear when gen still current.

### Verification
```text
cd web && npm test
# → 7 passed

cd web && npm run build
# → tsc -b && vite build OK
```

---

## Important fix follow-up (Task9Fix2): pin.diameter is radius

**Status:** FIXED  
**Commit:** `fix(web): treat pin.diameter as radius`  
**Date:** 2026-07-27

### Problem
JSON field `pin.diameter` is misnamed. Desktop stores **radius** (`pin->diameter = brd_pin.radius / scale`) and draws with `AddCircle(..., p->diameter * m_scale, ...)`. Web `draw.ts` / `hitTest.ts` multiplied by `0.5`, so pads rendered and picked at half size.

### Fix
- `draw.ts` `pinRadiusPx`: use `pin.diameter` directly as board-space radius when `> 0`; else default **7** (BoardView empty-diameter fill).
- `hitTest.ts`: same semantics; threshold = radius (not diameter/2). Renamed `DEFAULT_PIN_DIAMETER` → `DEFAULT_PIN_RADIUS = 7`.
- Unit tests in `hitTest.test.ts` assert no half-factor and default-7 fallback.

### Verification
```text
cd web && npm test
# → 11 passed (transform + hitTest)

cd web && npm run build
# → tsc -b && vite build OK
```
