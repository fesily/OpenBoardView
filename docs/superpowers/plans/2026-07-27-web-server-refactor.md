# Web + Server Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a local/intranet OpenBoardView server that parses boards with existing C++ logic and a browser client that views, searches, and edits overlays — no login.

**Architecture:** Extract `obv_core` (parse + domain + overlay + JSON export) from the desktop tree; run `obv_server` (cpp-httplib) in-process against that library; serve a Vite/React Canvas client that owns viewport transform, draw, and client-side search. Spec: `docs/superpowers/specs/2026-07-27-web-server-refactor-design.md`.

**Tech Stack:** C++20, CMake, cpp-httplib (header-only), nlohmann/json (or hand-rolled minimal JSON for export), SQLite3 optional (`ENABLE_SQLITE3`), TypeScript, Vite, React, Canvas 2D.

## Global Constraints

- No auth in MVP; default bind `127.0.0.1`; LAN requires explicit `0.0.0.0`.
- Decrypt keys (FZ/CAE/XZZ) stay server-side only; never in API responses or web bundle.
- Geometry units = existing board space (mil/thou after parse); server never sends screen-space.
- Overlay on-disk semantics must match desktop: `boardpath.yaml` (Version `0.0.2` PartInfos/NetInfos) + optional `boardpath.sqlite3` freeform annotations.
- Do not port ImGui/SDL draw path to the browser; rewrite client scene.
- Desktop target may keep building unchanged until a later optional link to `obv_core`; web MVP must not require desktop cutover.
- Prefer small focused files; no new abstractions without a caller.
- Every task ends with a runnable verification command and a commit.
- Project has **no existing unit test framework** — introduce a minimal `obv_core_tests` executable using assert + main (no gtest dependency).

---

## File structure (create / modify)

```
src/
  obv_core/
    CMakeLists.txt
    include/obv_core/
      parse.h              # ParseBoardBuffer / ParseBoardFile
      board_snapshot.h     # owned BRDFileBase + BRDBoard + bounds
      board_json.h         # ExportBoardJson / ExportOverlayJson
      overlay_store.h      # load/save annotations for a board path
      core_utils.h         # file_as_buffer without SDL
      decrypt_keys.h       # FZ/CAE/XZZ key bags
      vec2.h               # float x,y (replace ImVec2 in domain for core builds)
    src/
      parse.cpp
      board_json.cpp
      overlay_store.cpp
      core_utils.cpp
    # Reuse (via target_sources or object library) existing:
    #   FileFormats/*, BRDBoard.*, Board.cpp, annotations.*, Crypto/des.c,
    #   vectorhulls if needed, platform-neutral pieces of utils
  obv_core_tests/
    CMakeLists.txt
    test_parse_export.cpp
    fixtures/              # tiny synthetic or checked-in sample buffers
  obv_server/
    CMakeLists.txt
    main.cpp
    server_config.h/.cpp   # host, port, data root, keys
    board_registry.h/.cpp  # id → path, cache
    routes.cpp             # httplib route registration
    third_party/httplib.h  # vendored
    third_party/json.hpp   # vendored nlohmann single header (optional)
web/
  package.json
  vite.config.ts
  index.html
  src/
    main.tsx
    api/client.ts
    types/board.ts
    scene/BoardCanvas.tsx
    scene/transform.ts
    scene/draw.ts
    scene/hitTest.ts
    search/search.ts
    ui/App.tsx
    ui/Toolbar.tsx
    ui/SearchBox.tsx
    ui/InfoPane.tsx
docs/superpowers/specs/2026-07-27-web-server-refactor-design.md  # already exists
```

**Modify (carefully, later tasks):**

- `src/CMakeLists.txt` — `add_subdirectory(obv_core)` / `obv_server` / tests when `ENABLE_OBV_SERVER`
- `src/openboardview/Board.h` — replace `ImVec2` with `obv::Vec2` (or local `Vec2`) so core builds without ImGui; desktop still compiles
- Optionally leave desktop `utils.cpp` SDL-based; core uses `core_utils.cpp`

