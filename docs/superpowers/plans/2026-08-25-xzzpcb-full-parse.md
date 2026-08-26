# XZZPCBFile Full Parse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `XZZPCBFile` fill `BRDFileBase` tracks/vias/arcs/pin geometry/nets/scale so BoardView shows copper when opening `.pcb`, matching XJson semantics.

**Architecture:** Keep parsing in `XZZPCBFile`. Extract `LayerMapper`/`PCB_LAYER_ID` to `XzzLayers.h` and call `castSide`/`castPinSide` statically. Stop dividing coordinates by 10000; set `scale=10000` and `boardSymmetry=true`; delete origin translation. Binary layouts are pinned in `docs/superpowers/specs/2026-08-25-xzzpcb-full-parse-design.md` from `compare/Switch OLED-HEG-CPU-01 PCB layer.{pcb,json}`.

**Tech Stack:** C++20, CMake, existing `read_uint32_t`/`ENSURE_OR_FAIL`/`des_decrypt`, SDL log. No new test framework.

## Global Constraints

- No BVR3 writer, no new CLI, no unit-test harness, no committed `compare/` fixtures.
- Do not change DES/XOR/key checks.
- Do not change `BRDBoard`/`BoardView` rendering. `texts` may be filled; they are not drawn this round.
- Do not set `BRDPin.angle` or `diode_vale` from PCB (not present in the paired binary).
- Per-item geometry parse failure: `SDL_LogWarn` and skip that item. Structural truncation/`net_size`/key errors still `valid=false`.
- Coordinates stay file integers. `scale = 10000`. No `find_xy_translation`.
- Layer 28 geometry goes to `outline_segments` only, never `tracks`/`arcs`.
- Side via `xjsonfile::LayerMapper::castSide` / `castPinSide`. Do not fold max layer to Bottom (BRDBoard does that).
- File naming PascalCase. C++20. `NOMINMAX` already on.

## File map

| File | Role |
|------|------|
| `src/openboardview/FileFormats/XzzLayers.h` | Shared `PCB_LAYER_ID` + `LayerMapper` |
| `src/openboardview/FileFormats/XJsonFile.cpp` | Use header; delete local mapper/`unique_ptr` |
| `src/openboardview/FileFormats/XZZPCBFile.h` | New parse/via/text/net helpers; drop translation |
| `src/openboardview/FileFormats/XZZPCBFile.cpp` | Fill BRD fields per spec |

Oracle (do not commit). From the Switch OLED pair, after implementation the C++ parser must report approximately:

- `nets.size() == 644`
- `parts.size() == 1039`
- `pins.size() == 4424`
- `vias.size() == 3547`
- `tracks.size() == 26335` (26439 top-level 0x05 minus 104 layer-28)
- `arcs.size() == 0` (all 74 arcs are layer 28)
- `outline_segments` non-empty (104 lines + tessellated 74 arcs)
- first pin `size` non-zero

Verify by logging those sizes at the end of the constructor (keep the log; it is useful) and `cmake --build` the existing target.

---

### Task 1: Extract `XzzLayers.h` and stop using static mapper

**Files:**
- Create: `src/openboardview/FileFormats/XzzLayers.h`
- Modify: `src/openboardview/FileFormats/XJsonFile.cpp`

**Interfaces:**
- Produces: `namespace xjsonfile { enum PCB_LAYER_ID {...}; struct LayerMapper { static BRDPartMountingSide castSide(PCB_LAYER_ID); static BRDPinSide castPinSide(PCB_LAYER_ID); BRDPartMountingSide toSide(PCB_LAYER_ID); BRDPinSide toPinSide(PCB_LAYER_ID); ... }; }`
- Consumes: `BRDFileBase.h` enums

- [ ] **Step 1: Add header**

Create `src/openboardview/FileFormats/XzzLayers.h` with the LayerMapper currently in `XJsonFile.cpp` lines 24–77 (enum + struct, including ctor/`max`/`toSide`/`toPinSide`). Wrap in `namespace xjsonfile`. Include `BRDFileBase.h` and `<vector>`. No `static unique_ptr`.

- [ ] **Step 2: Point XJsonFile at the header**

