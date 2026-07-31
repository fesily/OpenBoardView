# Agent Pin / Part Operating-Conditions API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship agent-facing REST tools: resolve a board part pin to a full measurement snapshot (including net propagation), and CRUD multi-group `operating_conditions` on `PartInfo` via overlay YAML.

**Architecture:** Extend shared `PartInfo` (desktop YAML + server overlay) with `operating_conditions[]`. Add `obv_core` pin-resolve helpers that mirror `web/src/scene/pinValues.ts`. Add `BoardRegistry::ResolveRef` for `boardId` or unique library path/name. Register new routes under `/api/v1/boards/:ref/parts/...`. Spec: `docs/superpowers/specs/2026-07-31-agent-pin-part-api-design.md`.

**Tech Stack:** C++20, CMake, `obv_core` + `obv_server` (cpp-httplib), existing assert-based `obv_core_tests`, YAML via ryml (`annotations.cpp`), hand-rolled JSON in `overlay_store.cpp` / `routes.cpp`.

## Global Constraints

- Only overlay is writable; geometry/netlist remain read-only.
- Responses never leak absolute server filesystem paths (use `displayPath` / public `sourceName` only).
- Error body: `{ "error": { "code": "...", "message": "..." } }` — same as existing routes.
- Delete/missing condition must **404**, never silent success.
- Propagation priority: **overlay > board file field > same-net first local** (`pinValues.ts`).
- Desktop coexistence: YAML round-trip of `operating_conditions` must survive `SavePinInfos` / desktop save path.
- Project has no gtest; use `obv_core_tests` assert + `main`.
- Prefer small focused files; no MCP protocol server in this plan.
- Every task ends with a runnable verification command and a commit.
- Skip formatters/linters; no project-wide test suite beyond `obv_core_tests` and manual curl.

---

## File structure (create / modify)

```
src/openboardview/
  annotations.h                 # OperatingCondition + PartInfo field; operator bool
  annotations.cpp               # YAML read/write for operating_conditions

src/obv_core/
  include/obv_core/
    pin_resolve.h               # CREATE: pin key, find pin, measurement resolve, JSON builders
  src/
    pin_resolve.cpp             # CREATE
    overlay_store.cpp           # Export/Apply operating_conditions in partInfos JSON

src/obv_core_tests/
  test_parse_export.cpp         # Extend: conditions YAML/JSON + pin_resolve unit tests
  # OR split: test_pin_resolve.cpp + CMakeLists if file grows too large — prefer one file unless >500 new lines

src/obv_server/
  board_registry.h/.cpp         # ResolveRef(ref) → id | not_found | ambiguous
  routes.cpp                    # part/pin/conditions routes
  routes.h                      # comment update only if needed

docs/superpowers/specs/
  2026-07-31-agent-pin-part-api-design.md   # already approved (read-only for implementers)
```

**Do not modify (unless required for compile):** web frontend (optional consumer later).

---

### Task 1: `OperatingCondition` model + YAML round-trip

**Files:**
- Modify: `src/openboardview/annotations.h`
- Modify: `src/openboardview/annotations.cpp` (ryml `write`/`read` for `PartInfo` / new type)
- Test: `src/obv_core_tests/test_parse_export.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct OperatingCondition {
    std::string id;
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> enables;
    std::string note;
  };
  // on PartInfo:
  std::vector<OperatingCondition> operating_conditions;
  ```
- `PartInfo::operator bool` must be true when only `operating_conditions` is non-empty (prevents prune on `SavePinInfos`).

- [ ] **Step 1: Write the failing test**

Add to `test_parse_export.cpp`:

```cpp
static void test_operating_conditions_yaml_roundtrip() {
	const auto dir = filesystem::temp_directory_path() / "obv_core_oc_test";
	std::error_code ec;
	filesystem::create_directories(dir, ec);
	assert(!ec);
	const auto boardPath = dir / "board.brd";
	{
		std::ofstream touch(boardPath.string(), std::ios::trunc);
		assert(touch.good());
		touch << "x";
	}

	Annotations ann;
	ann.SetFilename(boardPath.string());
	auto &part = ann.NewPartInfo("U12");
	OperatingCondition oc;
	oc.id = "oc_01";
	oc.name = "UART0 TX path";
	oc.inputs = {"RXD0"};
	oc.outputs = {"TXD0"};
	oc.enables = {"UART_EN"};
	oc.note = "EN high, VCC 3.3";
	part.operating_conditions.push_back(oc);

	// Part with ONLY conditions (no pins/type) must still persist.
	auto &part2 = ann.NewPartInfo("U99");
	OperatingCondition oc2;
	oc2.id = "oc_a";
	oc2.outputs = {"Y"};
	part2.operating_conditions.push_back(oc2);

	std::string err;
	assert(obv::SavePartNetYaml(boardPath, ann, err));
	assert(err.empty());

	const auto yamlPath = boardPath.string() + ".yaml";
	{
		std::ifstream in(yamlPath);
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("operating_conditions") != std::string::npos);
		assert(content.find("oc_01") != std::string::npos);
		assert(content.find("UART_EN") != std::string::npos);
		assert(content.find("U99") != std::string::npos);
	}

	Annotations loaded;
	assert(obv::LoadOverlayForBoard(boardPath, loaded, err));
	assert(loaded.partInfos.count("U12") == 1);
	assert(loaded.partInfos["U12"].operating_conditions.size() == 1);
	const auto &got = loaded.partInfos["U12"].operating_conditions[0];
	assert(got.id == "oc_01");
	assert(got.name == "UART0 TX path");
	assert(got.inputs.size() == 1 && got.inputs[0] == "RXD0");
	assert(got.outputs.size() == 1 && got.outputs[0] == "TXD0");
	assert(got.enables.size() == 1 && got.enables[0] == "UART_EN");
	assert(got.note == "EN high, VCC 3.3");
	assert(loaded.partInfos.count("U99") == 1);
	assert(loaded.partInfos["U99"].operating_conditions.size() == 1);
	assert(loaded.partInfos["U99"].operating_conditions[0].id == "oc_a");

	// Cleanup best-effort
	filesystem::remove_all(dir, ec);
	std::cout << "operating_conditions yaml ok\n";
}
```

