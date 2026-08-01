# Part Screenshot + Pin show_name Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship agent REST tools that (1) render a cropped PNG of one part with pads + display labels and structured pin meta, and (2) batch-PATCH overlay `show_name` for automated silk label correction.

**Architecture:** Add `obv_core` part rasterizer (`part_render`) that consumes `Board` + `Annotations`, produces RGBA→PNG and matching meta JSON fields. Wire three routes under existing `/api/v1/boards/:ref/parts/:part/...` using `ResolveRef`, overlay mutex, and `SavePartNetYaml`. Reuse `PinOverlayKey` / `FindPartPin` / `FindComponent` from `pin_resolve`. Spec: `docs/superpowers/specs/2026-08-01-part-screenshot-pin-rename-design.md`.

**Tech Stack:** C++20, CMake, existing `obv_core`/`obv_server`, vendored `src/stb/stb_image_write.h` (+ optional `stb_truetype.h`), assert-based `obv_core_tests`, Python `scripts/test_agent_api.py`.

## Global Constraints

- Only overlay is writable; never mutate board-file `pin.name` / `pin.number`.
- Display label priority: **overlay `show_name` > board `show_name` > `name` > `number`**.
- board ref = existing `ResolveRef` (id | unique path | unique basename).
- Error body: `{ "error": { "code": "...", "message": "..." } }`.
- Unknown PATCH pin keys → **400** `UNKNOWN_PIN_KEY` (strict).
- Empty `show_name` clears overlay override.
- PNG max edge ≤ **2048**; default fit longest side **512**.
- No absolute server paths in JSON (`sourceName` = displayPath).
- No headless Chrome; CPU raster only.
- Screenshot draws **only** part outline + that part’s pads + labels (+ optional part name) — no tracks/vias/other parts.
- Prefer small focused files; TDD in `obv_core_tests`; extend HTTP suite in `scripts/test_agent_api.py`.
- Every task ends with verification + commit.
- Skip formatters/linters; no gtest.

---

## File structure (create / modify)

```
src/obv_core/
  include/obv_core/
    part_render.h              # CREATE: opts, meta structs, RenderPartScreenshot
  src/
    part_render.cpp            # CREATE: bounds, draw, PNG encode
  CMakeLists.txt               # add part_render.cpp; include path to src/stb

src/obv_core_tests/
  test_parse_export.cpp        # OR test_part_render.cpp + CMake — prefer new test_part_render.cpp if large

src/obv_server/
  routes.cpp                   # GET screenshot, GET screenshot/meta, PATCH pins
  CMakeLists.txt               # only if server needs extra includes (prefer all render in obv_core)

scripts/
  test_agent_api.py            # extend HTTP cases

docs/superpowers/specs/
  2026-08-01-part-screenshot-pin-rename-design.md  # read-only for implementers
```

**Reuse (do not reimplement):**
- `obv::PinOverlayKey`, `FindPartPin`, `FindComponent` — `pin_resolve.h`
- `withPartOverlay` / `applyBoardRef` / `publicSourceName` / `setError` / `MiniJson` — `routes.cpp`
- `SavePartNetYaml` / `LoadOverlayForBoard` — `overlay_store`
- STB headers already at `src/stb/stb_image_write.h`, `src/stb/stb_truetype.h`
- Optional font: copy or reference `src/imgui/misc/fonts/ProggyClean.ttf` (or Roboto-Medium.ttf) as runtime load path relative to executable **or** embed via `xxd`/string — **MVP: 5×7 bitmap font only** if TTF path is painful; labels must still work.

---

### Task 1: Display label helper + crop bounds (pure logic)