---

### Task 1: Scaffold `obv_core` CMake target (no GUI deps)

**Files:**
- Create: `src/obv_core/CMakeLists.txt`
- Create: `src/obv_core/include/obv_core/vec2.h`
- Create: `src/obv_core/include/obv_core/core_utils.h`
- Create: `src/obv_core/src/core_utils.cpp`
- Modify: `src/CMakeLists.txt` — add `option(ENABLE_OBV_CORE …)` and subdirectory
- Modify: `src/openboardview/Board.h` — remove `#include "imgui/imgui.h"`; use `Vec2` for outline fields

**Interfaces:**
- Produces: CMake target `obv_core` (STATIC); `struct obv::Vec2 { float x, y; };`
- Consumes: existing FileFormats sources, zlib, mpc, utf8, filesystem flags from parent

- [ ] **Step 1: Add `vec2.h`**

```cpp
// src/obv_core/include/obv_core/vec2.h
#pragma once
namespace obv {
struct Vec2 {
  float x = 0.f;
  float y = 0.f;
  Vec2() = default;
  Vec2(float x, float y) : x(x), y(y) {}
};
} // namespace obv
```

- [ ] **Step 2: Replace ImVec2 in `Board.h`**

In `src/openboardview/Board.h`:
- Remove `#include "imgui/imgui.h"`
- Add `#include "obv_core/vec2.h"` **or** define a local `using Vec2 = obv::Vec2` after adding include path
- Replace `ImVec2` with `obv::Vec2` (or alias `Vec2`) on `Component::outline`, `special_outline`, `hull`, `omin`, `omax`, `centerpoint`

Grep desktop draw code for `.x`/`.y` on those fields — `ImVec2` and `obv::Vec2` both have public `x,y`, so most call sites compile unchanged. Any `ImVec2{a,b}` construction on those fields becomes `obv::Vec2{a,b}`.

- [ ] **Step 3: Implement SDL-free `file_as_buffer` in core_utils**

```cpp
// core_utils.h
#pragma once
#include "filesystem_impl.h"
#include <string>
#include <vector>
namespace obv {
std::vector<char> file_as_buffer(const filesystem::path &filepath, std::string &error_msg);
bool check_fileext(const filesystem::path &filepath, const std::string &fileext_lower);
}
```

Implementation: copy logic from `utils.cpp` but replace `SDL_LogError` with `fprintf(stderr, …)` or silent; do **not** include SDL.

- [ ] **Step 4: CMake `obv_core`**

`src/obv_core/CMakeLists.txt` should:
- `add_library(obv_core STATIC …)` listing:
  - `src/core_utils.cpp`
  - `../openboardview/Board.cpp`
  - `../openboardview/BRDBoard.cpp`
  - `../openboardview/annotations.cpp` (YAML path; SQLite if `ENABLE_SQLITE3`)
  - all `../openboardview/FileFormats/*.cpp` currently in desktop SOURCES
  - `../openboardview/Crypto/des.c`
  - GenCAD generated header dependency (reuse same custom_command pattern as desktop CMake, or share generated path)
- `target_include_directories(obv_core PUBLIC include PRIVATE ../openboardview ../ ${CMAKE_CURRENT_BINARY_DIR}/…)`
- Link: `mpc`, zlib, filesystem libs, optional SQLite; **do not** link imgui/SDL/glad
- Define `HAVE_SQLITE3` when enabled
- For `annotations.cpp` rapidyaml: include `src/rapidyaml.hpp` path (`..` from openboardview)

Wire parent:

```cmake
option(ENABLE_OBV_CORE "Build shared board core library" ON)
if(ENABLE_OBV_CORE)
  add_subdirectory(obv_core)
endif()
```

- [ ] **Step 5: Build core only**

```bash
cmake -S . -B build-web -DENABLE_OBV_CORE=ON -DENABLE_OBV_SERVER=OFF
cmake --build build-web --target obv_core -j
```

