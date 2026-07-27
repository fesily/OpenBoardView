# OpenBoardView Web + Server Refactor — Design Spec

**Date:** 2026-07-27  
**Status:** Draft for review  
**Goal:** Remote access to existing desktop capabilities via browser + local/intranet server (no login).

---

## 1. Goals and non-goals

### Goals (MVP)

- Open board files through a browser against a local/intranet server.
- Server-side parse of existing formats (reuse C++ `FileFormats` + `BRDBoard`).
- Browser-side interactive view: pan / zoom / flip / rotate, part & net search, pin/net highlight.
- Read + write overlays: freeform annotations and Part/Pin/Net info overlays (same semantic model as desktop YAML + SQLite annotations).
- Multi-person use on LAN without authentication; last-write-wins for overlays.

### Explicit non-goals (MVP)

- User accounts, auth, ACL, multi-tenant isolation.
- Real-time collaborative editing (OT/CRDT).
- PDFBridge / desktop DDE / Evince integration.
- Shipping encryption keys to the browser.
- Full visual/theme parity with every ImGui preference panel.
- Replacing the desktop binary in v1 (desktop may keep sharing `obv_core`).

### Success criteria

1. Upload or open a known-good `.brd` / `.json` / supported format → board renders in browser.
2. Search part/net → highlights match desktop semantics (substring search default).
3. Create/edit/delete annotation and pin overlay → survives server restart and reloads in another browser.
4. Encrypted formats (`.fz` / `.cae` / `.xzz`) parse only when server-side keys are configured; keys never appear in API responses.

---

## 2. Constraints (from product choices)

| Choice | Decision |
|--------|----------|
| Primary goal | Remote access to desktop capability, not SaaS collaboration |
| Split | Server parse + browser render |
| Deploy | Local / intranet, multi-user, **no login** |
| MVP scope | View + search + highlight + annotation/overlay write |
| Stack | C++ parse core + thin HTTP server + Web frontend |

---

## 3. Current architecture (baseline)

Desktop app is a single process:

```
file bytes
  → BoardView::LoadFile (format detect)
  → BRDFileBase subclass
  → BRDBoard (domain: Net / Component / Pin / Track / Via / PcbArc)
  → Annotations load (board.yaml + optional board.sqlite3)
  → BoardView::Draw* via ImGui ImDrawList + SDL/OpenGL
  → Searcher in-process
```

Key sources:

- Parse: `src/openboardview/FileFormats/*`, `BoardView::LoadFile`
- Domain: `src/openboardview/Board.h`, `BRDBoard.*`
- Render/controller monolith: `BoardView.*` (~4k+ LOC)
- Query: `Searcher.*`
- Overlay: `annotations.*` (YAML PartInfos/NetInfos; SQLite freeform annotations when enabled)
- Shell: `main_opengl.cpp`

There is **no** existing network/server layer.

### Coupling risks addressed by this design

1. Render is hard-wired to `ImDrawList` → **rewrite on client**, do not stream ImGui commands.
2. Viewport math lives in `BoardView` → **port transform model to client**.
3. Encrypted parsers need keys → **server-only config**.
4. `BoardView` mixes IO, domain, UI, draw → **extract `obv_core`**, leave UI behind.

---

## 4. Target architecture

### 4.1 Deploy shape

- One server process binds a configurable host/port. **Default bind: `127.0.0.1`** (local companion); set `0.0.0.0` explicitly for LAN multi-user.
- Configurable data root:
  - `boards/` — uploaded or imported board files
  - `overlays/` — per-board annotation stores (or sidecar next to board)
  - `config/` — server config including decrypt keys (file mode `0600` recommended)
- Static web assets served by the same process (or reverse proxy in front).
- No auth middleware in MVP.

### 4.2 Process / module diagram

```
┌─────────────────────────────────────────────┐
│ Browser                                     │
│  UI Shell │ Board Scene │ Search │ Overlay  │
└─────────────┬───────────────┬───────────────┘
              │ REST/JSON     │ (optional WS later)
┌─────────────▼───────────────▼───────────────┐
│ obv_server (thin HTTP)                      │
│  routes │ board registry │ static files     │
└─────────────┬───────────────────────────────┘
              │ in-process API
┌─────────────▼───────────────────────────────┐
│ obv_core (C++ library)                      │
│  parse │ board model │ serialize │ overlay  │
└─────────────────────────────────────────────┘
```