In `XJsonFile.cpp`:
- `#include "XzzLayers.h"` after `XJsonFile.h`
- Delete the local `enum PCB_LAYER_ID`, `struct LayerMapper`, and `static std::unique_ptr<LayerMapper> layerMapper`
- Keep `namespace xjsonfile { struct Position ... }` as-is
- Replace every `layerMapper->toSide(x)` with `LayerMapper::castSide(x)`
- Replace `layerMapper->toPinSide(x)` with `LayerMapper::castPinSide(x)`
- In `XJsonFile::XJsonFile`, delete `get_all_layer` and the three lines that sort layers and assign `xjsonfile::layerMapper`
- Pad/module `xjsonfile::layerMapper->toSide` → `xjsonfile::LayerMapper::castSide`

Conversion operators live inside `namespace xjsonfile`, so `LayerMapper::castSide` resolves.

- [ ] **Step 3: Build**

```
cmake --build --config Release
```

Expected: XJsonFile compiles. No new linker errors related to LayerMapper.

- [ ] **Step 4: Commit**

```
git add src/openboardview/FileFormats/XzzLayers.h src/openboardview/FileFormats/XJsonFile.cpp
git commit -m "extract XzzLayers.h and use static LayerMapper::castSide"
```

---

### Task 2: Scale, raw coords, delete translation, fill `nets`

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.h`
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp`

**Interfaces:**
- Produces: `void apply_net(std::string &net, int &netId, uint32_t net_index);` on `XZZPCBFile`
- Produces: constructor sets `scale = 10000`, `boardSymmetry = true`, no translation
- Consumes: existing `net_dict`

- [ ] **Step 1: Header**

In `XZZPCBFile.h`:
- `#include "XzzLayers.h"`
- Add `void apply_net(std::string &net, int &netId, uint32_t net_index);`
- Delete declarations `find_xy_translation`, `translate_segments`, `translate_pins`

- [ ] **Step 2: `apply_net` + net map**

In `XZZPCBFile.cpp`, add:

```cpp
void XZZPCBFile::apply_net(std::string &net, int &netId, uint32_t net_index) {
	netId = static_cast<int>(net_index);
	auto it = net_dict.find(net_index);
	if (it == net_dict.end() || it->second == "NC") {
		net = "UNCONNECTED";
	} else {
		net = it->second;
	}
}
```

At the end of `parse_net_block`, after `net_dict[net_index] = net_name;`:

```cpp
BRDNet n;
n.id = static_cast<int>(net_index);
n.name = net_name;
nets[static_cast<int>(net_index)] = n;
```

If `net_size < 8`, `ENSURE_OR_FAIL` and return (keep current overflow-safe check; if still `net_size - 8` on uint32, use `size_t` for the length).

- [ ] **Step 3: Constructor scale / no translate**

Delete the unused local `std::list<...> outline_segments;` in the constructor.

Replace the translation block with:

```cpp
scale = static_cast<float>(XZZ_GLOBAL_SCALE);
boardSymmetry = true;
valid = true;
```

Delete the three translation function definitions at the bottom of the cpp.

- [ ] **Step 4: Stop dividing coordinates**

In `parse_pin_block`, `parse_test_pad_block`, `parse_line_segment_block`, `parse_arc_block`: assign `x`/`y`/`r` directly to `BRDPoint`/`int` **without** `/ XZZ_GLOBAL_SCALE`.

`parse_arc_block` still tessellates layer 28 only (until Task 4). Pass degrees as `static_cast<int>(angle_start / XZZ_GLOBAL_SCALE)` into `xzz_arc_to_segments` (file stores deg×10000).

`parse_line_segment_block` still layer-28-only until Task 3.

- [ ] **Step 5: Build**

```
cmake --build --config Release
```

Expected: compiles. Loading a `.pcb` still shows parts/pins, now in json-scale coordinates (board will look like XJson, not origin-shifted).

- [ ] **Step 6: Commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.h src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: raw coords, scale 10000, nets map, drop translation"
```

---

### Task 3: Top-level 0x05 lines → tracks

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp` `parse_line_segment_block`