**Files:**
- Create: `src/obv_core/include/obv_core/part_render.h`
- Create: `src/obv_core/src/part_render.cpp`
- Modify: `src/obv_core/CMakeLists.txt` (add source + `${CMAKE_CURRENT_SOURCE_DIR}/../stb` include for later PNG)
- Test: `src/obv_core_tests/test_part_render.cpp` (new) + `src/obv_core_tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
namespace obv {

struct PartRenderOpts {
  double scale = 0;       // 0 = auto from maxEdge
  double padding = -1;    // <0 = auto 5% of max(w,h), min 1.0
  int maxEdge = 512;      // 64..2048
  bool labels = true;
  bool partName = true;
};

struct PartRenderBounds {
  double minX = 0, minY = 0, maxX = 0, maxY = 0;
  double padding = 0;
};

// overlay show_name > board show_name > name > number
std::string PinDisplayLabel(const Pin &pin, const Annotations *ann /*nullable*/);

// Returns false if part missing or no outline and no pins
bool ComputePartBounds(const Board &board, const std::string &part,
                       double paddingOrAuto, PartRenderBounds &out, std::string &errCode);
// errCode: PART_NOT_FOUND | PART_NO_GEOMETRY

// Board → image pixel (flipY=true):
// imageX = (boardX - originBoardX) * scale
// imageY = (originBoardY - boardY) * scale
struct BoardToImage {
  double originBoardX = 0;
  double originBoardY = 0; // top of crop in board Y when flipY
  double scale = 1;
  bool flipY = true;
  int width = 0;
  int height = 0;
};

// Build transform from bounds + opts; clamps max edge to 2048 / opts.maxEdge
bool BuildBoardToImage(const PartRenderBounds &b, const PartRenderOpts &opts,
                       BoardToImage &out, std::string &err);

void BoardToImagePoint(const BoardToImage &t, double boardX, double boardY,
                       double &imageX, double &imageY);

} // namespace obv
```

- [ ] **Step 1: Add test file + CMake**

`src/obv_core_tests/CMakeLists.txt` — either second executable or add source to existing:

```cmake
# Prefer single binary with two sources:
add_executable(obv_core_tests test_parse_export.cpp test_part_render.cpp)
```

If current file is only `test_parse_export.cpp`, change to list both.

- [ ] **Step 2: Write failing tests**

`test_part_render.cpp`:

```cpp
#include "obv_core/part_render.h"
#include "annotations.h"
#include "Board.h"
#include <cassert>
#include <cmath>
#include <iostream>

static void test_pin_display_label_priority() {
  Pin pin;
  pin.name = "netish";
  pin.number = "3";
  pin.show_name = "BOARD_SN";

  // no overlay → board show_name
  assert(obv::PinDisplayLabel(pin, nullptr) == "BOARD_SN");

  pin.show_name.clear();
  assert(obv::PinDisplayLabel(pin, nullptr) == "netish");

  pin.name.clear();
  assert(obv::PinDisplayLabel(pin, nullptr) == "3");

  // overlay wins
  pin.name = "netish";
  pin.show_name = "BOARD_SN";
  pin.component = std::make_shared<Component>();
  pin.component->name = "U1";
  Annotations ann;
  auto &pi = ann.NewPinInfo("U1", "netish"); // key = name
  pi.show_name = "OV_SN";
  assert(obv::PinDisplayLabel(pin, &ann) == "OV_SN");

  // empty overlay falls through
  pi.show_name = "  ";
  assert(obv::PinDisplayLabel(pin, &ann) == "BOARD_SN");
  std::cout << "pin display label ok\n";
}

static void test_board_to_image_flip_y() {
  obv::PartRenderBounds b;
  b.minX = 0; b.minY = 0; b.maxX = 100; b.maxY = 50; b.padding = 0;
  // recompute with padding already applied in bounds
  obv::PartRenderOpts opts;
  opts.scale = 2.0;
  opts.maxEdge = 2048;
  obv::BoardToImage t;
  std::string err;
  assert(obv::BuildBoardToImage(b, opts, t, err));
  assert(t.width == 200);
  assert(t.height == 100);
  assert(t.flipY);
  double ix, iy;
  obv::BoardToImagePoint(t, 0, 50, ix, iy); // top of board box → y=0
  assert(std::fabs(ix - 0) < 1e-6);
  assert(std::fabs(iy - 0) < 1e-6);
  obv::BoardToImagePoint(t, 100, 0, ix, iy); // bottom-right
  assert(std::fabs(ix - 200) < 1e-6);
  assert(std::fabs(iy - 100) < 1e-6);
  std::cout << "board to image ok\n";
}

int main() {
  test_pin_display_label_priority();
  test_board_to_image_flip_y();
  // ComputePartBounds needs a real Board — covered in Task 2 with OBV_TEST_BOARD or synthetic if available
  std::cout << "part_render unit ok\n";
  return 0;
}
```

