// Pin id rule (locked Task 3):
//   id = componentName + "." + pin.number when pin belongs to a real named component
//   id = "nail." + pin.number + "." + globalPinIndex when:
//     - pin.component is null, OR
//     - component name is empty (BRDBoard strips dummy "..." names to ""), OR
//     - component_type is kComponentTypeDummy (nails / test pads)
// Geometry is raw board space (not screen-transformed). Overlay fields are omitted.
// Net ids are export-local sequential integers (starting at 1) mapped Net* -> id so
// name-only nets with unset Net::number stay unique across pins/tracks/vias/arcs.

#include "obv_core/board_json.h"

#include "Board.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <set>
#include <vector>

namespace obv {
namespace {

void appendEscaped(std::ostringstream &os, const std::string &s) {
	os << '"';
	for (unsigned char c : s) {
		switch (c) {
			case '"': os << "\\\""; break;
			case '\\': os << "\\\\"; break;
			case '\b': os << "\\b"; break;
			case '\f': os << "\\f"; break;
			case '\n': os << "\\n"; break;
			case '\r': os << "\\r"; break;
			case '\t': os << "\\t"; break;
			default:
				if (c < 0x20) {
					os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
					   << std::dec << std::setfill(' ');
				} else {
					os << static_cast<char>(c);
				}
				break;
		}
	}
	os << '"';
}

void appendNumber(std::ostringstream &os, double v) {
	if (!std::isfinite(v)) {
		os << "0";
		return;
	}
	os << v;
}

void appendPoint(std::ostringstream &os, float x, float y) {
	os << "{\"x\":";
	appendNumber(os, x);
	os << ",\"y\":";
	appendNumber(os, y);
	os << '}';
}

std::string sideToString(EBoardSide side) {
	switch (side) {
		case kBoardSideBoth: return "both";
		case kBoardSideBottom: return "bottom";
		case kBoardSideTop: return "top"; // also kBoardSideS1
		case kBoardSideS2: return "s2";
		case kBoardSideS3: return "s3";
		case kBoardSideS4: return "s4";
		case kBoardSideS5: return "s5";
		case kBoardSideS6: return "s6";
		case kBoardSideS7: return "s7";
		case kBoardSideS8: return "s8";
		case kBoardSideS9: return "s9";
		case kBoardSideS10: return "s10";
		case kBoardSideS11: return "s11";
		case kBoardSideS12: return "s12";
		case kBoardSideS13: return "s13";
		case kBoardSideS14: return "s14";
		case kBoardSideS15: return "s15";
		case kBoardSideS16: return "s16";
		default: {
			const int n = static_cast<int>(side);
			if (n >= static_cast<int>(kBoardSideS1) && n < static_cast<int>(kBoardSideMax)) {
				return "s" + std::to_string(n - static_cast<int>(kBoardSideS1) + 1);
			}
			return "both";
		}
	}
}

const char *mountToString(Component::EMountType t) {
	switch (t) {
		case Component::kMountTypeSMD: return "smd";
		case Component::kMountTypeDIP: return "dip";
		default: return "unknown";
	}
}

const char *componentTypeToString(Component::EComponentType t) {
	switch (t) {
		case Component::kComponentTypeDummy: return "dummy";
		case Component::kComponentTypeConnector: return "connector";
		case Component::kComponentTypeIC: return "ic";
		case Component::kComponentTypeResistor: return "resistor";
		case Component::kComponentTypeCapacitor: return "capacitor";
		case Component::kComponentTypeDiode: return "diode";
		case Component::kComponentTypeTransistor: return "transistor";
		case Component::kComponentTypeCrystal: return "crystal";
		case Component::kComponentTypeJellyBean: return "jellybean";
		case Component::kComponentTypeBoard: return "board";
		case Component::kComponentTypeInductor: return "inductor";
		default: return "unknown";
	}
}

const char *shapeToString(EShapeType t) {
	switch (t) {
		case kShapeTypeFold: return "fold";
		case kShapeTypeRect: return "rect";
		case kShapeTypeCircle:
		default: return "circle";
	}
}

// Nail/test-pad pins must not use ".<number>" when dummy component name is empty.
std::string pinId(const Pin &pin, size_t globalIndex) {
	const bool nailLike = !pin.component || pin.component->name.empty() ||
	                      pin.component->component_type == Component::kComponentTypeDummy;
	if (!nailLike) {
		return pin.component->name + "." + pin.number;
	}
	return "nail." + pin.number + "." + std::to_string(globalIndex);
}

// True when component already has a usable outline from parse/special data.
bool hasUsableOutline(const Component &c) {
	if (c.is_special_outline) return true;
	if (c.outline_done) return true;
	if (!c.hull.empty()) return true;
	return false;
}

// Export-time pin-bbox geometry when parse path never ran DrawParts hull/center.
// center = pin bbox midpoint; outline = axis-aligned rect around pins with margin.
struct CompGeom {
	float cx = 0.f, cy = 0.f;
	bool hasCenter = false;
	// 4-corner rect when derived from pins; empty when caller should use native outline
	bool usePinRect = false;
	float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
};

CompGeom deriveCompGeom(const Component &c) {
	CompGeom g;
	if (c.pins.empty()) {
		// Fall back to stored center even if 0,0 (no better source).
		g.cx = c.centerpoint.x;
		g.cy = c.centerpoint.y;
		g.hasCenter = true;
		return g;
	}

	float minX = std::numeric_limits<float>::max();
	float minY = std::numeric_limits<float>::max();
	float maxX = std::numeric_limits<float>::lowest();
	float maxY = std::numeric_limits<float>::lowest();
	float margin = 0.f;
	for (const auto &pp : c.pins) {
		if (!pp) continue;
		const float x = pp->position.x;
		const float y = pp->position.y;
		minX = std::min(minX, x);
		minY = std::min(minY, y);
		maxX = std::max(maxX, x);
		maxY = std::max(maxY, y);
		const float r = pp->diameter * 0.5f;
		if (r > margin) margin = r;
		// Also consider rect pin half-size
		const float hx = std::abs(pp->size.x) * 0.5f;
		const float hy = std::abs(pp->size.y) * 0.5f;
		if (hx > margin) margin = hx;
		if (hy > margin) margin = hy;
	}
	if (!(minX <= maxX && minY <= maxY)) {
		g.cx = c.centerpoint.x;
		g.cy = c.centerpoint.y;
		g.hasCenter = true;
		return g;
	}
	if (margin <= 0.f) {
		// Tiny pad so single-pin parts still get a visible box.
		const float span = std::max(maxX - minX, maxY - minY);
		margin = span > 0.f ? span * 0.1f : 1.f;
	}

	const bool centerUnset = (c.centerpoint.x == 0.f && c.centerpoint.y == 0.f);
	if (!hasUsableOutline(c) || centerUnset) {
		g.cx = (minX + maxX) * 0.5f;
		g.cy = (minY + maxY) * 0.5f;
		g.hasCenter = true;
	} else {
		g.cx = c.centerpoint.x;
		g.cy = c.centerpoint.y;
		g.hasCenter = true;
	}

	if (!hasUsableOutline(c)) {
		g.usePinRect = true;
		g.minX = minX - margin;
		g.minY = minY - margin;
		g.maxX = maxX + margin;
		g.maxY = maxY + margin;
	}
	return g;
}

// Append outline points: prefer special/outline_done/hull; else pin-rect from geom.
void appendComponentOutline(std::ostringstream &os, const Component &c, const CompGeom &g) {
	os << ",\"outline\":[";
	if (c.is_special_outline) {
		for (size_t oi = 0; oi < c.special_outline.size(); ++oi) {
			if (oi) os << ',';
			appendPoint(os, c.special_outline[oi].x, c.special_outline[oi].y);
		}
	} else if (c.outline_done) {
		for (size_t oi = 0; oi < c.outline.size(); ++oi) {
			if (oi) os << ',';
			appendPoint(os, c.outline[oi].x, c.outline[oi].y);
		}
	} else if (!c.hull.empty()) {
		for (size_t oi = 0; oi < c.hull.size(); ++oi) {
			if (oi) os << ',';
			appendPoint(os, c.hull[oi].x, c.hull[oi].y);
		}
	} else if (g.usePinRect) {
		// Axis-aligned rect (TL, TR, BR, BL) from pin bbox + margin.
		appendPoint(os, g.minX, g.minY);
		os << ',';
		appendPoint(os, g.maxX, g.minY);
		os << ',';
		appendPoint(os, g.maxX, g.maxY);
		os << ',';
		appendPoint(os, g.minX, g.maxY);
	}
	os << ']';
}

// Look up export net id; assign a new sequential id if this Net* was not in Nets().
int exportNetId(std::unordered_map<const Net *, int> &netIds, int &nextNetId, const Net *n) {
	if (!n) return 0;
	auto it = netIds.find(n);
	if (it != netIds.end()) return it->second;
	const int id = nextNetId++;
	netIds.emplace(n, id);
	return id;
}

void appendBounds(std::ostringstream &os, const BoardBounds &b) {
	os << "\"bounds\":{\"minX\":";
	appendNumber(os, b.minX);
	os << ",\"minY\":";
	appendNumber(os, b.minY);
	os << ",\"maxX\":";
	appendNumber(os, b.maxX);
	os << ",\"maxY\":";
	appendNumber(os, b.maxY);
	os << '}';
}

// Public sides list for JSON: union of every side string the document
// serializes on elements (component/pin/track/via board_side, via
// target_side, arc board_side). Not AllSide() — that list can omit
// arc-only layers and includes BRDBoard's synthetic max flip layer.
void appendSides(std::ostringstream &os, Board &board) {
	std::set<std::string> sideSet;
	auto add = [&](EBoardSide s) { sideSet.insert(sideToString(s)); };
	for (const auto &c : board.Components())
		if (c) add(c->board_side);
	for (const auto &p : board.Pins())
		if (p) add(p->board_side);
	for (const auto &t : board.Tracks())
		if (t) add(t->board_side);
	for (const auto &v : board.Vias()) {
		if (!v) continue;
		add(v->board_side);
		add(v->target_side);
	}
	for (const auto &a : board.arcs())
		if (a) add(a->board_side);

	std::vector<std::string> sides(sideSet.begin(), sideSet.end());
	// Deterministic order: both, bottom, top, then s2..sN by number.
	std::sort(sides.begin(), sides.end(), [](const std::string &a, const std::string &b) {
		auto rank = [](const std::string &s) -> int {
			if (s == "both") return 0;
			if (s == "bottom") return 1;
			if (s == "top") return 2;
			if (s.size() >= 2 && s[0] == 's') {
				int n = 0;
				for (size_t i = 1; i < s.size(); ++i) {
					if (s[i] < '0' || s[i] > '9') return 1000;
					n = n * 10 + (s[i] - '0');
				}
				return 100 + n;
			}
			return 2000;
		};
		const int ra = rank(a);
		const int rb = rank(b);
		if (ra != rb) return ra < rb;
		return a < b;
	});

	os << "\"sides\":[";
	for (size_t i = 0; i < sides.size(); ++i) {
		if (i) os << ',';
		appendEscaped(os, sides[i]);
	}
	os << ']';
}

void appendMetaCore(std::ostringstream &os, const BoardSnapshot &snap, const std::string &boardId) {
	os << "\"boardSchemaVersion\":1,\"boardId\":";
	appendEscaped(os, boardId);
	os << ",\"sourceName\":";
	appendEscaped(os, snap.sourceName);
	os << ',';
	appendBounds(os, snap.bounds);
	os << ',';
	appendSides(os, *snap.board);
}

} // namespace

std::string ExportMetaJson(const BoardSnapshot &snap, const std::string &boardId) {
	if (!snap.ok()) {
		return {};
	}
	std::ostringstream os;
	os << '{';
	appendMetaCore(os, snap, boardId);
	os << '}';
	return os.str();
}

std::string ExportBoardJson(const BoardSnapshot &snap, const std::string &boardId) {
	if (!snap.ok()) {
		return {};
	}

	Board &board = *snap.board;
	std::ostringstream os;
	os << std::setprecision(9);
	os << '{';
	appendMetaCore(os, snap, boardId);

	// outline
	os << ",\"outline\":{\"points\":[";
	{
		const auto &pts = board.OutlinePoints();
		for (size_t i = 0; i < pts.size(); ++i) {
			if (i) os << ',';
			appendPoint(os, pts[i]->x, pts[i]->y);
		}
	}
	os << "],\"segments\":[";
	{
		const auto &segs = board.OutlineSegments();
		for (size_t i = 0; i < segs.size(); ++i) {
			if (i) os << ',';
			os << "{\"x1\":";
			appendNumber(os, segs[i].first.x);
			os << ",\"y1\":";
			appendNumber(os, segs[i].first.y);
			os << ",\"x2\":";
			appendNumber(os, segs[i].second.x);
			os << ",\"y2\":";
			appendNumber(os, segs[i].second.y);
			os << '}';
		}
	}
	os << "]}";

	// nets — assign unique stable sequential ids in export order (Net::number often unset).
	std::unordered_map<const Net *, int> netIds;
	int nextNetId = 1;
	os << ",\"nets\":[";
	{
		const auto &nets = board.Nets();
		for (size_t i = 0; i < nets.size(); ++i) {
			if (i) os << ',';
			const Net &n = *nets[i];
			const int id = exportNetId(netIds, nextNetId, &n);
			os << "{\"id\":" << id << ",\"name\":";
			appendEscaped(os, n.name);
			os << ",\"isGround\":" << (n.is_ground ? "true" : "false") << '}';
		}
	}
	os << ']';

	// Global pin index map for stable nail.* ids shared by component.pins and pins[].
	std::unordered_map<const Pin *, size_t> pinGlobalIndex;
	{
		const auto &allPins = board.Pins();
		pinGlobalIndex.reserve(allPins.size());
		for (size_t i = 0; i < allPins.size(); ++i) {
			pinGlobalIndex.emplace(allPins[i].get(), i);
		}
	}

	// components
	os << ",\"components\":[";
	{
		const auto &comps = board.Components();
		for (size_t ci = 0; ci < comps.size(); ++ci) {
			if (ci) os << ',';
			const Component &c = *comps[ci];
			os << "{\"name\":";
			appendEscaped(os, c.name);
			os << ",\"side\":";
			appendEscaped(os, sideToString(c.board_side));
			os << ",\"mount\":";
			appendEscaped(os, mountToString(c.mount_type));
			os << ",\"type\":";
			appendEscaped(os, componentTypeToString(c.component_type));
			os << ",\"mfgcode\":";
			appendEscaped(os, c.mfgcode);
			const CompGeom geom = deriveCompGeom(c);
			os << ",\"center\":";
			appendPoint(os, geom.cx, geom.cy);
			appendComponentOutline(os, c, geom);
			os << ",\"pins\":[";
			for (size_t pi = 0; pi < c.pins.size(); ++pi) {
				if (pi) os << ',';
				size_t globalIdx = pi;
				auto it = pinGlobalIndex.find(c.pins[pi].get());
				if (it != pinGlobalIndex.end()) globalIdx = it->second;
				appendEscaped(os, pinId(*c.pins[pi], globalIdx));
			}
			os << "]}";
		}
	}
	os << ']';

	// pins (global list)
	os << ",\"pins\":[";
	{
		const auto &pins = board.Pins();
		for (size_t i = 0; i < pins.size(); ++i) {
			if (i) os << ',';
			const Pin &p = *pins[i];
			os << "{\"id\":";
			appendEscaped(os, pinId(p, i));
			os << ",\"component\":";
			if (p.component) {
				appendEscaped(os, p.component->name);
			} else {
				os << "null";
			}
			os << ",\"number\":";
			appendEscaped(os, p.number);
			os << ",\"name\":";
			appendEscaped(os, p.name);
			os << ",\"netId\":";
			if (p.net) {
				os << exportNetId(netIds, nextNetId, p.net);
			} else {
				os << "null";
			}
			os << ",\"side\":";
			appendEscaped(os, sideToString(p.board_side));
			os << ",\"pos\":";
			appendPoint(os, p.position.x, p.position.y);
			os << ",\"shape\":";
			appendEscaped(os, shapeToString(p.shape));
			os << ",\"diameter\":";
			appendNumber(os, p.diameter);
			os << ",\"size\":";
			appendPoint(os, p.size.x, p.size.y);
			os << ",\"angle\":" << p.angle << '}';
		}
	}
	os << ']';

	// tracks
	os << ",\"tracks\":[";
	{
		const auto &tracks = board.Tracks();
		for (size_t i = 0; i < tracks.size(); ++i) {
			if (i) os << ',';
			const Track &t = *tracks[i];
			os << "{\"side\":";
			appendEscaped(os, sideToString(t.board_side));
			os << ",\"start\":";
			appendPoint(os, t.position_start.x, t.position_start.y);
			os << ",\"end\":";
			appendPoint(os, t.position_end.x, t.position_end.y);
			os << ",\"width\":";
			appendNumber(os, t.width);
			os << ",\"netId\":";
			if (t.net) {
				os << exportNetId(netIds, nextNetId, t.net);
			} else {
				os << "null";
			}
			os << '}';
		}
	}
	os << ']';

	// vias
	os << ",\"vias\":[";
	{
		const auto &vias = board.Vias();
		for (size_t i = 0; i < vias.size(); ++i) {
			if (i) os << ',';
			const Via &v = *vias[i];
			os << "{\"side\":";
			appendEscaped(os, sideToString(v.board_side));
			os << ",\"targetSide\":";
			appendEscaped(os, sideToString(v.target_side));
			os << ",\"pos\":";
			appendPoint(os, v.position.x, v.position.y);
			os << ",\"size\":";
			appendNumber(os, v.size);
			os << ",\"netId\":";
			if (v.net) {
				os << exportNetId(netIds, nextNetId, v.net);
			} else {
				os << "null";
			}
			os << '}';
		}
	}
	os << ']';

	// arcs
	os << ",\"arcs\":[";
	{
		const auto &arcs = board.arcs();
		for (size_t i = 0; i < arcs.size(); ++i) {
			if (i) os << ',';
			const PcbArc &a = *arcs[i];
			os << "{\"side\":";
			appendEscaped(os, sideToString(a.board_side));
			os << ",\"pos\":";
			appendPoint(os, a.position.x, a.position.y);
			os << ",\"radius\":";
			appendNumber(os, a.radius);
			os << ",\"width\":";
			appendNumber(os, a.width);
			os << ",\"startAngle\":";
			appendNumber(os, a.startAngle);
			os << ",\"endAngle\":";
			appendNumber(os, a.endAngle);
			os << ",\"netId\":";
			if (a.net) {
				os << exportNetId(netIds, nextNetId, a.net);
			} else {
				os << "null";
			}
			os << '}';
		}
	}
	os << ']';

	os << '}';
	return os.str();
}

} // namespace obv