**Interfaces:**
- Consumes: `apply_net`, `LayerMapper::castSide`, `XZZ_GLOBAL_SCALE` unused for coords
- Produces: `tracks` filled; layer 28 still `outline_segments`

- [ ] **Step 1: Replace `parse_line_segment_block`**

```cpp
void XZZPCBFile::parse_line_segment_block(const std::vector<char> &buf) {
	uint32_t layer = read_uint32_t(buf, 0 * sizeof(uint32_t), error_msg);
	uint32_t x1    = read_uint32_t(buf, 1 * sizeof(uint32_t), error_msg);
	uint32_t y1    = read_uint32_t(buf, 2 * sizeof(uint32_t), error_msg);
	uint32_t x2    = read_uint32_t(buf, 3 * sizeof(uint32_t), error_msg);
	uint32_t y2    = read_uint32_t(buf, 4 * sizeof(uint32_t), error_msg);
	uint32_t width = read_uint32_t(buf, 5 * sizeof(uint32_t), error_msg);
	uint32_t net_index = read_uint32_t(buf, 6 * sizeof(uint32_t), error_msg);
	if (!error_msg.empty()) {
		return;
	}

	BRDPoint p1{static_cast<int>(x1), static_cast<int>(y1)};
	BRDPoint p2{static_cast<int>(x2), static_cast<int>(y2)};

	if (layer == static_cast<uint32_t>(xjsonfile::Board)) {
		outline_segments.push_back({p1, p2});
		return;
	}

	BRDTrack track{};
	track.points = {p1, p2};
	track.width = static_cast<float>(width);
	track.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	apply_net(track.net, track.netId, net_index);
	tracks.push_back(track);
}
```

If `buf.size() < 28`, `read_uint32_t` sets `error_msg` and the constructor currently aborts the whole file. Spec wants skip-one-item. Change this function only: if `buf.size() < 28`, `SDL_LogWarn` and `return` **without** writing `error_msg`.

```cpp
if (buf.size() < 7 * sizeof(uint32_t)) {
	SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short line block (%zu)", buf.size());
	return;
}
```

Do the size check first; then read fields (or unpack) without using `ENSURE_OR_FAIL` for this block.

- [ ] **Step 2: Build**

Expected: compiles. Pair file should yield `tracks.size()==26335` once logged (Task 10). Until then, loading `.pcb` should show copper tracks in BoardView.

- [ ] **Step 3: Commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: parse copper line segments as tracks"
```

---

### Task 4: Top-level 0x01 arcs → `BRDArc` or outline

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp` `parse_arc_block`

**Interfaces:**
- Consumes: `apply_net`, `LayerMapper::castSide`, existing `xzz_arc_to_segments`

- [ ] **Step 1: Replace `parse_arc_block`**

```cpp
void XZZPCBFile::parse_arc_block(const std::vector<char> &buf) {
	if (buf.size() < 8 * sizeof(uint32_t)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short arc block (%zu)", buf.size());
		return;
	}
	uint32_t layer = read_uint32_t(buf, 0 * sizeof(uint32_t), error_msg);
	uint32_t x     = read_uint32_t(buf, 1 * sizeof(uint32_t), error_msg);
	uint32_t y     = read_uint32_t(buf, 2 * sizeof(uint32_t), error_msg);
	uint32_t r     = read_uint32_t(buf, 3 * sizeof(uint32_t), error_msg);
	uint32_t as    = read_uint32_t(buf, 4 * sizeof(uint32_t), error_msg);
	uint32_t ae    = read_uint32_t(buf, 5 * sizeof(uint32_t), error_msg);
	uint32_t width = read_uint32_t(buf, 6 * sizeof(uint32_t), error_msg);
	uint32_t net_index = read_uint32_t(buf, 7 * sizeof(uint32_t), error_msg);
	if (!error_msg.empty()) {
		error_msg.clear();
		return;
	}

	const double deg_start = static_cast<double>(as) / XZZ_GLOBAL_SCALE;
	const double deg_end   = static_cast<double>(ae) / XZZ_GLOBAL_SCALE;
	BRDPoint centre{static_cast<int>(x), static_cast<int>(y)};

	if (layer == static_cast<uint32_t>(xjsonfile::Board)) {
		auto segments = xzz_arc_to_segments(static_cast<int>(deg_start), static_cast<int>(deg_end),
		                                    static_cast<int>(r), centre);
		std::move(segments.begin(), segments.end(), std::back_inserter(outline_segments));
		return;
	}

	BRDArc arc{};
	arc.pos = centre;
	arc.radius = static_cast<float>(r);
	arc.width = static_cast<float>(width);
	double start = deg_start;
	double end = deg_end;
	if (start > end) {
		start -= 360.0;
	}
	constexpr double degToRad = 3.14159265358979323846 / 180.0;
	arc.startAngle = static_cast<float>(start * degToRad);
	arc.endAngle = static_cast<float>(end * degToRad);
	arc.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	apply_net(arc.net, arc.netId, net_index);
	arcs.push_back(arc);
}
```