Note: `Pin::component` is `shared_ptr<Component>` — match `Board.h`. `NewPinInfo` keys must match `PinOverlayKey(pin)` (`name` first).

- [ ] **Step 3: Run tests — expect compile fail**

```bash
cmake --build build-core-only --config Debug --target obv_core_tests
```

- [ ] **Step 4: Implement header + label + transform + bounds skeleton**

`part_render.h` — declare structs/functions above.

`part_render.cpp` — implement:

```cpp
static std::string trim(const std::string &s) { /* ... */ }

std::string PinDisplayLabel(const Pin &pin, const Annotations *ann) {
  if (ann && pin.component) {
    const std::string part = pin.component->name;
    const std::string key = PinOverlayKey(pin);
    auto pit = ann->partInfos.find(part);
    if (pit != ann->partInfos.end()) {
      auto kit = pit->second.pins.find(key);
      if (kit != pit->second.pins.end()) {
        const std::string ov = trim(kit->second.show_name);
        if (!ov.empty()) return ov;
      }
    }
  }
  if (!trim(pin.show_name).empty()) return trim(pin.show_name);
  if (!trim(pin.name).empty()) return trim(pin.name);
  return trim(pin.number);
}

bool BuildBoardToImage(const PartRenderBounds &b, const PartRenderOpts &opts,
                       BoardToImage &out, std::string &err) {
  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;
  if (!(w > 0) || !(h > 0)) { err = "empty bounds"; return false; }
  int maxEdge = opts.maxEdge;
  if (maxEdge < 64) maxEdge = 64;
  if (maxEdge > 2048) maxEdge = 2048;
  double scale = opts.scale;
  if (scale <= 0) {
    scale = static_cast<double>(maxEdge) / std::max(w, h);
  }
  if (scale <= 0) { err = "bad scale"; return false; }
  int iw = static_cast<int>(std::ceil(w * scale));
  int ih = static_cast<int>(std::ceil(h * scale));
  if (iw < 1) iw = 1;
  if (ih < 1) ih = 1;
  // clamp longest edge
  const int longEdge = std::max(iw, ih);
  if (longEdge > maxEdge) {
    const double f = static_cast<double>(maxEdge) / longEdge;
    scale *= f;
    iw = std::max(1, static_cast<int>(std::ceil(w * scale)));
    ih = std::max(1, static_cast<int>(std::ceil(h * scale)));
  }
  if (std::max(iw, ih) > 2048) { err = "image too large"; return false; }
  out.originBoardX = b.minX;
  out.originBoardY = b.maxY; // top
  out.scale = scale;
  out.flipY = true;
  out.width = iw;
  out.height = ih;
  return true;
}

void BoardToImagePoint(const BoardToImage &t, double bx, double by,
                       double &ix, double &iy) {
  ix = (bx - t.originBoardX) * t.scale;
  iy = t.flipY ? (t.originBoardY - by) * t.scale : (by - /*minY*/ (t.originBoardY)) * t.scale;
}
```

`ComputePartBounds`: find component via `FindComponent`; collect outline points + pin centers ± half diameter/size; apply padding (auto if `paddingOrAuto < 0`: `0.05 * max(w,h)` then `max(pad, 1.0)`); set errCode.

Wire CMake:

```cmake
# in OBV_CORE_SOURCES
${CMAKE_CURRENT_SOURCE_DIR}/src/part_render.cpp

target_include_directories(obv_core PUBLIC
  ...
  ${CMAKE_CURRENT_SOURCE_DIR}/../stb   # for later tasks; ok to add now
)
```

- [ ] **Step 5: Run tests — expect pass**

```bash
cmake --build build-core-only --config Debug --target obv_core_tests
./build-core-only/src/obv_core_tests/Debug/obv_core_tests.exe
# or run only if main merges — if two mains, SINGLE main must call all tests.
```