Expected: `obv_core` links; may still fail if `annotations.cpp` / Board pull GUI — fix includes until core builds.

- [ ] **Step 6: Rebuild desktop to ensure Board.h change is safe**

```bash
cmake --build build-web --target openboardview -j
```

Expected: desktop target still builds (or fix remaining `ImVec2` constructions in `BoardView.cpp` / hull code).

- [ ] **Step 7: Commit**

```bash
git add src/obv_core src/CMakeLists.txt src/openboardview/Board.h src/openboardview/BoardView.cpp
git commit -m "build: extract obv_core library without GUI deps"
```

---

### Task 2: Parse API + BoardSnapshot

**Files:**
- Create: `src/obv_core/include/obv_core/decrypt_keys.h`
- Create: `src/obv_core/include/obv_core/board_snapshot.h`
- Create: `src/obv_core/include/obv_core/parse.h`
- Create: `src/obv_core/src/parse.cpp`
- Create: `src/obv_core_tests/test_parse_export.cpp` (initial failing parse test)
- Create: `src/obv_core_tests/CMakeLists.txt`
- Modify: `src/obv_core/CMakeLists.txt` — add parse.cpp

**Interfaces:**
- Produces:
```cpp
namespace obv {
struct DecryptKeys {
  std::array<uint32_t, 44> fzKey{};
  // match Config fields used by FZFile/CAEFile/XZZPCBFile — copy exact types from GUI/Config.h
  // fill from server config later
  bool hasFz = false;
  bool hasCae = false;
  bool hasXzz = false;
};

struct BoardBounds {
  float minX, minY, maxX, maxY;
};

struct BoardSnapshot {
  std::unique_ptr<BRDFileBase> file;
  std::unique_ptr<BRDBoard> board;
  BoardBounds bounds{};
  std::string sourceName;
  std::string error;
  bool ok() const { return file && file->valid && board; }
};

// filepath optional (used for ASC relative assets + overlay naming)
BoardSnapshot ParseBoardBuffer(std::vector<char> buffer,
                               const filesystem::path &filepath,
                               const DecryptKeys &keys);
BoardSnapshot ParseBoardFile(const filesystem::path &filepath,
                             const DecryptKeys &keys);
}
```
- Consumes: format classes from FileFormats; outline generation logic from `BoardView::LoadBoard` (copy bounds + empty-outline fallback into parse.cpp)

- [ ] **Step 1: Write failing test**

```cpp
// test_parse_export.cpp
#include "obv_core/parse.h"
#include <cassert>
#include <iostream>

// Minimal BRD-like fixture: prefer a real tiny sample under fixtures/
// If no binary fixture yet, test error path:
static void test_unrecognized_fails() {
  std::vector<char> buf = {'n','o','p','e'};
  obv::DecryptKeys keys;
  auto snap = obv::ParseBoardBuffer(buf, "x.bin", keys);
  assert(!snap.ok());
  assert(!snap.error.empty());
}

int main() {
  test_unrecognized_fails();
  std::cout << "ok\n";
  return 0;
}
```

- [ ] **Step 2: Implement format detect chain in `parse.cpp`**

Port the `if/else` chain from `BoardView::LoadFile` (lines ~253–288) exactly:
- `.fz` / `.cae` encrypted by extension
- ASC/BOM, GenCAD, AD, CAD, CST, BRD, BRD2, BDV, BVR, BVR3, Allegro, XZZ, JSON
- Set `snap.error` from `m_file->error_msg` or `"Unrecognized file format."`
- On success: ensure outline (≥3 points or segments) using the same margin rectangle fallback as `LoadBoard`
- `snap.board = std::make_unique<BRDBoard>(snap.file.get());` — **ownership note:** today `BRDBoard` only stores raw `const BRDFileBase*`; keep `file` alive in snapshot for board lifetime
- Compute `bounds` from outline points/segments (copy from `LoadBoard`)