Call it from `main` before return.

- [ ] **Step 2: Run test to verify it fails**

```bash
# From repo build dir used for server (example):
cmake --build build --target obv_core_tests
./build/src/obv_core_tests/obv_core_tests
# or Windows: build\src\obv_core_tests\Debug\obv_core_tests.exe
```

Expected: compile error (`operating_conditions` not a member) or assert fail on missing YAML key.

- [ ] **Step 3: Implement model + YAML**

In `annotations.h`, after `NetInfo` (or before `PartInfo`):

```cpp
struct OperatingCondition {
	std::string id;
	std::string name;
	std::vector<std::string> inputs;
	std::vector<std::string> outputs;
	std::vector<std::string> enables;
	std::string note;
};
```

Add to `PartInfo`:

```cpp
std::vector<OperatingCondition> operating_conditions;
```

Update `PartInfo::operator bool`:

```cpp
explicit operator bool() const {
	return !(part_type.empty() && angle == PartAngle::_0 && pins.empty() &&
	         operating_conditions.empty());
}
```

In `annotations.cpp` ryml helpers, add sequence write/read for `OperatingCondition` and wire into `PartInfo` write/read:

```cpp
void write(c4::yml::NodeRef *node, const OperatingCondition &oc) {
	(*node) |= c4::yml::MAP;
	if (!oc.id.empty()) node->append_child() << key("id") << oc.id;
	if (!oc.name.empty()) node->append_child() << key("name") << oc.name;
	if (!oc.inputs.empty()) node->append_child() << key("inputs") << oc.inputs;
	if (!oc.outputs.empty()) node->append_child() << key("outputs") << oc.outputs;
	if (!oc.enables.empty()) node->append_child() << key("enables") << oc.enables;
	if (!oc.note.empty()) node->append_child() << key("note") << oc.note;
}

bool read(const c4::yml::ConstNodeRef &node, OperatingCondition *oc) {
	if (node.has_child("id")) node["id"] >> oc->id;
	if (node.has_child("name")) node["name"] >> oc->name;
	if (node.has_child("inputs")) node["inputs"] >> oc->inputs;
	if (node.has_child("outputs")) node["outputs"] >> oc->outputs;
	if (node.has_child("enables")) node["enables"] >> oc->enables;
	if (node.has_child("note")) node["note"] >> oc->note;
	return true;
}
```

In existing `write(..., const PartInfo &pi)` / `read(..., PartInfo *pi)`:

```cpp
// write:
if (!pi.operating_conditions.empty())
	node->append_child() << key("operating_conditions") << pi.operating_conditions;

// read:
if (node.has_child("operating_conditions"))
	node["operating_conditions"] >> pi->operating_conditions;
```

If ryml needs explicit `std::vector<OperatingCondition>` adapters, follow the same pattern already used for `std::map` pin maps in this file (or write a SEQ loop manually with `append_child() |= SEQ`).

Ensure `#include <vector>` is available via existing headers.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target obv_core_tests && ./build/src/obv_core_tests/obv_core_tests
```

Expected: prints `operating_conditions yaml ok` and exits 0. Existing overlay tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/openboardview/annotations.h src/openboardview/annotations.cpp src/obv_core_tests/test_parse_export.cpp
git commit -m "feat(core): PartInfo operating_conditions YAML round-trip"
```

---

### Task 2: Overlay JSON export / apply for `operating_conditions`

**Files:**
- Modify: `src/obv_core/src/overlay_store.cpp` (`appendPartInfo`, `parsePartInfo`)
- Test: `src/obv_core_tests/test_parse_export.cpp`

**Interfaces:**
- Consumes: `PartInfo::operating_conditions` from Task 1
- Produces: `ExportOverlayJson` includes `"operating_conditions":[{...}]`; `ApplyOverlayJson` restores them
- Bulk `PUT /overlays` clients that omit the field wipe conditions (document in code comment near `parsePartInfo`)