**Important:** only **one** `main`. Either:
- put part_render tests into `test_parse_export.cpp`, or
- make `test_part_render.cpp` functions called from existing `main`.

**Plan decision:** add tests as functions in `test_parse_export.cpp` **or** export `run_part_render_tests()` from `test_part_render.cpp` and call from `test_parse_export.cpp` main. Simplest: **append tests to `test_parse_export.cpp`** unless file huge — then separate compilation unit with `void run_part_render_tests();` declared.

- [ ] **Step 6: Commit**

```bash
git add src/obv_core/include/obv_core/part_render.h src/obv_core/src/part_render.cpp \
  src/obv_core/CMakeLists.txt src/obv_core_tests/
git commit -m "feat(core): part render bounds, transform, display label helpers"
```

---

### Task 2: Software raster + PNG encode

**Files:**
- Modify: `src/obv_core/src/part_render.cpp`
- Modify: `src/obv_core/include/obv_core/part_render.h`
- Test: `src/obv_core_tests/test_parse_export.cpp` (or part_render tests)

**Interfaces:**
- Produces:

```cpp
struct PartPinMeta {
  std::string key, id, number, name;
  std::string boardShowName, overlayShowName, displayLabel;
  double boardX = 0, boardY = 0;
  double imageX = 0, imageY = 0;
  std::string type, shape;
  double diameter = 0;
  std::string netName;
};

struct PartScreenshotMeta {
  std::string part;
  BoardToImage transform;
  PartRenderBounds bounds; // final padded bounds
  PartRenderOpts optsUsed;
  std::vector<PartPinMeta> pins;
};

struct PartScreenshotResult {
  std::string png; // binary
  PartScreenshotMeta meta;
};

// errCode: PART_NOT_FOUND | PART_NO_GEOMETRY | RENDER_FAILED | BAD_REQUEST
bool RenderPartScreenshot(const Board &board, const Annotations &ann,
                          const std::string &part, const PartRenderOpts &opts,
                          PartScreenshotResult &out, std::string &errCode,
                          std::string &errMessage);
```

- [ ] **Step 1: Failing test — PNG magic + non-empty meta pins (needs board)**

```cpp
static void test_render_part_png_if_env() {
  const char *p = std::getenv("OBV_TEST_BOARD");
  if (!p) { std::cout << "skip part render png\n"; return; }
  obv::DecryptKeys keys;
  auto snap = obv::ParseBoardFile(p, keys);
  assert(snap.ok());
  // pick first component with pins
  std::string partName;
  for (const auto &c : snap.board->Components()) {
    if (c && !c->name.empty() && !c->pins.empty()) { partName = c->name; break; }
  }
  assert(!partName.empty());
  Annotations ann;
  obv::PartRenderOpts opts;
  opts.maxEdge = 256;
  obv::PartScreenshotResult r;
  std::string code, msg;
  assert(obv::RenderPartScreenshot(*snap.board, ann, partName, opts, r, code, msg));
  assert(r.png.size() >= 8);
  assert(static_cast<unsigned char>(r.png[0]) == 0x89);
  assert(r.png[1] == 'P' && r.png[2] == 'N' && r.png[3] == 'G');
  assert(r.meta.transform.width > 0 && r.meta.transform.height > 0);
  assert(!r.meta.pins.empty());
  for (const auto &pm : r.meta.pins) {
    assert(pm.imageX >= -1 && pm.imageX <= r.meta.transform.width + 1);
    assert(pm.imageY >= -1 && pm.imageY <= r.meta.transform.height + 1);
  }
  std::cout << "part render png ok part=" << partName
            << " bytes=" << r.png.size() << "\n";
}
```

Also always-run unit test without board: raster buffer fill + encode 1×1 or synthetic — optional `EncodePng` if exported.

- [ ] **Step 2: Run — fail missing RenderPartScreenshot**

- [ ] **Step 3: Implement raster**

Internal:

