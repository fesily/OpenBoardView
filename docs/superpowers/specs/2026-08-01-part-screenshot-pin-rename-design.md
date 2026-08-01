# Part Screenshot + Pin show_name Rename — Design Spec

**Date:** 2026-08-01  
**Status:** Approved for implementation planning  
**Parent:** `2026-07-31-agent-pin-part-api-design.md`  
**Goal:** Agent-facing REST tools to (1) render a cropped PNG of a single part with pin pads and display labels, plus structured pin layout metadata; (2) batch-update overlay `show_name` so incorrect silk labels can be corrected without mutating board geometry.

---

## 1. Goals and non-goals

### Goals

1. **Part screenshot (server-side PNG)** — given board ref + part name, return a cropped raster of that part’s outline, pads, and pin display labels for agent visual inspection.
2. **Screenshot metadata** — companion JSON with image size, board→image transform, and per-pin current labels + positions so agents can rename **without OCR**.
3. **Batch pin label write** — `PATCH` multiple `show_name` values into overlay `PartInfos[part].pins[key]` in one request.
4. Stay inside architecture invariants:
   - Parse only on server  
   - Geometry / netlist read-only  
   - **Only overlay is writable**  
   - board ref = `boardId` | unique library path | unique basename (existing `ResolveRef`)  
   - Desktop coexistence via shared YAML `show_name`

### Explicit non-goals (this change)

- Headless Chromium / Playwright screenshots  
- OCR or LLM-in-server rename  
- Drawing full-board copper (tracks/vias/arcs) in the crop  
- Mutating board-file `pin.name` / `pin.number`  
- Real-time collab, auth, MCP protocol wrapper  
- Pixel-perfect match to browser Canvas theme (server renderer is “readable agent view”, not UI clone)  
- Web UI “export PNG” button (optional later consumer of the same API)

### Success criteria

1. `GET .../parts/{part}/screenshot` returns `image/png`; image shows the target part outline, its pads, and labels using the same priority as the web client: **overlay `show_name` > board `show_name` > `name` > `number`**.
2. `GET .../parts/{part}/screenshot/meta` returns pin keys and image-space positions consistent with the PNG transform.
3. `PATCH .../parts/{part}/pins` persists `show_name` into board-sidecar YAML; subsequent screenshot/meta and web UI reflect the change after reload.
4. Unknown pin keys → **400**; missing part/board → **404**; empty `show_name` clears overlay override.
5. No absolute server filesystem paths in JSON; error body remains `{error:{code,message}}`.

---

## 2. Architecture placement

```
Agent
  │  GET screenshot (PNG)
  │  GET screenshot/meta (JSON)
  │  PATCH pins { show_name... }
  ▼
obv_server routes  ──► ResolveRef / GetParsed / OverlayMutex
        │                    │
        │                    ▼
        │              BoardSnapshot (geometry RO)
        │                    │
        ├──────────► part_render (new): bounds → RGBA → PNG
        └──────────► overlay YAML load/save (PinInfo.show_name)
```

| Concern | Location |
|---------|----------|
| Crop bounds + draw list | `obv_core` (or small `obv_server` helper calling core geometry) |
| PNG encode | vendored single-header (`stb_image_write` or existing dep if any) |
| Font for labels | bundled small bitmap/TTF (server-only asset); document path |
| Overlay pin merge | existing `Annotations` / `SavePartNetYaml` |
| HTTP | `/api/v1/boards/:ref/parts/:part/...` next to existing agent routes |

**Do not** reimplement crop in the agent client. Server is source of truth for both pixels and metadata.

---

## 3. Display label resolution (shared)

For each pin belonging to `part`, display label:

```
overlay.partInfos[part].pins[pinOverlayKey].show_name   # if non-empty trim
else board pin.show_name                                 # if non-empty trim
else board pin.name
else board pin.number
else ""
```

`pinOverlayKey` = existing rule: `name || number || id` (same as `pinValues.ts` / `PinOverlayKey`).

Orientation marker (A1 / pin 1) may use a distinct fill color in the PNG for readability (optional, match web `#dd0000` when part pin count ≥ 3).

---

## 4. Screenshot rendering

### 4.1 Crop window

1. Resolve part component; require non-empty outline **or** ≥1 pin (else 400 `PART_NO_GEOMETRY`).
2. Axis-aligned bounds from outline points ∪ pin positions (pad extent ≈ half diameter / half size).
3. Expand by `padding` in **board units** (default: 5% of max(width,height), clamped to a minimum of 1.0 board unit).
4. Map board Y with the same convention as board JSON / web (document: y-up vs canvas flip — **match `ExportBoardJson` coordinates**; image row 0 = top of crop box in screen sense: flip Y when blitting so labels read upright).

### 4.2 Layers drawn (only inside crop)