On this pair, all 74 arcs are layer 28, so `arcs` stays empty and outline grows. That is correct.

- [ ] **Step 2: Build and commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: parse copper arcs; keep layer 28 tessellated"
```

---

### Task 5: Top-level 0x02 vias

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.h` (declare `parse_via_block`)
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp`

**Interfaces:**
- Produces: `void parse_via_block(const std::vector<char> &buf);`

- [ ] **Step 1: Implement**

Header: `void parse_via_block(const std::vector<char> &buf);`

```cpp
void XZZPCBFile::parse_via_block(const std::vector<char> &buf) {
	if (buf.size() < 8 * sizeof(uint32_t)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short via block (%zu)", buf.size());
		return;
	}
	uint32_t x     = read_uint32_t(buf, 0 * sizeof(uint32_t), error_msg);
	uint32_t y     = read_uint32_t(buf, 1 * sizeof(uint32_t), error_msg);
	uint32_t size  = read_uint32_t(buf, 2 * sizeof(uint32_t), error_msg);
	// u32[3] aperture ignored
	uint32_t layer = read_uint32_t(buf, 4 * sizeof(uint32_t), error_msg);
	uint32_t to    = read_uint32_t(buf, 5 * sizeof(uint32_t), error_msg);
	uint32_t net_index = read_uint32_t(buf, 6 * sizeof(uint32_t), error_msg);
	if (!error_msg.empty()) {
		error_msg.clear();
		return;
	}

	BRDVia via{};
	via.pos = {static_cast<int>(x), static_cast<int>(y)};
	via.size = static_cast<float>(size);
	via.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	via.target_side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(to));
	apply_net(via.net, via.netId, net_index);
	vias.push_back(via);
}
```

In `process_block` case `0x02`: call `parse_via_block(block_buf);`

- [ ] **Step 2: Build and commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.h src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: parse via blocks"
```

---