- [ ] **Step 1: Write the failing test**

```cpp
static void test_operating_conditions_json_roundtrip() {
	Annotations ann;
	auto &part = ann.NewPartInfo("U12");
	OperatingCondition oc;
	oc.id = "oc_01";
	oc.name = "g1";
	oc.inputs = {"A"};
	oc.outputs = {"B", "C"};
	oc.enables = {"EN"};
	oc.note = "n";
	part.operating_conditions.push_back(oc);

	std::string js = obv::ExportOverlayJson(ann);
	assert(js.find("\"operating_conditions\"") != std::string::npos);
	assert(js.find("\"oc_01\"") != std::string::npos);
	assert(js.find("\"EN\"") != std::string::npos);

	Annotations applied;
	std::string err;
	assert(obv::ApplyOverlayJson(applied, js, err));
	assert(err.empty());
	assert(applied.partInfos["U12"].operating_conditions.size() == 1);
	assert(applied.partInfos["U12"].operating_conditions[0].outputs.size() == 2);
	assert(applied.partInfos["U12"].operating_conditions[0].outputs[1] == "C");
	std::cout << "operating_conditions json ok\n";
}
```

- [ ] **Step 2: Run test to verify it fails**

Rebuild + run `obv_core_tests`. Expected: export missing key or apply drops array.

- [ ] **Step 3: Implement JSON export/apply**

In `appendPartInfo` after pins block:

```cpp
if (!pi.operating_conditions.empty()) {
	if (!first) os << ',';
	first = false;
	appendEscaped(os, "operating_conditions");
	os << ":[";
	for (size_t i = 0; i < pi.operating_conditions.size(); ++i) {
		if (i) os << ',';
		const auto &oc = pi.operating_conditions[i];
		os << '{';
		bool of = true;
		auto sfield = [&](const char *k, const std::string &v) {
			if (v.empty()) return;
			if (!of) os << ',';
			of = false;
			appendEscaped(os, k);
			os << ':';
			appendEscaped(os, v);
		};
		auto afield = [&](const char *k, const std::vector<std::string> &arr) {
			if (!of) os << ',';
			of = false;
			appendEscaped(os, k);
			os << ":[";
			for (size_t j = 0; j < arr.size(); ++j) {
				if (j) os << ',';
				appendEscaped(os, arr[j]);
			}
			os << ']';
		};
		sfield("id", oc.id);
		sfield("name", oc.name);
		// Always emit arrays (even empty) so clients see stable shape
		afield("inputs", oc.inputs);
		afield("outputs", oc.outputs);
		afield("enables", oc.enables);
		sfield("note", oc.note);
		os << '}';
	}
	os << ']';
}
```

In `JsonCursor::parsePartInfo`, handle key `"operating_conditions"`:

```cpp
} else if (key == "operating_conditions") {
	pi.operating_conditions.clear();
	if (!expect('[')) return false;
	skipWs();
	if (peek(']')) { ++i; }
	else {
		for (;;) {
			OperatingCondition oc;
			if (!expect('{')) return false;
			// parse object fields id/name/note strings and inputs/outputs/enables string arrays
			// reuse parseString / skipValue patterns; reject non-string array elems
			// ... implement loop until '}' ...
			pi.operating_conditions.push_back(std::move(oc));
			skipWs();
			if (peek(']')) { ++i; break; }
			if (!expect(',')) return false;
		}
	}
}
```

Implement a small `parseStringArray(std::vector<std::string> &out)` helper on `JsonCursor` (trim not required at parse; Task 5 validates on write API).