Read key types from `GUI/Config.h` / `FZFile.h` before filling `DecryptKeys`.

- [ ] **Step 3: Wire `obv_core_tests` executable**

```cmake
add_executable(obv_core_tests test_parse_export.cpp)
target_link_libraries(obv_core_tests PRIVATE obv_core)
add_test(NAME obv_core_tests COMMAND obv_core_tests)
```

Enable testing in root or `src/CMakeLists.txt` when core on: `enable_testing()`.

- [ ] **Step 4: Run test**

```bash
cmake --build build-web --target obv_core_tests -j
./build-web/src/obv_core_tests/obv_core_tests   # adjust path to actual output
```

Expected: prints `ok`, exit 0.

- [ ] **Step 5: Add one real fixture if available**

Place a small supported board under `src/obv_core_tests/fixtures/sample.brd` (or `.json`). Add `test_parse_sample_ok` asserting `snap.ok()`, `!Pins().empty()` or outline non-empty. If repo has no redistributable sample, document using a local path via env `OBV_TEST_BOARD` and skip if unset:

```cpp
if (const char* p = std::getenv("OBV_TEST_BOARD")) {
  auto snap = obv::ParseBoardFile(p, {});
  assert(snap.ok());
}
```

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(core): ParseBoardBuffer/File with BoardSnapshot"
```

---

### Task 3: Board JSON export

**Files:**
- Create: `src/obv_core/include/obv_core/board_json.h`
- Create: `src/obv_core/src/board_json.cpp`
- Modify: `src/obv_core_tests/test_parse_export.cpp` — assert JSON contains schema version

**Interfaces:**
```cpp
namespace obv {
// boardSchemaVersion = 1
std::string ExportBoardJson(const BoardSnapshot &snap, const std::string &boardId);
std::string ExportMetaJson(const BoardSnapshot &snap, const std::string &boardId);
}
```

JSON shape **must** match design spec §5.2 field set:
`boardSchemaVersion`, `boardId`, `sourceName`, `bounds`, `sides`, `outline`, `nets`, `components`, `pins`, `tracks`, `vias`, `arcs`.

**Pin id rule (lock in this task):**  
`id = componentName + "." + pin.number` when component present, else `"nail." + number + "." + index`.  
Document in comment at top of `board_json.cpp`.

- [ ] **Step 1: Write failing assertion for schema key**

```cpp
static void test_export_has_schema() {
  // Use env board or synthesize: if parse fails, skip
  const char* p = std::getenv("OBV_TEST_BOARD");
  if (!p) { std::cout << "skip export\n"; return; }
  auto snap = obv::ParseBoardFile(p, {});
  assert(snap.ok());
  auto js = obv::ExportBoardJson(snap, "testid");
  assert(js.find("\"boardSchemaVersion\":1") != std::string::npos);
  assert(js.find("\"pins\"") != std::string::npos);
}
```

- [ ] **Step 2: Implement exporter**

Prefer **nlohmann/json** vendored at `src/obv_core/third_party/json.hpp` OR manual `std::ostringstream` if avoiding dep — pick nlohmann for correctness on escaping.

Map enums:
- `EBoardSide` → `"top"`, `"bottom"`, `"both"`, or `"sN"` for multi-side
- mount/type from `Component` enums to lowercase strings

Do not include overlay fields in board JSON.

- [ ] **Step 3: Run tests + commit**

```bash
cmake --build build-web --target obv_core_tests -j && ./build-web/.../obv_core_tests
git commit -am "feat(core): export boardSchemaVersion 1 JSON"
```

---

### Task 4: Overlay store (YAML + SQLite)

**Files:**
- Create: `src/obv_core/include/obv_core/overlay_store.h`
- Create: `src/obv_core/src/overlay_store.cpp`
- Extend tests for YAML round-trip

**Interfaces:**
```cpp
namespace obv {
struct OverlayDocument {
  // serializable form for API
  // annotations: vector of {id,side,x,y,net,part,pin,note,visible}
  // partInfos / netInfos maps matching annotations.h
};

bool LoadOverlayForBoard(const filesystem::path &boardPath, Annotations &out, std::string &err);
bool SavePartNetYaml(const filesystem::path &boardPath, const Annotations &ann, std::string &err);
// Freeform annotations: wrap Annotations::Add/Update/Remove/Load after SetFilename
std::string ExportOverlayJson(const Annotations &ann);
bool ApplyOverlayJson(Annotations &ann, const std::string &json, std::string &err); // for PUT parts/nets
}
```

- [ ] **Step 1: Implement load/save using existing `Annotations`**

```cpp
bool LoadOverlayForBoard(const filesystem::path &boardPath, Annotations &out, std::string &err) {
  out.SetFilename(boardPath.string());
  out.Load();           // sqlite path when HAVE_SQLITE3
  out.RefreshPinInfos(); // yaml
  return true;
}
```

Save pin/net infos: call `SavePinInfos()` after mutating `partInfos`/`netInfos`.

- [ ] **Step 2: JSON export/import for API**

Map annotations vector + PartInfos + NetInfos to JSON. PUT replaces PartInfos/NetInfos maps; annotation CRUD uses discrete methods matching desktop SQL.

- [ ] **Step 3: Round-trip test**

Create temp dir, write empty yaml via Save, Load, assert no crash; if SQLite enabled, Add annotation, Load, assert count 1.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(core): overlay load/save and JSON for API"
```

