# Final Review Fix Report

**Branch:** `merge_upsteam_my`  
**Date:** 2026-07-27  
**Commit message:** `fix: final review canvas routes, angle enum, overlay rename`

## Findings fixed

### 1. Render routing geometry (`web/src/scene/draw.ts`)

**Problem:** `drawBoard` only painted fill / outline / parts / pins / highlights / annotations. Boards with tracks, arcs, and vias loaded with empty copper.

**Fix:** Layer order now matches design §8.3:
`fill → outline → tracks → arcs → vias → parts → pins → highlights → annotations`.

- Tracks: canvas lines with scaled width, `sideVisible` filter.
- Arcs: `ctx.arc` with board radians, scaled radius/width, side filter.
- Vias: filled circles; shown when either `side` or `targetSide` is visible (or `both`).

Colors: `track` / `arc` `#5a7a9a`, `via` `#90a4ae`.

### 2. Part overlay angle encoding (`web/src/ui/InfoPane.tsx`)

**Problem:** UI sent degrees `90/180/270`. Server `ApplyOverlayJson` / `partAngleFromInt` expects desktop `PartAngle` enum integers:
`_0=0`, `_270=1`, `_180=2`, `_90=3`, `sorted=4`. Non-zero angle saves failed with `bad part angle`.

**Fix:**
- Select option **values** are enum ints `0–4`.
- Display **labels** remain `0° / 90° / 180° / 270° / sorted`.
- Save path validates enum range, not degrees.
- Load path already used `String(partInfo.angle)` so enum ints map back to the correct option.

### 3. Move overlay sidecars on duplicate upload rename (`src/obv_server/board_registry.cpp`)

**Problem:** Duplicate content-id upload with a new display name renamed only the board file. YAML / SQLite overlays stayed under the old board path, so overlays vanished after rename.

**Fix:** After a successful board rename, best-effort rename of sidecars using the same naming as `Annotations::Load` / `SavePinInfos`:
- YAML: `boardPath.string() + ".yaml"`
- SQLite: last `.` → `_`, then `+ ".sqlite3"`

Failures remain non-fatal (same policy as board rename).

## Verification

| Check | Result |
|-------|--------|
| `npm test` (web/) | 3 files / 26 tests passed |
| `npm run build` (web/) | vite production build OK |
| `cmake --build build-web --config Release --target obv_server` | `board_registry.cpp` rebuilt; `obv_server.exe` linked |

## Files touched

- `web/src/scene/draw.ts`
- `web/src/ui/InfoPane.tsx`
- `src/obv_server/board_registry.cpp`
- `docs/superpowers/briefs/final-review-fix-report.md`
