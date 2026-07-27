# Task 11 Report: Overlay editing UI

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `214ba73` (Task 10 search + highlight)  
**Commit:** `feat(web): annotation and pin overlay editing`  
**Date:** 2026-07-27

---

## Summary

Client overlay editor for pin/part/net fields and freeform annotations:

- Load overlays when a board opens (`GET …/overlays`); **Reload overlays** button for multi-browser refresh
- `InfoPane`: board pin/part/net details + editable pin/part/net overlays via `PUT` overlays (read-modify-write on `partInfos`/`netInfos`)
- Right-click canvas → prompt → `POST …/annotations` at board coords (side 0=top / 1=bottom); markers drawn on canvas
- Annotation list: `PATCH` note, `DELETE` soft-remove; local overlay state refreshed after each write
- `postAnnotation` now returns the created `OverlayAnnotation` (server 201 body)

Search/highlight/canvas selection unchanged.

---

## Files

| Path | Role |
|------|------|
| `web/src/ui/InfoPane.tsx` | Selection + pin/part/net overlay edit + annotation list |
| `web/src/ui/App.tsx` | Load overlays, context annotate, wire InfoPane |
| `web/src/scene/BoardCanvas.tsx` | `onContextMenu` → board coords; annotations prop |
| `web/src/scene/draw.ts` | Annotation triangle markers |
| `web/src/api/client.ts` | `postAnnotation` → `Promise<OverlayAnnotation>` |
| `web/src/index.css` | Info pane / annotation styles |

---

## API usage

| Action | Method |
|--------|--------|
| Load | `getOverlays(id)` on open + reload |
| Pin/part/net fields | `putOverlays(id, { partInfos, netInfos })` |
| New note | `postAnnotation(renderedBoardId, { side, x, y, note, part?, pin?, net? })` |
| Edit note | `patchAnnotation(id, annId, note)` |
| Remove | `deleteAnnotation(id, annId)` |

Pin PartInfos key: `pin.name \|\| pin.number \|\| pin.id` under `partInfos[component].pins`.

---

## Verification

```text
cd web && npm test
# → 26 passed (transform + hitTest + search)

cd web && npm run build
# → tsc -b && vite build OK
```

Manual checklist:

1. Open board → overlays load (empty or prior YAML/SQLite).
2. Select pin → board fields + pin/part/net overlay editors.
3. Edit note/show_name → Save pin overlay → Reload overlays still shows values.
4. Edit part_type/angle → Save part overlay; edit net showname/note → Save net overlay.
5. Right-click canvas → create annotation → marker + list row (uses rendered `board.boardId`).
6. PATCH note / Delete annotation from list.
7. Second browser: Reload overlays → sees first browser’s writes (last-write-wins).

---

## Out of scope

- Task 12 (serve `web/dist` from `obv_server`)
- Auth / conflict UI beyond last-write-wins

---

## Review fix (2026-07-27)

**Commit:** `fix(web): part/net overlay editors and annotation board id`

Addressed Task 11 review findings:

1. **Net overlay editing** — `InfoPane` now has inputs for `showname` + `note` with **Save net overlay** (`PUT` RMW `netInfos[net.name]`).
2. **Part overlay editing** — inputs for `part_type` and angle select (0/90/180/270/none) with **Save part overlay** (`PUT` RMW `partInfos[partName]`).
3. **Annotation board id** — posts use rendered `board.boardId`; old board cleared on selection change; canvas hidden while `boardLoading` so right-click cannot target a mismatched id.

### Verification

```text
cd web && npm test   # 26 passed
cd web && npm run build  # OK
```