Also update `SavePartNetYaml` reload-compare if it deep-compares PartInfo fields and would false-fail — grep for compare loops in `overlay_store.cpp` and include `operating_conditions` equality if present.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target obv_core_tests && ./build/src/obv_core_tests/obv_core_tests
```

Expected: `operating_conditions json ok`, full suite green.

- [ ] **Step 5: Commit**

```bash
git add src/obv_core/src/overlay_store.cpp src/obv_core_tests/test_parse_export.cpp
git commit -m "feat(core): export/apply operating_conditions in overlay JSON"
```

---

### Task 3: `pin_resolve` core helpers

**Files:**
- Create: `src/obv_core/include/obv_core/pin_resolve.h`
- Create: `src/obv_core/src/pin_resolve.cpp`
- Modify: `src/obv_core/CMakeLists.txt` (add `src/pin_resolve.cpp` to `OBV_CORE_SOURCES`)
- Test: `src/obv_core_tests/test_parse_export.cpp` (or `test_pin_resolve.cpp` + CMake)

**Interfaces:**
- Produces (exact names for later tasks):

```cpp
namespace obv {

std::string PinOverlayKey(const Pin &pin);
// Prefer name, else number, else UniqueId()/export id — must match web pinOverlayKey:
//   pin.name || pin.number || pin.id
// For domain Pin without export id: name || number || UniqueId()

enum class MeasureSource { None, Overlay, Board, Propagated };

struct MeasureField {
	std::string board;
	std::string overlay;
	std::string localValue;
	MeasureSource localSource = MeasureSource::None;
	std::string effectiveValue;
	MeasureSource effectiveSource = MeasureSource::None;
	std::string fromComponent; // set when Propagated
	std::string fromPinKey;
	std::string fromPinId;
};

struct PinResolveResult {
	const Pin *pin = nullptr; // non-owning, points into snapshot board
	std::string pinKey;
	std::string netName;
	MeasureField diode, voltage, ohm, ohm_black;
	// overlay pin meta
	std::string overlayNote;
	std::string overlayShowName;
	PinVoltageFlag overlayVoltageFlag = PinVoltageFlag::unknown;
};

// Find pin under part; matching order per spec §4.2
const Pin *FindPartPin(const Board &board, const std::string &part, const std::string &pinRef);

// Fill measurements for a found pin
void ResolvePinMeasurements(const Board &board, const Annotations &ann,
                            const Pin &pin, PinResolveResult &out);

// High-level: find + resolve; returns false if part/pin missing
bool ResolvePartPin(const Board &board, const Annotations &ann,
                    const std::string &part, const std::string &pinRef,
                    PinResolveResult &out, std::string &errCode);
// errCode: PART_NOT_FOUND | PIN_NOT_FOUND

// JSON string for GET pin response (boardId/sourceName filled by caller or params)
std::string ExportPinResolveJson(const std::string &boardId,
                                 const std::string &sourceName,
                                 const std::string &part,
                                 const PinResolveResult &r);

// Part existence
const Component *FindComponent(const Board &board, const std::string &part);

// Generate condition id: "oc_" + 8 hex from counter/random — deterministic: use
// next free oc_0001 style within the part's existing ids
std::string AllocateConditionId(const PartInfo &part);

// Validate/normalize condition fields; returns false + err message
bool NormalizeOperatingCondition(OperatingCondition &oc, std::string &err);
// Rules from spec: id<=64, name/note<=2048, label<=128, arrays<=256, trim, drop empty labels

} // namespace obv
```

Use domain types from `Board.h` / `BRDBoard` (`board.Pins()`, `pin.component->name`, `pin.diode_value`, etc.).

**Measurement algorithm (must match frontend):**

```text
local.overlay = ann.partInfos[part].pins[PinOverlayKey(pin)].{diode|voltage|...} trimmed
local.board   = pin.diode_value / voltage_value / ohm_value / ohm_black_value trimmed
local         = overlay if non-empty else board
effective     = local if non-empty else first non-empty local among pins with same net pointer (or same net identity) in board.Pins() order
```

Use `pin.net` shared_ptr equality for same-net (more reliable than export netId integers).

`PinOverlayKey` for domain:

```cpp
std::string PinOverlayKey(const Pin &pin) {
	if (!pin.name.empty()) return pin.name;
	if (!pin.number.empty()) return pin.number;
	return pin.UniqueId();
}
```

- [ ] **Step 1: Write failing unit tests (no real board file)**

Because constructing a full `BRDBoard` is heavy, test pure logic via a **test-only fixture approach**:

Option A (preferred if easy): if tests can include a tiny hand-built board — skip.

Option B: extract internal helpers that operate on a simplified struct, and test those:

```cpp
// In pin_resolve.cpp (anonymous namespace) or exposed for tests:
// Prefer testing through ResolvePinMeasurements with a minimal mock.

// Practical approach used in this plan:
// Add free functions used by ResolvePinMeasurements that take string maps, unit-tested:

// Actually: implement Resolve on real Pin* requires Board.
// Use OBV_TEST_BOARD when set for integration; always-run unit tests for:
//   - NormalizeOperatingCondition
//   - AllocateConditionId
//   - PinOverlayKey via a stack Pin with name/number set (Pin is a concrete struct!)
```

`Pin` is a concrete struct — tests can construct `Pin` objects on the stack, but `Board::Pins()` returns shared vectors from BRDBoard. So:

1. Unit-test `NormalizeOperatingCondition`, `AllocateConditionId`, string source priority with a small **exported** helper:

```cpp
// pin_resolve.h — testable pure API
MeasureField ResolveOneField(
	const std::string &overlayVal,
	const std::string &boardVal,
	const std::string &propagatedVal,
	const std::string &propComponent,
	const std::string &propPinKey,
	const std::string &propPinId);