---

### Task 5: `obv_server` skeleton — config, health, static

**Files:**
- Create: `src/obv_server/CMakeLists.txt`
- Create: `src/obv_server/third_party/httplib.h` (vendor [cpp-httplib](https://github.com/yhirose/cpp-httplib) single header)
- Create: `src/obv_server/server_config.h`
- Create: `src/obv_server/server_config.cpp`
- Create: `src/obv_server/main.cpp`
- Modify: `src/CMakeLists.txt` — `option(ENABLE_OBV_SERVER ON)` → `add_subdirectory(obv_server)`

**Interfaces:**
```cpp
struct ServerConfig {
  std::string host = "127.0.0.1";
  int port = 8080;
  filesystem::path dataRoot; // contains boards/, overlays/, config/
  obv::DecryptKeys keys;
  size_t maxUploadBytes = 64 * 1024 * 1024;
  bool allowDelete = false;
};
ServerConfig LoadConfig(const filesystem::path &jsonOrTomlPath);
```

- [ ] **Step 1: Vendor httplib.h into `third_party/`**

- [ ] **Step 2: main listens and mounts routes**

```cpp
#include "httplib.h"
int main(int argc, char** argv) {
  auto cfg = /* parse --config --host --port --data */;
  httplib::Server svr;
  svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"ok"})", "application/json");
  });
  svr.Get("/api/v1/version", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"server":"obv_server","core":"1"})", "application/json");
  });
  // static later: svr.set_mount_point("/", webDist);
  std::cout << "listening " << cfg.host << ":" << cfg.port << "\n";
  if (!svr.listen(cfg.host, cfg.port)) return 1;
  return 0;
}
```

- [ ] **Step 3: Run and curl**

```bash
cmake --build build-web --target obv_server -j
./build-web/src/obv_server/obv_server --port 8080 &
curl -s http://127.0.0.1:8080/api/v1/health
# Expected: {"status":"ok"}
```

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(server): obv_server health/version skeleton"
```

---

### Task 6: Board registry + upload/list/get

**Files:**
- Create: `src/obv_server/board_registry.h`
- Create: `src/obv_server/board_registry.cpp`
- Create: `src/obv_server/routes.cpp` (or keep in main until large)
- Modify: `main.cpp` — register board routes

**Interfaces:**
```cpp
class BoardRegistry {
public:
  explicit BoardRegistry(ServerConfig cfg);
  // content-addressed id = sha256 hex of file bytes
  struct Entry {
    std::string id;
    filesystem::path path;
    std::string name;
    std::string parseError;
    bool ok = false;
  };
  Entry ImportUpload(const std::string &originalName, const std::string &body);
  std::vector<Entry> List() const;
  std::shared_ptr<const obv::BoardSnapshot> GetParsed(const std::string &id);
  filesystem::path BoardPath(const std::string &id) const;
private:
  // map id → Entry; cache id → shared_ptr<BoardSnapshot>
  // re-parse if mtime changes
};
```

**Routes:**

| Method | Path | Behavior |
|--------|------|----------|
| GET | `/api/v1/boards` | JSON array of `{id,name,ok,error}` |
| POST | `/api/v1/boards` | multipart field `file` or raw body + `X-Filename`; write under `dataRoot/boards/<id>_<safeName>`; parse; return `{id, ok, error, meta}` |
| GET | `/api/v1/boards/:id` | `ExportBoardJson` or 404 |
| GET | `/api/v1/boards/:id/meta` | `ExportMetaJson` |
| DELETE | `/api/v1/boards/:id` | only if `allowDelete` |

Error envelope: `{"error":{"code":"PARSE_FAILED","message":"…"}}` with HTTP 400/404/413.

- [ ] **Step 1: Implement SHA-256 boardId**

Use a small portable sha256 (embed 1 file) or Windows BCrypt / OpenSSL if already linked — prefer **embedded public-domain sha256.c** under `obv_server/` to avoid new system deps.

- [ ] **Step 2: Implement ImportUpload + cache**

- [ ] **Step 3: Manual verification**

```bash
curl -s -F "file=@/path/to/sample.brd" http://127.0.0.1:8080/api/v1/boards
curl -s http://127.0.0.1:8080/api/v1/boards
curl -s http://127.0.0.1:8080/api/v1/boards/<id> | head -c 200
```

Expected: parse ok JSON starts with `{"boardSchemaVersion":1`

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(server): board upload list and JSON get"
```

---

### Task 7: Overlay HTTP API

**Files:**
- Modify: `board_registry.cpp` / `routes.cpp`
- Use: `obv::LoadOverlayForBoard`, `ExportOverlayJson`, annotation mutators

**Routes:**

| Method | Path |
|--------|------|
| GET | `/api/v1/boards/:id/overlays` |
| PUT | `/api/v1/boards/:id/overlays` |
| POST | `/api/v1/boards/:id/annotations` body `{"side":0,"x":..,"y":..,"net":"","part":"","pin":"","note":"…"}` |
| PATCH | `/api/v1/boards/:id/annotations/:annId` body `{"note":"…"}` |
| DELETE | `/api/v1/boards/:id/annotations/:annId` soft-delete |

- [ ] **Step 1: Implement handlers with per-board mutex**

- [ ] **Step 2: Verify persistence**

```bash
# create annotation, restart server, GET overlays — note still present
```

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(server): overlay and annotation CRUD"
```

---

### Task 8: Web app scaffold + API client

**Files:**
- Create: `web/package.json`, `vite.config.ts`, `tsconfig.json`, `index.html`
- Create: `web/src/main.tsx`, `web/src/ui/App.tsx`, `web/src/api/client.ts`, `web/src/types/board.ts`

**Interfaces (TS):**
```ts
// types/board.ts — mirror boardSchemaVersion 1
export interface BoardDocument { boardSchemaVersion: 1; boardId: string; /* … */ }

// api/client.ts
export async function listBoards(): Promise<BoardSummary[]>;
export async function uploadBoard(file: File): Promise<{id: string}>;
export async function getBoard(id: string): Promise<BoardDocument>;
export async function getOverlays(id: string): Promise<OverlayDocument>;
export async function postAnnotation(id: string, body: NewAnnotation): Promise<void>;
```

- [ ] **Step 1: Scaffold Vite React-TS**

```bash
cd web && npm create vite@latest . -- --template react-ts
npm install
```

- [ ] **Step 2: Proxy API in dev**

```ts
// vite.config.ts
export default defineConfig({
  server: { proxy: { '/api': 'http://127.0.0.1:8080' } },
  build: { outDir: 'dist' }
});
```

- [ ] **Step 3: App shows health + file input upload + board list**

- [ ] **Step 4: Verify**

```bash
# terminal1: obv_server
# terminal2: cd web && npm run dev
# browser: upload board, see id in list
```

- [ ] **Step 5: Commit**

```bash
git add web && git commit -m "feat(web): scaffold Vite app and board API client"
```

---

### Task 9: View transform + Canvas outline/pins

**Files:**
- Create: `web/src/scene/transform.ts`
- Create: `web/src/scene/draw.ts`
- Create: `web/src/scene/hitTest.ts`
- Create: `web/src/scene/BoardCanvas.tsx`

**Interfaces:**
```ts
export interface ViewState {
  scale: number;
  mx: number; // board center x
  my: number;
  rotation: 0|1|2|3; // quarters clockwise
  side: 'top' | 'bottom' | string;
  flipY: boolean;
  mirror: boolean;
}
export function boardToScreen(v: ViewState, x: number, y: number, cssW: number, cssH: number): {x:number;y:number};
export function screenToBoard(v: ViewState, sx: number, sy: number, cssW: number, cssH: number): {x:number;y:number};
export function centerOnBounds(bounds: Bounds, cssW: number, cssH: number): ViewState;
```

Port semantics from `BoardView::CoordToScreen` / `ScreenToCoord` / `CenterView` / `Rotate` / `FlipBoard` — read those functions in `BoardView.cpp` before coding; match axis flip conventions.

- [ ] **Step 1: Unit-test transform with vitest** (add devDependency)

```ts
// transform.test.ts
import { boardToScreen, screenToBoard, centerOnBounds } from './transform';
test('roundtrip center', () => {
  const v = centerOnBounds({minX:0,minY:0,maxX:100,maxY:50}, 200, 100);
  const s = boardToScreen(v, 50, 25, 200, 100);
  const b = screenToBoard(v, s.x, s.y, 200, 100);
  expect(b.x).toBeCloseTo(50, 1);
  expect(b.y).toBeCloseTo(25, 1);
});
```

- [ ] **Step 2: Draw outline segments + pins as circles; parts as polylines if outline present**

Layers order per spec §8.3 (subset): fill → outline → parts → pins → highlights.

- [ ] **Step 3: Wheel zoom (toward cursor), drag pan, buttons flip/rotate/reset**

- [ ] **Step 4: Hit-test nearest pin within threshold in board space**

- [ ] **Step 5: Manual visual check on sample board + commit**

```bash
cd web && npm test && npm run dev
git commit -am "feat(web): canvas board view with pan zoom flip"
```

---

### Task 10: Client search + highlight

**Files:**
- Create: `web/src/search/search.ts`
- Modify: `App.tsx`, `BoardCanvas.tsx`, `SearchBox.tsx`

**Interfaces:**
```ts
export type SearchMode = 'sub' | 'prefix' | 'whole';
export function searchParts(board: BoardDocument, q: string, mode: SearchMode, limit: number): string[]; // component names
export function searchNets(board: BoardDocument, q: string, mode: SearchMode, limit: number): string[]; // net names
```

Mirror `Searcher` / `SearchMode` in `Searcher.cpp` (case-insensitive substring default).

- [ ] **Step 1: Vitest cases for sub/prefix/whole**

- [ ] **Step 2: UI: typing filters highlights; click result centers view**

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(web): client part/net search and highlight"
```

---

### Task 11: Overlay editing UI

**Files:**
- Create: `web/src/ui/InfoPane.tsx`
- Modify: `api/client.ts`, `BoardCanvas.tsx` (context click → new annotation)

- [ ] **Step 1: Show selected pin/part/net fields from board JSON + overlay**

- [ ] **Step 2: Edit pin note/voltage/diode → PUT overlays (or granular API if added)**

For MVP, allow:
- POST annotation at right-click board coord
- PATCH annotation note
- Edit `PinInfo.note` / `show_name` via PUT full overlay document (read-modify-write)

- [ ] **Step 3: Two-browser check (no login multi-user)**

Browser A writes note; Browser B reloads overlays → sees update.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(web): annotation and pin overlay editing"
```

---

### Task 12: Serve web `dist` from `obv_server` + packaging

**Files:**
- Modify: `obv_server/main.cpp` — `set_mount_point("/", cfg.webRoot)`
- Modify: `CMakeLists` or script `scripts/build_web_release.sh`
- Create: `scripts/run_obv_server.sh` (or `.ps1` for Windows)
- Optional: extend root `Dockerfile` for server image

- [ ] **Step 1: Production static**

```bash
cd web && npm run build
# copy web/dist to build-web/web_dist or data/www
./obv_server --host 127.0.0.1 --port 8080 --www path/to/web/dist --data path/to/data
```

Open `http://127.0.0.1:8080/` — SPA loads; API same origin.

- [ ] **Step 2: SPA fallback**

For client routes, serve `index.html` on non-API 404 (httplib error handler).

- [ ] **Step 3: Document keys file format** under `data/config/keys.json` (example without real secrets)

- [ ] **Step 4: End-to-end checklist (manual)**

1. Health OK  
2. Upload board → canvas draw  
3. Search part → highlight  
4. Add annotation → restart server → still there  
5. Confirm `/api/v1/version` has no key material  

- [ ] **Step 5: Commit**

```bash
git commit -am "feat(server): host web dist and release run scripts"
```

---

### Task 13: Hardening pass (MVP exit)

**Files:** various

- [ ] **Step 1: Upload limits + extension allow-list** (reject crazy sizes; optional)

- [ ] **Step 2: Path traversal tests** on `boardId` (only `[0-9a-f]{64}`)

- [ ] **Step 3: gzip** — httplib or reverse proxy; at least document nginx example

- [ ] **Step 4: Large board smoke** — time `GET /boards/:id`; if >5s or >50MB JSON, file follow-up issue for split endpoints (do not block MVP if samples OK)

- [ ] **Step 5: Final commit + tag note in CHANGELOG if project uses one**

```bash
git commit -am "chore: MVP hardening for obv_server web path"
```

---

## Spec coverage checklist

| Spec section | Task(s) |
|--------------|---------|
| §1 Goals MVP view/search/overlay | 9–11 |
| §1 encrypted keys server-only | 2, 5, 13 |
| §4 obv_core / obv_server / web split | 1–8 |
| §5 board JSON schema | 3 |
| §5 overlay YAML/SQLite | 4, 7 |
| §6 HTTP API boards | 6 |
| §6 HTTP API overlays | 7 |
| §6 health/version | 5 |
| §7 parse pipeline | 2 |
| §8 Canvas + transform | 9 |
| §8 search client | 10 |
| §9 overlay write path | 7, 11 |
| §10 security bind/default | 5, 13 |
| §11 phases 1–5 | Tasks 1–13 |
| Desktop cutover optional | not required (explicit) |
| PDFBridge | out of scope |

## Placeholder / consistency self-review

- Pin id rule fixed in Task 3 (`component.number`).
- `boardSchemaVersion: 1` consistent across core export and TS types.
- Default host `127.0.0.1` in `ServerConfig` matches spec.
- No TBD steps left; fixture absence handled via `OBV_TEST_BOARD` env skip.
- HTTP paths all under `/api/v1` as spec.

---

## Execution notes for agents

1. Work on a branch `feature/web-server` (or user-chosen).
2. Prefer **subagent-driven-development**: one task per subagent, stop on failing verification.
3. Do not run full desktop packaging unless Task 1 needs compile proof.
4. Windows host: use `build-web` with MSVC or MinGW consistent with existing presets; paths in curl examples use Git Bash or PowerShell equivalents.
5. When touching `Board.h`, always rebuild **both** `obv_core` and `openboardview` in the same change set.