| Order | Layer | Style (defaults) |
|-------|--------|------------------|
| 1 | Background | opaque dark `#1a1d24` |
| 2 | Part outline fill + stroke | fill `rgba(80,90,110,0.35)`, stroke `#8b93a7` |
| 3 | Pads | circle/rect by pin shape; fill cyan-like `#6ec6ff`; orientation pin red `#dd0000` |
| 4 | Pin labels | light text `#e8eaed` with dark halo for contrast |
| 5 | Part name (once) | above/near outline centroid, muted `#c5cad3` |

**Not drawn:** tracks, vias, arcs, other parts, annotations, net web, measurement values (diode/voltage).

### 4.3 Raster parameters (query)

| Query | Type | Default | Limits |
|-------|------|---------|--------|
| `scale` | float | auto-fit longest side to 512 px | 0.01 … 100; result max edge ≤ **2048** px |
| `padding` | float | auto 5% | 0 … 1e6 board units |
| `labels` | 0\|1 | 1 | hide pin text if 0 |
| `partName` | 0\|1 | 1 | hide part name if 0 |
| `maxEdge` | int | 512 | 64 … 2048 (used when scale omitted) |

If computed image would exceed `maxEdge` / hard 2048, reduce scale and note final `scale` in meta.

### 4.4 Implementation sketch

- Buffer: RGBA8 tightly packed  
- Primitives: filled polygon (outline), circle, rotated rect, axis-aligned text  
- Font: prefer stb_truetype + one bundled font under `src/obv_server/assets/` or `data/fonts/`; fallback 5×7 bitmap if TTF load fails (labels still required)  
- Encode: PNG via `stbi_write_png_to_func` into `std::string` / response buffer  
- No GPU; pure CPU; single-threaded per request under existing parse/overlay locks only as needed (snapshot read lock free; overlay needed for labels)

---

## 5. Screenshot HTTP API

### 5.1 PNG

```
GET /api/v1/boards/:ref/parts/:part/screenshot
```

- **200** `Content-Type: image/png` body = raw PNG bytes  
- Optional headers: `X-Image-Width`, `X-Image-Height`, `X-Board-Scale` (informational; meta endpoint is authoritative)

### 5.2 Metadata

```
GET /api/v1/boards/:ref/parts/:part/screenshot/meta
```

Same query params as PNG (so agents can request matching scale/padding).

**200 JSON:**

```json
{
  "boardId": "<64 hex>",
  "sourceName": "<library-relative>",
  "part": "U12",
  "image": {
    "width": 512,
    "height": 400,
    "scale": 2.5,
    "padding": 12.0,
    "labels": true,
    "partName": true
  },
  "boardBounds": {
    "minX": 0, "minY": 0, "maxX": 100, "maxY": 80
  },
  "transform": {
    "boardToImage": {
      "originBoardX": 0,
      "originBoardY": 80,
      "scale": 2.5,
      "flipY": true
    }
  },
  "pins": [
    {
      "key": "1",
      "id": "...",
      "number": "1",
      "name": "...",
      "boardShowName": "...",
      "overlayShowName": "VCC",
      "displayLabel": "VCC",
      "board": { "x": 10, "y": 20 },
      "image": { "x": 25, "y": 150 },
      "type": "component",
      "shape": "circle",
      "diameter": 5,
      "netName": "..."
    }
  ]
}
```

Transform contract:

```
imageX = (boardX - originBoardX) * scale
imageY = (originBoardY - boardY) * scale   // when flipY true
```

`displayLabel` uses §3 resolution. `overlayShowName` empty string if no overlay override.

### 5.3 Errors (screenshot)

| HTTP | code | When |
|------|------|------|
| 404 | `NOT_FOUND` | board missing |
| 404 | `PART_NOT_FOUND` | component missing |
| 400 | `PARSE_FAILED` | board parse failed |
| 400 | `PART_NO_GEOMETRY` | no outline and no pins |
| 400 | `BAD_REQUEST` | invalid query (scale/maxEdge) |
| 500 | `RENDER_FAILED` | PNG encode / OOM |

---

## 6. Batch pin show_name PATCH

### 6.1 Endpoint

```
PATCH /api/v1/boards/:ref/parts/:part/pins
Content-Type: application/json
```

### 6.2 Request body

```json
{
  "pins": {
    "<pinKey>": { "show_name": "VCC" },
    "<pinKey>": { "show_name": "" }
  }
}
```

Rules:

| Rule | Behavior |
|------|----------|
| Max keys per request | 512 |
| `show_name` max length | 128 (trim) |
| Empty / whitespace-only `show_name` | **clear** overlay `show_name` (field removed or set empty so display falls back to board) |
| Unknown pin key (not matching any pin under part by name/number/id/overlay key) | **400** `UNKNOWN_PIN_KEY` (strict; message lists first few bad keys) |
| Empty `pins` object | **400** `BAD_REQUEST` |
| Part missing on board | **404** `PART_NOT_FOUND` |
| Only `show_name` accepted this MVP | other fields in pin object → **400** (reject unknown fields) |