```cpp
struct Rgba { std::vector<uint8_t> px; int w, h; };
void fill(Rgba&, int r,g,b,a);
void fillCircle(...);
void fillPolygon(...); // scanline or triangle fan for outline
void fillRotatedRect(...); // for rect pins using pin.angle
void drawText5x7(...); // bitmap font MVP
```

Colors (spec defaults):
- bg `#1a1d24`
- outline fill `rgba(80,90,110,0.35)` → blend on bg
- outline stroke `#8b93a7`
- pad `#6ec6ff`
- A1/pin1 `#dd0000` when part pins ≥ 3 (same rule as web)
- label `#e8eaed` with dark halo (draw black offsets then white/light)
- part name `#c5cad3`

PNG:

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// stbi_write_png_to_func into std::string
```

Algorithm `RenderPartScreenshot`:
1. `ComputePartBounds` with opts.padding  
2. `BuildBoardToImage`  
3. Allocate RGBA `width*height*4`  
4. Fill bg  
5. Transform outline points → image; fill polygon + stroke  
6. For each pin of part: draw pad; collect PartPinMeta with labels  
7. If labels: drawText at pad  
8. If partName: draw part name near top of image  
9. Encode PNG  
10. Fill meta.optsUsed with actual scale/padding/flags  

On failure set `errCode` appropriately.

- [ ] **Step 4: Run tests**

```bash
cmake --build build-core-only --config Debug --target obv_core_tests
# without env: skip png, unit tests pass
# with env: OBV_TEST_BOARD=path/to.bvr
```

- [ ] **Step 5: Commit**

```bash
git add src/obv_core/include/obv_core/part_render.h src/obv_core/src/part_render.cpp src/obv_core_tests/
git commit -m "feat(core): CPU part screenshot raster to PNG"
```

---

### Task 3: Meta JSON export helper

**Files:**
- Modify: `part_render.h` / `part_render.cpp`
- Optional tiny test: string contains `"pins"` and `"transform"`

**Interfaces:**
- Produces:

```cpp
// boardId/sourceName filled by caller
std::string ExportPartScreenshotMetaJson(const std::string &boardId,
                                         const std::string &sourceName,
                                         const PartScreenshotMeta &meta);