```

2. Integration: if `OBV_TEST_BOARD` set, parse board and resolve a known pin — optional skip.

```cpp
static void test_resolve_one_field_priority() {
	auto m = obv::ResolveOneField("ov", "bd", "prop", "R1", "2", "id2");
	assert(m.overlay == "ov");
	assert(m.board == "bd");
	assert(m.localValue == "ov");
	assert(m.localSource == obv::MeasureSource::Overlay);
	assert(m.effectiveValue == "ov");
	assert(m.effectiveSource == obv::MeasureSource::Overlay);

	m = obv::ResolveOneField("", "bd", "prop", "R1", "2", "id2");
	assert(m.localValue == "bd");
	assert(m.effectiveSource == obv::MeasureSource::Board);

	m = obv::ResolveOneField("", "", "prop", "R1", "2", "id2");
	assert(m.localSource == obv::MeasureSource::None);
	assert(m.effectiveValue == "prop");
	assert(m.effectiveSource == obv::MeasureSource::Propagated);
	assert(m.fromComponent == "R1");
	std::cout << "resolve field priority ok\n";
}

static void test_normalize_condition() {
	OperatingCondition oc;
	oc.id = "  oc_1  ";
	oc.inputs = {" A ", "", "B"};
	oc.outputs = {std::string(200, 'x')}; // too long label
	std::string err;
	// First fix long label case
	oc.outputs = {"Y"};
	assert(obv::NormalizeOperatingCondition(oc, err));
	assert(oc.id == "oc_1");
	assert(oc.inputs.size() == 2);
	assert(oc.inputs[0] == "A");

	OperatingCondition bad;
	bad.id = std::string(65, 'a');
	assert(!obv::NormalizeOperatingCondition(bad, err));
	std::cout << "normalize condition ok\n";
}