### 6.3 Persistence

1. `ResolveRef` → parse board → verify part exists  
2. Lock `OverlayMutex`  
3. `LoadOverlayForBoard`  
4. For each key: resolve to canonical overlay key (`PinOverlayKey` of matched pin); set/clear `partInfos[part].pins[key].show_name`  
5. Create empty `PartInfo` / `PinInfo` only when writing non-empty show_name  
6. After clear: if `PinInfo` becomes empty (`operator bool` false), prune pin entry (existing SavePinInfos behavior)  
7. `SavePartNetYaml` with existing verify-after-write  
8. Return updated snapshot for the touched pins

### 6.4 Response 200

```json
{
  "boardId": "...",
  "part": "U12",
  "updated": [
    {
      "key": "1",
      "show_name": "VCC",
      "displayLabel": "VCC"
    }
  ]
}
```

Cleared pins appear with `"show_name": ""` and `displayLabel` equal to post-clear resolution.

### 6.5 Interaction with bulk overlay PUT

Unchanged: full `PUT /overlays` replaces entire `partInfos` and can wipe show_names if client omits them. Document only.

### 6.6 Interaction with GET pin resolve

Existing pin resolve already returns overlay `show_name`; no schema change required beyond observing new values after PATCH.

---

## 7. Agent automation loop (normative usage)

```
1. GET  /api/v1/boards/{ref}/parts/{part}/screenshot/meta
2. GET  /api/v1/boards/{ref}/parts/{part}/screenshot
      (same query string as meta)
3. Agent decides renames using meta.pins[].displayLabel + image coords
      (+ PNG for human-in-the-loop or multimodal model)
4. PATCH /api/v1/boards/{ref}/parts/{part}/pins
      { "pins": { "1": { "show_name": "..." }, ... } }
5. Optional: repeat 1–2 to verify
```

No server-side session object.

---

## 8. Error summary

Unified body:

```json
{ "error": { "code": "NOT_FOUND", "message": "..." } }
```

| code | HTTP | Used by |
|------|------|---------|
| `NOT_FOUND` | 404 | board |
| `PART_NOT_FOUND` | 404 | part |
| `PART_NO_GEOMETRY` | 400 | screenshot |
| `BAD_REQUEST` | 400 | query/body |
| `UNKNOWN_PIN_KEY` | 400 | PATCH |
| `PARSE_FAILED` | 400 | parse |
| `OVERLAY_LOAD_FAILED` / `OVERLAY_SAVE_FAILED` | 500 | PATCH |
| `RENDER_FAILED` | 500 | screenshot |
| `BOARD_REF_AMBIGUOUS` | 409 | ref |

---

## 9. Testing

- **Unit (obv_core_tests):** label resolution; bounds with padding; boardToImage transform round-trip for pin centers; PATCH merge logic via overlay YAML round-trip (show_name set/clear).  
- **HTTP (scripts/test_agent_api.py extension):**  
  - screenshot 200 + PNG magic bytes `89 50 4E 47`  
  - meta pins non-empty; each pin has image x/y in [0,width]/[0,height]  
  - PATCH then meta `overlayShowName` / `displayLabel` updated  
  - PATCH unknown key 400  
  - PATCH clear restores display to board label  
- Manual agent smoke optional.

---

## 10. Implementation sketch (not a plan)

1. Vendor `stb_image_write.h` (+ optional `stb_truetype.h`) under `src/obv_server/third_party/` or `obv_core/third_party/`.  
2. `obv_core/include/obv_core/part_render.h` + `.cpp`: `RenderPartScreenshot(board, ann, part, opts) → PNG bytes + Meta`.  
3. Routes: screenshot, screenshot/meta, PATCH pins.  
4. Extend `scripts/test_agent_api.py` cases.  
5. Docs: this spec; link from agent API design.

---

## 11. Architecture invariant checklist

| Invariant | How upheld |
|-----------|------------|
| Parse only on server | Render + rename use server snapshot |
| Geometry/netlist RO | PATCH only `show_name` in overlay |
| Only overlay writable | YAML PartInfos |
| No absolute paths | `sourceName` = displayPath |
| Desktop coexistence | same `show_name` field desktop already loads |

---

## 12. Open follow-ups (out of scope)

- Measurement values / net names on screenshot  
- Neighbor copper in crop  
- Web UI download button  
- Multimodal auto-rename service  
- Real MCP tool descriptors  

---

## 13. Approval record

| Section | Status |
|---------|--------|
| Screenshot render + dual API | Approved 2026-08-01 |
| Batch PATCH show_name | Approved 2026-08-01 |
| Errors / loop / non-goals | Approved 2026-08-01 |
| Approach | A — PNG + meta + PATCH pins |