### Task 6: Part header, name from 0x06, inner 0x05 outline

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp` `parse_part_block`
- Modify: `src/openboardview/FileFormats/XZZPCBFile.h` (`parse_text_block` used here too)

**Interfaces:**
- Consumes: `des_decrypt`, `parse_pin_block` (still old geometry until Task 7)
- Produces: `part.mounting_side` from layer; `part.name` from first non-empty 0x06; `part.format` unique endpoints from inner 0x05

- [ ] **Step 1: Add a length-prefixed name reader used by part FPID and 0x06**

Local to the cpp (anonymous namespace):

```cpp
static bool read_named_string(const std::vector<char> &buf, uint32_t &ptr, std::string &out, std::string &error_msg) {
	if (ptr + 6 > buf.size()) {
		return false;
	}
	ptr += 2; // u16 unk
	uint32_t nlen = read_uint32_t(buf, ptr, error_msg);
	ptr += 4;
	if (!error_msg.empty() || ptr + nlen > buf.size()) {
		error_msg.clear();
		return false;
	}
	out.assign(buf.begin() + ptr, buf.begin() + ptr + nlen);
	ptr += nlen;
	return true;
}
```

- [ ] **Step 2: Rewrite `parse_part_block` walk**

Replace the skip-18 / skip-31 name hack:

```cpp
void XZZPCBFile::parse_part_block(std::vector<char> &encrypted_buf) {
	BRDPart part{};
	auto buf = des_decrypt(encrypted_buf);
	uint32_t current_pointer = 0;
	uint32_t part_size = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	if (!error_msg.empty()) {
		return;
	}
	if (buf.size() < part_size + 4 || current_pointer + 18 > buf.size()) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short part header");
		return;
	}
	uint32_t layer = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	uint32_t x     = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	uint32_t y     = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	current_pointer += 4; // angle unused (BRDPart has no angle)
	std::string fpid;
	if (!read_named_string(buf, current_pointer, fpid, error_msg)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: part FPID unreadable");
		return;
	}

	part.mounting_side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	part.part_type = BRDPartType::SMD;
	part.name = fpid;
	(void)x; (void)y;

	const uint32_t part_end = part_size + 4;
	ENSURE_OR_FAIL(buf.size() >= part_end, error_msg, return);

	auto push_unique = [](std::vector<BRDPoint> &pts, BRDPoint p) {
		if (std::find(pts.begin(), pts.end(), p) == pts.end()) {
			pts.push_back(p);
		}
	};

	while (current_pointer < part_end) {
		uint8_t sub = static_cast<uint8_t>(buf[current_pointer]);
		current_pointer += 1;
		if (sub == 0x00) {
			continue;
		}
		uint32_t sub_size = read_uint32_t(buf, current_pointer, error_msg);
		current_pointer += 4;
		if (!error_msg.empty()) {
			error_msg.clear();
			return;
		}
		if (current_pointer + sub_size > buf.size()) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short part sub-block 0x%02X", sub);
			return;
		}
		std::vector<char> sub_buf(buf.begin() + current_pointer, buf.begin() + current_pointer + sub_size);
		uint32_t sub_start = current_pointer;
		current_pointer += sub_size;

		switch (sub) {
			case 0x06: {
				parse_text_block(sub_buf);
				if (!texts.empty() && part.name == fpid && !texts.back().text.empty()) {
					part.name = texts.back().text;
				}
				break;
			}
			case 0x05: {
				if (sub_buf.size() >= 20) {
					uint32_t x1 = read_uint32_t(sub_buf, 4, error_msg);
					uint32_t y1 = read_uint32_t(sub_buf, 8, error_msg);
					uint32_t x2 = read_uint32_t(sub_buf, 12, error_msg);
					uint32_t y2 = read_uint32_t(sub_buf, 16, error_msg);
					if (!error_msg.empty()) {
						error_msg.clear();
						break;
					}
					push_unique(part.format, {static_cast<int>(x1), static_cast<int>(y1)});
					push_unique(part.format, {static_cast<int>(x2), static_cast<int>(y2)});
				}
				break;
			}
			case 0x09: {
				uint32_t pin_ptr = sub_start - 4; // parse_pin_block starts at size field
				auto pin = parse_pin_block(buf, pin_ptr);
				if (!error_msg.empty()) {
					error_msg.clear();
					break;
				}
				pin.part = static_cast<unsigned int>(parts.size() + 1);
				pins.push_back(pin);
				break;
			}
			default:
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: Unknown sub block type: 0x%02X in %s",
				            sub, part.name.c_str());
				break;
		}
	}

	part.end_of_pins = static_cast<unsigned int>(pins.size());
	parts.push_back(part);
}
```

`parse_pin_block` currently takes `current_pointer` at the **size** field (after the type byte). Inner walk already consumed type+size and copied payload. **Do not** use `sub_start - 4` if that is easy to get wrong.

Safer pin call: parse from `sub_buf` by temporarily prefixing is messy. Change `parse_pin_block` in Task 7 to take the payload **including** the leading size word.

For this task, keep calling `parse_pin_block` as today: it expects pointer at size. So **do not** pre-advance over size for 0x09:

```cpp
case 0x09: {
	uint32_t pin_ptr = sub_start - 4; // size field
	auto pin = parse_pin_block(buf, pin_ptr);
	current_pointer = pin_ptr; // parse_pin_block sets pointer to pin_block_end
	...
}
```

Wait: the while-loop already added `sub_size` to `current_pointer`. `parse_pin_block` sets `current_pointer = pin_block_end` which is `size_field + 4 + size` = `sub_start + sub_size`. After the call, overwrite the loop's `current_pointer` with `pin_ptr` after parse (which should equal `sub_start + sub_size`).

Simplest 0x09 path in this task: **don't skip size in the common walker for 0x09**.

Rewrite the walker:

```cpp
while (current_pointer < part_end) {
	uint8_t sub = static_cast<uint8_t>(buf[current_pointer]);
	if (sub == 0x00) { current_pointer += 1; continue; }
	if (sub == 0x09) {
		current_pointer += 1;
		auto pin = parse_pin_block(buf, current_pointer);
		if (!error_msg.empty()) { error_msg.clear(); break; }
		pin.part = static_cast<unsigned int>(parts.size() + 1);
		pins.push_back(pin);
		continue;
	}
	current_pointer += 1;
	uint32_t sub_size = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	...
}
```

- [ ] **Step 3: Stub `parse_text_block` so this compiles**

Header: `void parse_text_block(const std::vector<char> &buf);`

Minimal body (full layout in Task 8 is OK to do here if small):

```cpp
void XZZPCBFile::parse_text_block(const std::vector<char> &buf) {
	if (buf.size() < 24) return;
	uint32_t layer = read_uint32_t(buf, 0, error_msg);
	uint32_t x = read_uint32_t(buf, 4, error_msg);
	uint32_t y = read_uint32_t(buf, 8, error_msg);
	if (!error_msg.empty()) { error_msg.clear(); return; }
	uint32_t ptr = 24;
	std::string text;
	if (!read_named_string(buf, ptr, text, error_msg)) return;
	if (text.empty()) return;
	BRDText t;
	t.pos = {static_cast<int>(x), static_cast<int>(y)};
	t.text = text;
	t.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	texts.push_back(t);
}
```

`process_block` case `0x06`: `parse_text_block(block_buf);`

0x06 payload on the pair is 36 bytes: 6×u32 then `u16+u32+name`. Name starts at offset 24. Good.

- [ ] **Step 4: Build and commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.h src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: parse part layer, silk name, and outline tracks"
```