### 4.3 Module ownership (maps to user-named cores)

| Module | Responsibility | Runs where | Built from |
|--------|----------------|------------|------------|
| **Load + Parse** | Read bytes, detect format, decrypt, build `BRDBoard` | Server (`obv_core`) | `FileFormats/*`, load chain from `LoadFile`, `BRDBoard` |
| **Render** | Coord transforms, layered draw, hit-test, hover | Browser only | New Canvas/WebGL scene; semantics from `Draw*` / `CoordToScreen` |
| **Query** | Part/net/pin search, highlight sets | Browser default (in-memory board JSON); optional server index later | Port `Searcher` behavior |
| **Overlay / modify** | Freeform notes + Part/Pin/Net info | Server persist + browser editor | `Annotations` model; storage adapter |

### 4.4 Shared library strategy

Extract **`obv_core`** as a static/shared library **without** SDL/ImGui/OpenGL:

- Format parsers + `BRDFileBase` tree
- `Board` / `BRDBoard` domain construction
- Overlay load/save (YAML + SQLite path, feature-flagged)
- JSON export of board + overlay DTOs
- Pure functions: format detect, parse buffer, apply overlay onto view model

`obv_server` links `obv_core` + HTTP stack.  
Desktop `openboardview` **may** later link the same `obv_core` (phase after MVP) so parsers stay single-sourced. Desktop cutover is **not** required for web MVP.

---

## 5. Data model

### 5.1 Units and coordinates

- Board space stays in existing internal units (mil/thou as today after parse).
- Origin and axis conventions match `BRDBoard` / current `CoordToScreen` inputs.
- Client applies: translation (`mx,my`), scale, rotation (0–3 quarters), side flip/mirror, then screen mapping.
- Server does **not** send screen-space geometry.

### 5.2 Board document (server → client)

Logical JSON shape (field names illustrative; implement as versioned DTO `boardSchemaVersion`):

```json
{
  "boardSchemaVersion": 1,
  "boardId": "…",
  "sourceName": "example.brd",
  "bounds": { "minX": 0, "minY": 0, "maxX": 0, "maxY": 0 },
  "sides": ["top", "bottom"],
  "outline": {
    "points": [{"x":0,"y":0}],
    "segments": [{"x1":0,"y1":0,"x2":0,"y2":0}]
  },
  "nets": [
    { "id": 1, "name": "GND", "isGround": true }
  ],
  "components": [
    {
      "name": "R1",
      "side": "top",
      "mount": "smd",
      "type": "resistor",
      "mfgcode": "",
      "center": {"x":0,"y":0},
      "outline": [{"x":0,"y":0}],
      "pins": ["R1.1", "R1.2"]
    }
  ],
  "pins": [
    {
      "id": "R1.1",
      "component": "R1",
      "number": "1",
      "name": "",
      "netId": 1,
      "side": "top",
      "pos": {"x":0,"y":0},
      "shape": "circle",
      "diameter": 7,
      "size": {"x":0,"y":0},
      "angle": 0
    }
  ],
  "tracks": [],
  "vias": [],
  "arcs": []
}
```

Rules:

- IDs stable for a given parse of a file (pin id = `component + "." + number` or existing unique strategy; document chosen rule in code).
- Geometry is **raw board**, not pre-outlined for a theme.
- Optional large arrays may be split later (`GET /boards/:id/geometry` vs `/nets`) if payload size demands it; MVP may single-shot.

### 5.3 Overlay document (read/write)

Mirrors desktop:

1. **Freeform annotations** (today SQLite `annotations` table): id, side, x, y, net, part, pin, note, visible.
2. **PartInfos / NetInfos** (today `filename.yaml` Version `0.0.2`): part_type, angle, per-pin show_name/diode/voltage/ohm/ohm_black/note/voltage_flag; net showname/note.

API exposes one combined overlay resource; server maps to the same on-disk artifacts so desktop and web can share files if pointed at the same board path (nice-to-have, not required day one).

### 5.4 Board registry

| Field | Meaning |
|-------|---------|
| `boardId` | Stable id (sha256 of stored file bytes, or uuid + stored path map) |
| `path` | Absolute path under data root |
| `name` | Original filename |
| `mtime` / size | Cache invalidation |
| `parseStatus` | ok / error + message |

In-memory parse cache: `boardId → shared_ptr<BoardSnapshot>` with optional TTL / LRU. Re-parse when file mtime changes.