```

JSON shape **exactly** as design §5.2 (`image`, `boardBounds`, `transform.boardToImage`, `pins[]` fields).

- [ ] **Step 1: Test**

```cpp
static void test_meta_json_shape() {
  obv::PartScreenshotMeta m;
  m.part = "U1";
  m.transform.width = 10; m.transform.height = 8; m.transform.scale = 1;
  m.transform.originBoardX = 0; m.transform.originBoardY = 8; m.transform.flipY = true;
  m.bounds = {0,0,10,8,0};
  m.optsUsed.labels = true; m.optsUsed.partName = true; m.optsUsed.maxEdge = 512;
  obv::PartPinMeta pm;
  pm.key = "1"; pm.displayLabel = "VCC"; pm.imageX = 1; pm.imageY = 2;
  m.pins.push_back(pm);
  auto js = obv::ExportPartScreenshotMetaJson("abcd", "x.bvr", m);
  assert(js.find("\"pins\"") != std::string::npos);
  assert(js.find("\"boardToImage\"") != std::string::npos);
  assert(js.find("\"VCC\"") != std::string::npos);
  assert(js.find("\"flipY\":true") != std::string::npos);
  std::cout << "meta json ok\n";
}
```

- [ ] **Step 2–4: Implement hand-rolled JSON (same style as pin_resolve Export*) → green → commit**

```bash
git commit -m "feat(core): export part screenshot meta JSON"
```

---

### Task 4: HTTP GET screenshot + meta

**Files:**
- Modify: `src/obv_server/routes.cpp`

**Interfaces:**
- Consumes: `RenderPartScreenshot`, `ExportPartScreenshotMetaJson`, `applyBoardRef`, `publicSourceName`, overlay load for labels
- Produces:
  - `GET /api/v1/boards/:ref/parts/:part/screenshot`
  - `GET /api/v1/boards/:ref/parts/:part/screenshot/meta`

- [ ] **Step 1: Query parser helper**

```cpp
bool parsePartRenderOpts(const httplib::Request &req, obv::PartRenderOpts &opts, std::string &err) {
  opts = {};
  if (req.has_param("scale")) {
    opts.scale = std::atof(req.get_param_value("scale").c_str());
    if (!(opts.scale > 0) || opts.scale > 100) { err = "invalid scale"; return false; }
  }
  if (req.has_param("padding")) {
    opts.padding = std::atof(req.get_param_value("padding").c_str());
    if (opts.padding < 0) { err = "invalid padding"; return false; }
  }
  if (req.has_param("maxEdge")) {
    opts.maxEdge = std::atoi(req.get_param_value("maxEdge").c_str());
    if (opts.maxEdge < 64 || opts.maxEdge > 2048) { err = "invalid maxEdge"; return false; }
  }
  if (req.has_param("labels")) opts.labels = req.get_param_value("labels") != "0";
  if (req.has_param("partName")) opts.partName = req.get_param_value("partName") != "0";
  return true;
}
```

- [ ] **Step 2: Register routes** (more specific `screenshot/meta` **before** `screenshot` if needed)

```cpp
// GET meta
svr.Get(R"(/api/v1/boards/:ref/parts/:part/screenshot/meta)", ...);
// GET png
svr.Get(R"(/api/v1/boards/:ref/parts/:part/screenshot)", ...);
```

Shared body:
1. `applyBoardRef`  
2. `GetParsed` → PARSE_FAILED / NOT_FOUND  
3. `parsePartRenderOpts` → BAD_REQUEST  
4. Lock overlay; `LoadOverlayForBoard` (labels need overlay; if load fails → 500)  
5. `RenderPartScreenshot`  
6. meta: `ExportPartScreenshotMetaJson` + `application/json`  
7. png: `res.set_content(r.png, "image/png")` + optional `X-Image-Width` headers  

Map errCode:
- PART_NOT_FOUND → 404  
- PART_NO_GEOMETRY / BAD_REQUEST → 400  
- RENDER_FAILED → 500  

- [ ] **Step 3: Build server**

```bash
cmake --build build-web --config Release --target obv_server
```

- [ ] **Step 4: Commit**

```bash
git add src/obv_server/routes.cpp
git commit -m "feat(server): GET part screenshot PNG and meta"
```

---

### Task 5: HTTP PATCH pins show_name

**Files:**
- Modify: `src/obv_server/routes.cpp`
- Reuse: `withPartOverlay`, `FindPartPin`, `PinOverlayKey`, `PinDisplayLabel`

**Interfaces:**
- Produces: `PATCH /api/v1/boards/:ref/parts/:part/pins`

Request parse:

```cpp
// { "pins": { "1": { "show_name": "VCC" }, ... } }
bool parsePinsShowNamePatch(const std::string &json,
  std::map<std::string, std::string> &out, // key -> show_name (may be empty = clear)
  std::string &err);