---

### Task 7: Pin 0x09 geometry

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp` `parse_pin_block`

**Interfaces:**
- Consumes: `apply_net`, `LayerMapper::castPinSide`
- Produces: `BRDPin` with layer side, raw pos, size/shape/top/bottom, `complex_draw`, `radius = min(size)/2`, netId

- [ ] **Step 1: Replace `parse_pin_block`**

```cpp
BRDPin XZZPCBFile::parse_pin_block(const std::vector<char> &buf, uint32_t &current_pointer) {
	BRDPin pin{};
	uint32_t pin_block_size = read_uint32_t(buf, current_pointer, error_msg);
	uint32_t pin_block_end  = current_pointer + pin_block_size + 4;
	current_pointer += 4;
	if (!error_msg.empty() || pin_block_end > buf.size()) {
		error_msg.clear();
		current_pointer = pin_block_end > buf.size() ? static_cast<uint32_t>(buf.size()) : pin_block_end;
		return {};
	}

	uint32_t layer = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	uint32_t x     = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	uint32_t y     = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	current_pointer += 8; // drillSize unused
	uint32_t pin_name_size = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	if (!error_msg.empty() || current_pointer + pin_name_size + 32 + 4 > pin_block_end) {
		error_msg.clear();
		current_pointer = pin_block_end;
		return {};
	}
	std::string pin_name(buf.begin() + current_pointer, buf.begin() + current_pointer + pin_name_size);
	current_pointer += pin_name_size;

	auto ru32 = [&](uint32_t &p) {
		uint32_t v = read_uint32_t(buf, p, error_msg);
		p += 4;
		return v;
	};
	uint32_t gp = current_pointer;
	uint32_t top_w = ru32(gp), top_h = ru32(gp);
	uint8_t top_shape = static_cast<uint8_t>(buf[gp]); gp += 1;
	uint32_t size_w = ru32(gp), size_h = ru32(gp);
	uint8_t shape = static_cast<uint8_t>(buf[gp]); gp += 1;
	uint32_t bot_w = ru32(gp), bot_h = ru32(gp);
	uint8_t bot_shape = static_cast<uint8_t>(buf[gp]); gp += 1;
	current_pointer += 32;
	uint32_t net_index = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer = pin_block_end;
	if (!error_msg.empty()) {
		error_msg.clear();
		return {};
	}

	pin.pos = {static_cast<int>(x), static_cast<int>(y)};
	pin.name = pin_name;
	pin.snum = pin.name;
	pin.side = xjsonfile::LayerMapper::castPinSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	pin.top_size = {static_cast<int>(top_w), static_cast<int>(top_h)};
	pin.top_shape = static_cast<BPDPinShape>(top_shape);
	pin.size = {static_cast<int>(size_w), static_cast<int>(size_h)};
	pin.shape = static_cast<BPDPinShape>(shape);
	pin.bottom_size = {static_cast<int>(bot_w), static_cast<int>(bot_h)};
	pin.bottom_shape = static_cast<BPDPinShape>(bot_shape);
	pin.complex_draw = (top_shape != shape) || (shape != bot_shape);
	pin.radius = static_cast<double>(std::min(pin.size.x, pin.size.y)) / 2.0;
	pin.angle = 0;
	apply_net(pin.net, pin.netId, net_index);
	return pin;
}
```

Do **not** treat leftover 5 geometry bytes or trailing 8 bytes as angle/diode.

- [ ] **Step 2: Test pad raw coords**

`parse_test_pad_block`: keep dummy `"..." + name` logic; stop dividing x/y (if not already in Task 2); `apply_net` instead of net_dict lookup. Leave side Top if no layer field.

- [ ] **Step 3: Build and commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: parse pin size/shape/layer/netId"
```