---

## 6. HTTP API (MVP)

Base: `/api/v1`. JSON request/response. CORS open for LAN dev if UI is on another origin; production default same-origin static host.

### Boards

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/boards` | List known boards (id, name, mtime, status) |
| `POST` | `/boards` | Upload multipart file → parse → return `boardId` + summary |
| `GET` | `/boards/:id` | Full board JSON (geometry + nets + parts + pins …) |
| `GET` | `/boards/:id/meta` | Bounds, name, side list, schema version only |
| `DELETE` | `/boards/:id` | Optional; remove uploaded board + overlay (config-gated) |

### Overlays

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/boards/:id/overlays` | Full overlay document |
| `PUT` | `/boards/:id/overlays` | Replace PartInfos/NetInfos blob (or whole doc) |
| `POST` | `/boards/:id/annotations` | Create freeform annotation |
| `PATCH` | `/boards/:id/annotations/:annId` | Update note/fields |
| `DELETE` | `/boards/:id/annotations/:annId` | Soft-delete (visible=0) like desktop |

### System

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/health` | Liveness |
| `GET` | `/version` | Server + core version |

### Errors

Uniform envelope:

```json
{ "error": { "code": "PARSE_FAILED", "message": "Unrecognized file format." } }
```

Map from existing `m_error_msg` / `BRDFileBase::error_msg` where possible.

### Search

- **MVP:** client-side over downloaded board JSON (substring / prefix / whole modes mirroring `SearchMode`).
- **Later:** `GET /boards/:id/search?q=&type=part|net` only if boards exceed comfortable memory/transfer.

### Not in MVP API

- Auth headers, sessions, websockets, binary protobuf (JSON first; optional MessagePack later).

---

## 7. Server design

### 7.1 Suggested layout (new tree)

```
src/
  obv_core/           # parse, board, overlay, json export
  obv_server/         # HTTP routes, static, main
  openboardview/      # existing desktop (unchanged initially)
web/                  # frontend package
```

### 7.2 HTTP stack

Default recommendation: **C++ HTTP** in-process with `obv_core` (e.g. cpp-httplib or Crow) to avoid FFI and keep one binary for intranet drop-in.

Alternative acceptable: small Node/Go reverse facade calling `obv_core` via CLI/FFI — only if team prefers; not default.

### 7.3 Parse pipeline (server)

Port the decision tree from `BoardView::LoadFile`:

1. Read buffer from upload or disk.
2. Extension/encrypted shortcuts (`.fz`, `.cae`, …) then `verifyFormat` chain.
3. On `valid`, construct `BRDBoard`, compute bounds (logic from `LoadBoard`).
4. Load overlays via `Annotations::SetFilename` + `Load` / YAML deserialize.
5. Export DTO JSON; cache snapshot.

Decrypt keys: server config file fields analogous to `config.FZKey` / `CAEKey` / `XZZPCBKey`. Never log key material.

### 7.4 Concurrency

- Parse may be CPU-heavy: serialize parses with a worker queue or mutex around non-thread-safe parser state if needed.
- Read cache shared under shared_mutex; overlay writes lock per `boardId`.
- No login ⇒ no per-user isolation; document last-write-wins.

### 7.5 Static hosting

Serve `web/dist` at `/` so a single port is enough for LAN.

---

## 8. Frontend design

### 8.1 Stack (default)

- TypeScript + Vite
- UI: React or Vue (team choice; default **React** for ecosystem)
- Board scene: **Canvas 2D first** (simpler hit-test); upgrade hot paths to WebGL if large boards demand it
- State: board document + view transform + selection + overlay draft

### 8.2 View transform (port of desktop semantics)

Client state mirrors `BoardView` essentials:

- `scale`, `mx`/`my` (board midpoint target), `rotation` (0–3), `currentSide`, flip/mirror flags
- `boardToScreen` / `screenToBoard`
- `needsRedraw` equivalent: dirty flag on transform/selection change

Interactions (MVP):

- Wheel zoom toward cursor
- Drag pan
- Flip side, rotate 90°
- Click select pin/part; highlight whole net
- Search box → highlight set + optional center-on-first

### 8.3 Draw layers (align `DrawChannel`)

1. Background / board fill  
2. Outline  
3. Tracks / arcs / vias (if present)  
4. Parts outlines  
5. Pins  
6. Highlights / net web  
7. Annotations / text overlays  

### 8.4 UI chrome

- File open (upload) + board list from server
- Search (part/net)
- Side / rotate / reset view
- Info pane: selected pin/part/net fields + editable overlay fields
- Annotation create on context position

### 8.5 Offline / desktop PDF features

Out of scope. No Sumatra/Evince bridge in browser MVP.

---

## 9. Overlay write path

```
UI edit
  → PATCH/POST/PUT /api/v1/boards/:id/...
  → server updates Annotations model
  → persist YAML and/or SQLite (same files desktop uses when path-aligned)
  → return updated overlay
  → other browsers refresh on next GET (no live push in MVP)