static void test_allocate_condition_id() {
	PartInfo p;
	OperatingCondition a; a.id = "oc_0001";
	p.operating_conditions.push_back(a);
	std::string id = obv::AllocateConditionId(p);
	assert(id != "oc_0001");
	assert(id.rfind("oc_", 0) == 0);
	std::cout << "allocate condition id ok\n";
}
```

- [ ] **Step 2: Run tests — expect link/compile fail**

- [ ] **Step 3: Implement `pin_resolve.h` / `.cpp` + CMake**

Add sources to `src/obv_core/CMakeLists.txt` next to `overlay_store.cpp`.

Implement:

- trim helper
- `ResolveOneField`
- `FindComponent` — iterate `board.Components()`, match `name`
- `FindPartPin` — filter pins where `component && component->name == part`, then match order: name, number, UniqueId, PinOverlayKey
- `ResolvePinMeasurements` — for each mode, compute board/overlay strings; build net propagation by scanning `board.Pins()` for same `net.get()` and first non-empty local
- `ExportPinResolveJson` — hand-rolled JSON matching spec §4.4 (`local.source` strings: `overlay|board|none`, effective: `overlay|board|propagated|none`)
- `NormalizeOperatingCondition` / `AllocateConditionId`

Pin export id in JSON: reuse the same scheme as `board_json.cpp` if possible (extract or duplicate small `pinId` helper). If extract is invasive, use `PinOverlayKey` + index only for `from.pinId` when needed — **prefer calling a shared helper**. Grep `pinId(` in `board_json.cpp`; if static, duplicate the minimal rule:

```cpp
// board_json uses something like component+number; read function and match.
```

- [ ] **Step 4: Run tests — expect pass**

```bash
cmake --build build --target obv_core_tests && ./build/src/obv_core_tests/obv_core_tests
```

- [ ] **Step 5: Commit**

```bash
git add src/obv_core/include/obv_core/pin_resolve.h src/obv_core/src/pin_resolve.cpp src/obv_core/CMakeLists.txt src/obv_core_tests/test_parse_export.cpp
git commit -m "feat(core): pin measurement resolve helpers for agent API"
```

---

### Task 4: `BoardRegistry::ResolveRef`

**Files:**
- Modify: `src/obv_server/board_registry.h`
- Modify: `src/obv_server/board_registry.cpp`

**Interfaces:**
- Produces:

```cpp
enum class BoardRefStatus { Ok, NotFound, Ambiguous, InvalidId };

struct BoardRefResult {
	BoardRefStatus status = BoardRefStatus::NotFound;
	std::string boardId;              // set when Ok
	std::vector<std::string> candidates; // Ambiguous: "id\tdisplayPath" lines for message only
};

BoardRefResult ResolveRef(const std::string &ref) const;
// Also useful:
// Entry lookup after resolve — use existing BoardPath / GetParsed with boardId
```

Resolution order (spec §3.2):

1. If `IsValidBoardId(ref)` → lookup by id after `List()`/`scan`; missing → NotFound
2. Else normalize ref separators to preferred form; match `Entry.displayPath` exactly (case-sensitive on Windows paths: use stored displayPath as source of truth)
3. Else match `Entry.name` (basename) uniquely among entries
4. 0 → NotFound; >1 basename → Ambiguous with candidate id + displayPath (no absolute path)

- [ ] **Step 1: Implement ResolveRef** (server has no unit harness — implement carefully + manual verify in Task 7)

```cpp
BoardRegistry::BoardRefResult BoardRegistry::ResolveRef(const std::string &ref) const {
	BoardRefResult r;
	if (ref.empty()) {
		r.status = BoardRefStatus::NotFound;
		return r;
	}
	// Ensure index fresh
	const auto entries = List(); // rescans
	if (IsValidBoardId(ref)) {
		for (const auto &e : entries) {
			if (e.id == ref) {
				r.status = BoardRefStatus::Ok;
				r.boardId = e.id;
				return r;
			}
		}
		r.status = BoardRefStatus::NotFound;
		return r;
	}
	// normalize ref: replace \\ with / for comparison against displayPath which uses preferred separators
	auto norm = [](std::string s) {
		for (char &c : s) if (c == '\\') c = '/';
		return s;
	};
	const std::string want = norm(ref);
	std::vector<const Entry *> pathHits;
	std::vector<const Entry *> nameHits;
	for (const auto &e : entries) {
		if (norm(e.displayPath) == want) pathHits.push_back(&e);
		if (e.name == ref || norm(e.name) == want) nameHits.push_back(&e);
	}
	if (pathHits.size() == 1) {
		r.status = BoardRefStatus::Ok;
		r.boardId = pathHits[0]->id;
		return r;
	}
	if (pathHits.size() > 1) {
		r.status = BoardRefStatus::Ambiguous;
		for (auto *e : pathHits) r.candidates.push_back(e->id + " " + e.displayPath);
		return r;
	}
	if (nameHits.size() == 1) {
		r.status = BoardRefStatus::Ok;
		r.boardId = nameHits[0]->id;
		return r;
	}
	if (nameHits.size() > 1) {
		r.status = BoardRefStatus::Ambiguous;
		for (auto *e : nameHits) r.candidates.push_back(e->id + " " + e.displayPath);
		return r;
	}
	r.status = BoardRefStatus::NotFound;
	return r;
}
```

Declare in header public section.

Helper for routes:

```cpp
// optional: Entry GetEntry(const std::string &id) const;
```

If no GetEntry, routes can `List()` and find id for `sourceName`/`displayPath` — acceptable but O(n). Prefer adding:

```cpp
bool TryGetEntry(const std::string &id, Entry &out) const;
```

- [ ] **Step 2: Build server**

```bash
cmake --build build --target obv_server
```

Expected: success.

- [ ] **Step 3: Commit**

```bash
git add src/obv_server/board_registry.h src/obv_server/board_registry.cpp
git commit -m "feat(server): resolve board ref by id, path, or unique name"
```

---

### Task 5: HTTP routes — pin + part GET

**Files:**
- Modify: `src/obv_server/routes.cpp`
- Modify: `src/obv_server/routes.h` (comment only if needed)

**Interfaces:**
- Consumes: `ResolveRef`, `GetParsed`, `BoardPath`, `LoadOverlayForBoard`, `ResolvePartPin`, `ExportPinResolveJson`, `FindComponent`
- Produces endpoints:
  - `GET /api/v1/boards/:ref/parts/:part/pins/:pin`
  - `GET /api/v1/boards/:ref/parts/:part`

- [ ] **Step 1: Add route helpers in anonymous namespace of `routes.cpp`**

```cpp
// Decode path params (httplib may already decode). Keep part/pin raw.
// applyBoardRef: sets boardId or writes error response; returns false on failure
bool applyBoardRef(BoardRegistry &registry, const std::string &ref,
                   httplib::Response &res, std::string &boardId) {
	auto r = registry.ResolveRef(ref);
	if (r.status == BoardRegistry::BoardRefStatus::Ambiguous) {
		std::string msg = "ambiguous board ref";
		for (const auto &c : r.candidates) { msg += "; "; msg += c; }
		setError(res, 409, "BOARD_REF_AMBIGUOUS", msg);
		return false;
	}
	if (r.status != BoardRegistry::BoardRefStatus::Ok) {
		setError(res, 404, "NOT_FOUND", "board not found");
		return false;
	}
	boardId = r.boardId;
	return true;
}

std::string publicSourceName(BoardRegistry &registry, const std::string &boardId) {
	BoardRegistry::Entry e;
	if (registry.TryGetEntry(boardId, e) && !e.displayPath.empty()) return e.displayPath;
	if (registry.TryGetEntry(boardId, e)) return e.name;
	return boardId;
}
```

- [ ] **Step 2: Register GET pin route** (more specific paths **before** generic board GET if any conflict; place near other `/boards/:id/...` routes)

```cpp
svr.Get(R"(/api/v1/boards/:ref/parts/:part/pins/:pin)",
	[&registry](const httplib::Request &req, httplib::Response &res) {
		const std::string ref = pathParam(req, "ref");
		const std::string part = pathParam(req, "part");
		const std::string pin = pathParam(req, "pin");
		std::string boardId;
		if (!applyBoardRef(registry, ref, res, boardId)) return;
		auto snap = registry.GetParsed(boardId);
		if (!snap) { setError(res, 404, "NOT_FOUND", "board not found"); return; }
		if (!snap->ok()) {
			setError(res, 400, "PARSE_FAILED",
			         snap->error.empty() ? "parse failed" : snap->error);
			return;
		}
		const auto boardPath = registry.BoardPath(boardId);
		std::lock_guard<std::mutex> lock(registry.OverlayMutex(boardId));
		Annotations ann;
		std::string err;
		if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
			setError(res, 500, "OVERLAY_LOAD_FAILED",
			         err.empty() ? "failed to load overlay" : err);
			ann.Close();
			return;
		}
		obv::PinResolveResult pr;
		std::string code;
		if (!obv::ResolvePartPin(*snap->board, ann, part, pin, pr, code)) {
			setError(res, 404, code.c_str(),
			         code == "PART_NOT_FOUND" ? "part not found" : "pin not found");
			ann.Close();
			return;
		}
		const std::string js = obv::ExportPinResolveJson(
			boardId, publicSourceName(registry, boardId), part, pr);
		ann.Close();
		res.set_content(js, "application/json");
	});
