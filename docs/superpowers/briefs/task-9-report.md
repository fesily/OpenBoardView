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