---

### Task 8: Constructor log + pair verification

**Files:**
- Modify: `src/openboardview/FileFormats/XZZPCBFile.cpp` constructor (log counts)

**Interfaces:**
- Consumes: all previous parse paths

- [ ] **Step 1: Log counts after `valid = true`**

```cpp
SDL_Log("XZZPCBFile: parts=%u pins=%u tracks=%zu vias=%zu arcs=%zu outline=%zu nets=%zu texts=%zu",
        num_parts, num_pins, tracks.size(), vias.size(), arcs.size(),
        outline_segments.size(), nets.size(), texts.size());
```

Place after `num_*` assignments.

- [ ] **Step 2: Build Release**

```
cmake --build --config Release
```

Expected: no errors in `XZZPCBFile.cpp`.

- [ ] **Step 3: Open the pair PCB in OpenBoardView**

File: `compare/Switch OLED-HEG-CPU-01 PCB layer.pcb`

Debug/SDL log must show:
- parts=1039
- pins=4424
- vias=3547
- tracks=26335
- arcs=0
- nets=644
- outline_segments > 0

BoardView: copper tracks visible, vias visible, pad rectangles non-circular, layer list has multiple sides. Pad rotation vs json may differ (allowed).

If counts differ by more than the documented extras (json +1 via without pos, json +1 arc without pos, xzz +1 extra track already in 26335), fix the parser; do not tweak expected numbers silently.

- [ ] **Step 4: Regression**

Still reject invalid key. A truncated buffer still sets `error_msg` without crashing.

- [ ] **Step 5: Commit**

```
git add src/openboardview/FileFormats/XZZPCBFile.cpp
git commit -m "XZZPCBFile: log parse counts after full copper parse"
```

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| XzzLayers extract, no static mapper | 1 |
| scale 10000, no translation, boardSymmetry | 2 |
| nets map + netId | 2 |
| 0x05 tracks / layer 28 outline | 3 |
| 0x01 arcs / layer 28 tessellate | 4 |
| 0x02 vias | 5 |
| part layer/name/inner 0x05 | 6 |
| 0x06 texts | 6 |
| pin geometry | 7 |
| skip per-item, no angle/diode | 7 |
| pair counts + visual | 8 |

## Placeholder scan

No TBD. Layouts are numeric. Verification numbers are from the Switch OLED pair.