```

Note: httplib path params — existing code uses `:id`. If `:ref` works the same, fine. If multi-segment path refs need encoding, clients must `encodeURIComponent` each segment (single segment only for ref — path with `/` must be encoded as `%2F` in one segment). Document that library paths with `/` are one encoded segment.

- [ ] **Step 3: Register GET part route**

Return JSON:

```json
{
  "boardId": "...",
  "sourceName": "...",
  "part": {
    "name": "U12",
    "side": "...",
    "mount": "...",
    "type": "...",
    "mfgcode": "...",
    "center": {"x":0,"y":0},
    "pins": ["...ids or numbers..."]
  },
  "pins": [
    {"id":"...","number":"...","name":"...","type":"...","netId":null,"netName":""}
  ],
  "partInfo": {
    "part_type": "",
    "angle": 0,
    "operating_conditions": [ ... ]
  }
}
```

Implement `ExportPartSummaryJson` either in `pin_resolve.cpp` or inline in routes — prefer `obv_core` helper `ExportPartSummaryJson(...)` for consistency.

`netId` in part summary: optional; can omit numeric export ids and only put `netName` from `pin.net->name` to avoid reimplementing export net id map. Spec allows light pin list with netId/netName — use `pin.net ? pin.net->name : ""` and `netId: null` unless easy to match export map. **Decision for implementer:** include `netName` always; include `netId` only if reusing export helper is cheap — otherwise null is acceptable for agent MVP if `netName` present. Prefer matching board JSON net ids if a small shared helper exists; otherwise `netName` only is OK and document in response.

- [ ] **Step 4: Build**

```bash
cmake --build build --target obv_server
```

- [ ] **Step 5: Commit**

```bash
git add src/obv_server/routes.cpp src/obv_core/include/obv_core/pin_resolve.h src/obv_core/src/pin_resolve.cpp
git commit -m "feat(server): GET part and pin resolve agent endpoints"
```

---

### Task 6: HTTP routes — operating-conditions CRUD

**Files:**
- Modify: `src/obv_server/routes.cpp`
- Reuse: `NormalizeOperatingCondition`, `AllocateConditionId`, overlay load/save

**Endpoints:**

| Method | Path |
|--------|------|
| GET | `/api/v1/boards/:ref/parts/:part/operating-conditions` |
| POST | same |
| PUT | same (full array replace) |
| GET | `.../operating-conditions/:condId` |
| PUT | `.../operating-conditions/:condId` |
| DELETE | `.../operating-conditions/:condId` |

- [ ] **Step 1: Shared mutate helper**

```cpp
// Pseudocode
bool withPartOverlay(
  BoardRegistry &registry, const std::string &boardId, const std::string &part,
  httplib::Response &res,
  bool requirePartOnBoard,
  std::function<bool(Annotations&, PartInfo&, httplib::Response&)> fn)
{
  auto snap = registry.GetParsed(boardId);
  // parse checks...
  if (requirePartOnBoard && !obv::FindComponent(*snap->board, part)) {
    setError(res, 404, "PART_NOT_FOUND", "part not found");
    return false;
  }
  lock overlay
  load ann
  auto &pi = ann.partInfos[part]; // creates if missing — only after part exists
  pi.partName = part;
  if (!fn(ann, pi, res)) { ann.Close(); return false; }
  if (!obv::SavePartNetYaml(boardPath, ann, err)) { ... }
  // reload optional for response consistency
  ann.Close();
  return true;
}
```

- [ ] **Step 2: GET list / GET one**

```cpp
// GET list → { boardId, part, operating_conditions: [...] }
// GET one → single object or 404 CONDITION_NOT_FOUND
```

Serialize conditions with same field names as Task 2 JSON.

- [ ] **Step 3: POST create**

Parse body with existing `MiniJson` or small dedicated parser (fields: optional id, name, inputs[], outputs[], enables[], note).

```cpp
OperatingCondition oc;
// parse...
if (oc.id.empty()) oc.id = obv::AllocateConditionId(pi);
else {
  // conflict if exists
  for (const auto &x : pi.operating_conditions)
    if (x.id == oc.id) { setError 409 CONDITION_ID_CONFLICT; return; }
}
if (!obv::NormalizeOperatingCondition(oc, err)) { setError 400 BAD_REQUEST; return; }
if (pi.operating_conditions.size() >= 256) { setError 400; return; }
pi.operating_conditions.push_back(oc);
// save...
res.status = 201;
// return created object
```

- [ ] **Step 4: PUT one / DELETE one / PUT full replace**

- PUT one: find by id; replace fields; keep path id; normalize; 404 if missing  
- DELETE: erase if found else 404 (no silent success)  
- PUT collection: body `{"operating_conditions":[...]}`; normalize each; replace vector entirely; does **not** clear `pins`/`part_type`

- [ ] **Step 5: Route registration order**

Register more specific `.../operating-conditions/:condId` **before** collection routes if the router is first-match; httplib typically matches exact patterns — mirror annotation route style (`:annId`).

- [ ] **Step 6: Build**

```bash
cmake --build build --target obv_server
```

- [ ] **Step 7: Commit**

```bash
git add src/obv_server/routes.cpp
git commit -m "feat(server): CRUD part operating_conditions for agent API"
```

---

### Task 7: Manual smoke verification

**Files:** none required (optional script not committed unless useful)

**Prerequisites:** server built; `boardRoot` points at a library with at least one real board that has a multi-pin part (e.g. an IC).

- [ ] **Step 1: Start server**

```bash
# Example — adjust host/port/boardRoot to local config
./build/src/obv_server/obv_server --board-root ./data/boards --host 127.0.0.1 --port 8080
```

(Use whatever flag names `server_config` already supports — read `src/obv_server/main.cpp` / `server_config.cpp` and use real CLI.)

- [ ] **Step 2: List boards and pick ref**

```bash
curl -s http://127.0.0.1:8080/api/v1/boards | head
```

Pick `id` and `path`/`name` from response.

- [ ] **Step 3: Pin resolve by boardId and by name**

```bash
# Replace BOARD_ID, PART, PIN
curl -s "http://127.0.0.1:8080/api/v1/boards/BOARD_ID/parts/PART/pins/PIN" | jq .
# Same via filename if unique:
curl -s "http://127.0.0.1:8080/api/v1/boards/MyBoard.bvr/parts/PART/pins/PIN" | jq .
```

Check:

- `measurements.*.local` / `effective` present  
- If overlay empty and another pin on net has diode in board file, `effective.source` is `propagated` with `from`  
- 404 for missing part/pin  
- 409 for intentionally ambiguous basename if you can stage two same filenames in different folders  

- [ ] **Step 4: Conditions CRUD**

```bash
curl -s -X POST "http://127.0.0.1:8080/api/v1/boards/BOARD_ID/parts/PART/operating-conditions" \
  -H "Content-Type: application/json" \
  -d '{"name":"g1","inputs":["A"],"outputs":["B"],"enables":["EN"],"note":"test"}' | jq .