```

Conflict policy: last HTTP write wins. Optional weak `ETag` / `If-Match` can be added later without auth.

---

## 10. Security (no-auth LAN)

Accept risk profile of trusted intranet:

- Bind defaults: prefer `127.0.0.1` for “local companion”; document `0.0.0.0` for LAN.
- No TLS required for pure offline LAN; optional reverse proxy TLS.
- Upload size limit and extension allow-list.
- Path traversal guards on `boardId` and stored paths.
- Keys only in server config file, not in env dumps or client bundle.
- DELETE board disabled by default.

---

## 11. Phased delivery

### Phase 0 — Documentation / spike (short)

- Inventory format list and sample boards.
- Confirm JSON size on a large board; decide single vs split payload.

### Phase 1 — `obv_core` extraction

- CMake library target without GUI deps.
- API: `ParseBoard(bytes|path) → BoardSnapshot`, `ExportBoardJson`, overlay load/save.
- Unit/smoke: parse fixture → JSON golden snippet.

### Phase 2 — `obv_server`

- Health, upload, get board, get/put overlays, annotation CRUD.
- Static file hook.
- Manual curl verification.

### Phase 3 — Web viewer (read-only)

- Upload/list/open, Canvas render outline/parts/pins, pan/zoom/flip/rotate.
- Client search + highlight.

### Phase 4 — Overlay editing

- Annotation CRUD UI; pin/part/net info editors; reload persistence check across browsers.

### Phase 5 — Hardening

- Large board performance, cache, error UX, packaging (`Dockerfile` / single binary + web assets), optional desktop link to `obv_core`.

---

## 12. Testing strategy

| Layer | What |
|-------|------|
| Core | Parse known fixtures; JSON schema/version field present; overlay round-trip |
| API | Integration tests: upload → GET board → write annotation → GET overlays |
| Frontend | Transform unit tests; search mode tests; manual visual checklist on sample boards |
| Regression | Same board opened in desktop vs web: net membership and pin counts match |

No requirement to drive ImGui in CI for web path.

---

## 13. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Huge JSON for dense boards | Measure early; compress (gzip); split endpoints; later binary |
| Parser thread-safety unknown | Serialize parse; audit global state in format code |
| Desktop/web overlay drift | Share `obv_core` serializers; same YAML version `0.0.2` |
| Canvas perf | Layer caching; spatial index for hit-test; WebGL path if needed |
| No-auth abuse on LAN | Bind address docs; upload limits; disable delete |

---

## 14. Open decisions (defaults locked unless overridden)

1. **Frontend framework:** React + Vite + Canvas 2D (default).  
2. **HTTP library:** cpp-httplib or Crow in-process (implementer picks one mature option).  
3. **boardId:** SHA-256 of stored file bytes (content-addressed); collisions of name handled by id.  
4. **Desktop migration to `obv_core`:** after web MVP, not blocking.  
5. **SQLite annotations:** enable in server builds (`ENABLE_SQLITE3`) to match desktop freeform notes when available; YAML always on for Part/Net infos.

---

## 15. Documentation references (implementation must re-read)

- `src/openboardview/BoardView.cpp` — `LoadFile`, `LoadBoard`, draw/search entry points  
- `src/openboardview/FileFormats/BRDFileBase.h` — parse staging structs  
- `src/openboardview/Board.h` / `BRDBoard.*` — domain model  
- `src/openboardview/Searcher.*` — search modes  
- `src/openboardview/annotations.*` — overlay schema and persistence  
- `src/openboardview/main_opengl.cpp` — desktop shell only (do not port)

---

## 16. Approval gate

This document is the design baseline for the subsequent **implementation plan** (phased tasks, verification checklists). Changes to goals, auth, or split boundary require updating this spec first.