```

Rules from spec:
- max 512 keys  
- show_name length ≤ 128 after trim  
- reject unknown fields inside pin object  
- empty pins → BAD_REQUEST  

Handler:

```cpp
svr.Patch(R"(/api/v1/boards/:ref/parts/:part/pins)", [&](...) {
  // resolve board, parse body
  // withPartOverlay(..., requirePartOnBoard=true, [&](ann, pi, res) {
  //   for each key:
  //     const Pin* p = FindPartPin(*board, part, key);
  //     if (!p) collect unknown
  //   if any unknown → setError 400 UNKNOWN_PIN_KEY; return false
  //   for each: canonicalKey = PinOverlayKey(*p);
  //     if show_name empty: clear pinInfo.show_name (erase field)
  //     else set NewPinInfo / pins[canonicalKey].show_name = value
  //   return true;
  // });
  // respond { boardId, part, updated: [{key, show_name, displayLabel}] }
});
```

Need `Board` inside `withPartOverlay` — load snap before/inside (already have GetParsed in helper). Extend helper or capture `snap` in lambda from outer GetParsed.

- [ ] **Step 1: Implement parse + route**

- [ ] **Step 2: Build**

```bash
cmake --build build-web --config Release --target obv_server
```

- [ ] **Step 3: Commit**

```bash
git add src/obv_server/routes.cpp
git commit -m "feat(server): PATCH batch pin show_name for agent rename"
```

---

### Task 6: Extend `scripts/test_agent_api.py` + manual smoke

**Files:**
- Modify: `scripts/test_agent_api.py`

**Cases to add:**

| name | assert |
|------|--------|
| `screenshot.png_magic` | GET screenshot 200; body starts with PNG magic; Content-Type image/png |
| `screenshot.meta_shape` | meta has pins[], transform, image width/height; pin image coords in range |
| `screenshot.meta_matches_query` | `?maxEdge=128` → width,height ≤ 128 |
| `pins.patch_show_name` | PATCH one pin; meta overlayShowName/displayLabel updated; GET pin resolve overlay.show_name |
| `pins.patch_clear` | PATCH `show_name:""`; display falls back |
| `pins.patch_unknown_key` | 400 UNKNOWN_PIN_KEY |
| `pins.patch_empty` | 400 BAD_REQUEST |

Implementation notes:
- For binary PNG, `request()` helper currently JSON-parses — add `request_raw` that returns `bytes` without JSON parse.
- Cleanup: restore show_name after tests (clear or previous value).

- [ ] **Step 1: Implement cases + raw GET**

- [ ] **Step 2: Run suite**

```bash
python scripts/test_agent_api.py --boards data/boards --port 18083
```

Expected: all previous + new cases PASS.

- [ ] **Step 3: Commit**

```bash
git add scripts/test_agent_api.py
git commit -m "test: agent screenshot and pin show_name PATCH cases"
```

---

### Task 7: End-to-end verification checklist

**Files:** none (or brief report under `docs/superpowers/briefs/` if desired)

- [ ] **Step 1: Start server**

```bash
build-web/src/obv_server/Release/obv_server.exe --host 127.0.0.1 --port 8080 --boards data/boards --data data
```

- [ ] **Step 2: Curl smoke**

```bash
# pick boardId + part from /api/v1/boards and board JSON
curl -sS "http://127.0.0.1:8080/api/v1/boards/BOARD/parts/PART/screenshot/meta" | head
curl -sS "http://127.0.0.1:8080/api/v1/boards/BOARD/parts/PART/screenshot" -o /tmp/part.png
file /tmp/part.png   # PNG image

curl -sS -X PATCH "http://127.0.0.1:8080/api/v1/boards/BOARD/parts/PART/pins" \
  -H "Content-Type: application/json" \
  -d "{\"pins\":{\"PINKEY\":{\"show_name\":\"AGENT_TEST\"}}}"

curl -sS "http://127.0.0.1:8080/api/v1/boards/BOARD/parts/PART/screenshot/meta" | findstr AGENT_TEST
```

- [ ] **Step 3: Spec checklist**

| Requirement | OK |
|-------------|-----|
| PNG crop outline+pads+labels | |
| Meta pins without OCR | |
| Label priority overlay>board>name>number | |
| PATCH batch show_name | |
| Clear empty show_name | |
| Unknown key 400 | |
| No absolute paths | |
| Max edge clamp | |

- [ ] **Step 4: Commit only if fixes**

```bash
git commit -m "fix: part screenshot/rename smoke fixes"
```

---

## Self-review (plan vs spec)

| Spec | Task |
|------|------|
| §3 Display label | Task 1 |
| §4 Raster layers + params | Task 2 |
| §5.1 PNG / §5.2 meta API | Tasks 3–4 |
| §6 PATCH pins | Task 5 |
| §7 Agent loop | Task 6–7 (documented usage) |
| §8 Errors | Tasks 4–5 |
| §9 Testing | Tasks 1–2, 6–7 |
| Non-goals (no Chrome, no copper) | honored in Task 2 draw list |

**Placeholder scan:** none.  
**Type consistency:** `PartRenderOpts` / `PartScreenshotResult` / `RenderPartScreenshot` used Tasks 2→4; `PinDisplayLabel` Tasks 1→2→5.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-01-part-screenshot-pin-rename.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with checkpoints  

Which approach?
