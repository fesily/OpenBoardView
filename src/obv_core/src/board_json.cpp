// Pin id rule (locked Task 3):
//   id = componentName + "." + pin.number   when pin.component is present
//   id = "nail." + pin.number + "." + index when no component (nails / free pins)
// Geometry is raw board space (not screen-transformed). Overlay fields are omitted.

#include "obv_core/board_json.h"

#include "Board.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

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

std::string pinId(const Pin &pin, size_t index) {
	if (pin.component) {
		return pin.component->name + "." + pin.number;
	}
	return "nail." + pin.number + "." + std::to_string(index);
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

void appendSides(std::ostringstream &os, Board &board) {
	os << "\"sides\":[";
	const auto &sides = board.AllSide();
	for (size_t i = 0; i < sides.size(); ++i) {
		if (i) os << ',';
		appendEscaped(os, sideToString(sides[i]));
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

	// nets
	os << ",\"nets\":[";
	{
		const auto &nets = board.Nets();
		for (size_t i = 0; i < nets.size(); ++i) {
			if (i) os << ',';
			const Net &n = *nets[i];
			os << "{\"id\":" << n.number << ",\"name\":";
			appendEscaped(os, n.name);
			os << ",\"isGround\":" << (n.is_ground ? "true" : "false") << '}';
		}
	}
	os << ']';

	// Build pin ids once so component.pins can reference them stably by pin index in board Pins().
	// Prefer component-local listing with same id rule.
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
			os << ",\"center\":";
			appendPoint(os, c.centerpoint.x, c.centerpoint.y);
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
			}
			os << "],\"pins\":[";
			for (size_t pi = 0; pi < c.pins.size(); ++pi) {
				if (pi) os << ',';
				// index not needed when component present
				appendEscaped(os, pinId(*c.pins[pi], pi));
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
				os << p.net->number;
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
				os << t.net->number;
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
				os << v.net->number;
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
				os << a.net->number;
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