curl -s "http://127.0.0.1:8080/api/v1/boards/BOARD_ID/parts/PART/operating-conditions" | jq .

# PUT one, DELETE one, PUT full replace
# Restart server and GET list again — data must persist in board.yaml sidecar
```

- [ ] **Step 5: Negative cases**

```bash
curl -s -o /dev/null -w "%{http_code}" -X DELETE ".../operating-conditions/nope"   # expect 404
curl -s -o /dev/null -w "%{http_code}" ".../parts/NoSuchPart"                     # expect 404
```

- [ ] **Step 6: Commit nothing if only manual; if you fixed bugs, commit fixes**

```bash
# If smoke found bugs, fix and:
git add -u
git commit -m "fix(server): agent pin/part API smoke fixes"
```

- [ ] **Step 7: Final checklist vs spec**

| Spec requirement | Verified |
|------------------|----------|
| Pin full snapshot + propagation | |
| Multi-group conditions CRUD | |
| boardId or unique path/name | |
| 409 ambiguous | |
| 404 missing condition (no silent delete) | |
| YAML persistence after restart | |
| No absolute paths in JSON | |
| Desktop YAML keys preserved | |

---

## Self-review (plan vs spec)

| Spec section | Task |
|--------------|------|
| §3 Resource model + board ref | Task 4, 5, 6 |
| §4 Pin full snapshot | Task 3, 5 |
| §4.5 Part summary GET | Task 5 |
| §5 Operating conditions model + YAML | Task 1 |
| §5 JSON + bulk overlay interaction | Task 2 |
| §5 CRUD endpoints | Task 6 |
| §6 Errors | Tasks 4–6 (`setError` codes) |
| §7 Non-goals (no MCP protocol) | Honored |
| Desktop coexistence | Task 1 YAML write path shared with desktop |

**Placeholder scan:** none intentional.  
**Type consistency:** `OperatingCondition` fields `id/name/inputs/outputs/enables/note` used in Tasks 1–2–6; `ResolvePartPin` / `ExportPinResolveJson` in 3→5; `ResolveRef` in 4→5/6.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-31-agent-pin-part-api.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session with `executing-plans`, batched with checkpoints  

Which approach?
