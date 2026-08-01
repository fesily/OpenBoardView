// Pin measurement resolve helpers for agent API (mirror web pinValues.ts).
// Priority: overlay > board file field > same-net first local (board.Pins order).

#include "obv_core/pin_resolve.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>
#include <unordered_map>
#include <limits>



namespace obv {
namespace {

std::string trimCopy(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
	return s.substr(b, e - b);
}

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
					os << "\\u" << std::hex << std::setw(4) << std::setfill('0')
					   << static_cast<int>(c) << std::dec << std::setfill(' ');
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

// True when component already has a usable outline from parse/special data.
// Mirrors board_json.cpp for matching part.outline export shape.
bool hasUsableOutline(const Component &c) {
	if (c.is_special_outline) return true;
	if (c.outline_done) return true;
	if (!c.hull.empty()) return true;
	return false;
}

// Export-time pin-bbox geometry when parse path never ran DrawParts hull/center.
// Matches board_json::deriveCompGeom so part summary outline matches board JSON.
struct CompGeom {
	float cx = 0.f, cy = 0.f;
	bool hasCenter = false;
	bool usePinRect = false;
	float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
};

CompGeom deriveCompGeom(const Component &c) {
	CompGeom g;
	if (c.pins.empty()) {
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

const char *localSourceString(MeasureSource s) {
	switch (s) {
		case MeasureSource::Overlay: return "overlay";
		case MeasureSource::Board: return "board";
		case MeasureSource::None:
		case MeasureSource::Propagated:
		default: return "none";
	}
}

const char *effectiveSourceString(MeasureSource s) {
	switch (s) {
		case MeasureSource::Overlay: return "overlay";
		case MeasureSource::Board: return "board";
		case MeasureSource::Propagated: return "propagated";
		case MeasureSource::None:
		default: return "none";
	}
}

const char *voltageFlagToString(PinVoltageFlag f) {
	switch (f) {
		case PinVoltageFlag::input: return "input";
		case PinVoltageFlag::output: return "output";
		case PinVoltageFlag::unknown:
		default: return "unknown";
	}
}

const char *pinTypeToString(Pin::EPinType t) {
	switch (t) {
		case Pin::kPinTypeNotConnected: return "not_connected";
		case Pin::kPinTypeComponent: return "component";
		case Pin::kPinTypeVia: return "via";
		case Pin::kPinTypeTestPad: return "test_pad";
		case Pin::kPinTypeUnkown:
		default: return "unknown";
	}
}

const char *sideToString(EBoardSide s) {
	switch (s) {
		case kBoardSideBoth: return "both";
		case kBoardSideBottom: return "bottom";
		case kBoardSideTop: return "top";
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
		default: return "both";
	}
}

const char *shapeToString(EShapeType t) {
	switch (t) {
		case kShapeTypeRect: return "rect";
		case kShapeTypeFold: return "fold";
		case kShapeTypeCircle:
		default: return "circle";
	}
}

// Board methods are non-const; accept const Board& and const_cast for read-only use.
Board &mutableBoard(const Board &board) {
	return const_cast<Board &>(board);
}

size_t pinGlobalIndex(const Board &board, const Pin *pin) {
	const auto &pins = mutableBoard(board).Pins();
	for (size_t i = 0; i < pins.size(); ++i) {
		if (pins[i].get() == pin) return i;
	}
	return 0;
}

const PinInfo *lookupOverlayPin(const Annotations &ann, const Pin &pin) {
	if (!pin.component) return nullptr;
	const auto pit = ann.partInfos.find(pin.component->name);
	if (pit == ann.partInfos.end()) return nullptr;
	const std::string key = PinOverlayKey(pin);
	if (key.empty()) return nullptr;
	const auto kit = pit->second.pins.find(key);
	if (kit == pit->second.pins.end()) return nullptr;
	return &kit->second;
}

std::string boardFieldValue(const Pin &pin, int mode) {
	// mode: 0 diode, 1 voltage, 2 ohm, 3 ohm_black
	switch (mode) {
		case 0: return trimCopy(pin.diode_value);
		case 1: return trimCopy(pin.voltage_value);
		case 2: return trimCopy(pin.ohm_value);
		case 3: return trimCopy(pin.ohm_black_value);
		default: return {};
	}
}

std::string overlayFieldValue(const PinInfo *info, int mode) {
	if (!info) return {};
	switch (mode) {
		case 0: return trimCopy(info->diode);
		case 1: return trimCopy(info->voltage);
		case 2: return trimCopy(info->ohm);
		case 3: return trimCopy(info->ohm_black);
		default: return {};
	}
}

std::string localFieldValue(const Annotations &ann, const Pin &pin, int mode) {
	const PinInfo *info = lookupOverlayPin(ann, pin);
	const std::string ov = overlayFieldValue(info, mode);
	if (!ov.empty()) return ov;
	return boardFieldValue(pin, mode);
}

// First non-empty local among pins sharing the same Net* (board.Pins order).
// Returns seed pin or nullptr.
const Pin *findNetSourcePin(const Board &board, const Annotations &ann, const Net *net, int mode) {
	if (!net) return nullptr;
	const auto &pins = mutableBoard(board).Pins();
	for (const auto &sp : pins) {
		if (!sp || sp->net != net) continue;
		if (!localFieldValue(ann, *sp, mode).empty()) return sp.get();
	}
	return nullptr;
}
// Match board_json.cpp: sequential export ids in Nets() order, then first
// encounter on pins/tracks/vias/arcs for nets missing from Nets().
int computeExportNetId(const Board &board, const Net *target) {
	if (!target) return 0;
	std::unordered_map<const Net *, int> netIds;
	int nextNetId = 1;
	auto assign = [&](const Net *n) {
		if (!n) return;
		if (netIds.find(n) != netIds.end()) return;
		netIds.emplace(n, nextNetId++);
	};

	Board &b = mutableBoard(board);
	for (const auto &n : b.Nets()) {
		if (n) assign(n.get());
	}
	for (const auto &p : b.Pins()) {
		if (p && p->net) assign(p->net);
	}
	for (const auto &t : b.Tracks()) {
		if (t && t->net) assign(t->net);
	}
	for (const auto &v : b.Vias()) {
		if (v && v->net) assign(v->net);
	}
	for (const auto &a : b.arcs()) {
		if (a && a->net) assign(a->net);
	}

	const auto it = netIds.find(target);
	return it != netIds.end() ? it->second : 0;
}


void normalizeLabelArray(std::vector<std::string> &arr, std::string &err, bool &ok) {
	if (!ok) return;
	if (arr.size() > 256) {
		err = "array exceeds 256 entries";
		ok = false;
		return;
	}
	std::vector<std::string> out;
	out.reserve(arr.size());
	for (const auto &raw : arr) {
		std::string t = trimCopy(raw);
		if (t.empty()) continue;
		if (t.size() > 128) {
			err = "label exceeds 128 characters";
			ok = false;
			return;
		}
		out.push_back(std::move(t));
	}
	arr = std::move(out);
}

void appendMeasureField(std::ostringstream &os, const MeasureField &m) {
	os << "{\"local\":{\"value\":";
	appendEscaped(os, m.localValue);
	os << ",\"source\":";
	appendEscaped(os, localSourceString(m.localSource));
	os << "},\"effective\":{\"value\":";
	appendEscaped(os, m.effectiveValue);
	os << ",\"source\":";
	appendEscaped(os, effectiveSourceString(m.effectiveSource));
	if (m.effectiveSource == MeasureSource::Propagated) {
		os << ",\"from\":{\"component\":";
		appendEscaped(os, m.fromComponent);
		os << ",\"pinKey\":";
		appendEscaped(os, m.fromPinKey);
		os << ",\"pinId\":";
		appendEscaped(os, m.fromPinId);
		os << '}';
	}
	os << "},\"board\":";
	appendEscaped(os, m.board);
	os << ",\"overlay\":";
	appendEscaped(os, m.overlay);
	os << '}';
}

} // namespace

std::string PinOverlayKey(const Pin &pin) {
	if (!pin.name.empty()) return pin.name;
	if (!pin.number.empty()) return pin.number;
	return pin.UniqueId();
}

std::string ExportPinId(const Pin &pin, size_t globalIndex) {
	// Match board_json.cpp pinId(): component.number or nail.number.index
	const bool nailLike = !pin.component || pin.component->name.empty() ||
	                      pin.component->component_type == Component::kComponentTypeDummy;
	if (!nailLike) {
		return pin.component->name + "." + pin.number;
	}
	return "nail." + pin.number + "." + std::to_string(globalIndex);
}

MeasureField ResolveOneField(const std::string &overlayVal,
                             const std::string &boardVal,
                             const std::string &propagatedVal,
                             const std::string &propComponent,
                             const std::string &propPinKey,
                             const std::string &propPinId) {
	MeasureField m;
	m.overlay = trimCopy(overlayVal);
	m.board = trimCopy(boardVal);
	const std::string prop = trimCopy(propagatedVal);

	if (!m.overlay.empty()) {
		m.localValue = m.overlay;
		m.localSource = MeasureSource::Overlay;
	} else if (!m.board.empty()) {
		m.localValue = m.board;
		m.localSource = MeasureSource::Board;
	} else {
		m.localSource = MeasureSource::None;
	}

	if (!m.localValue.empty()) {
		m.effectiveValue = m.localValue;
		m.effectiveSource = m.localSource;
	} else if (!prop.empty()) {
		m.effectiveValue = prop;
		m.effectiveSource = MeasureSource::Propagated;
		m.fromComponent = propComponent;
		m.fromPinKey = propPinKey;
		m.fromPinId = propPinId;
	} else {
		m.effectiveSource = MeasureSource::None;
	}
	return m;
}

const Component *FindComponent(const Board &board, const std::string &part) {
	for (const auto &c : mutableBoard(board).Components()) {
		if (c && c->name == part) return c.get();
	}
	return nullptr;
}

const Pin *FindPartPin(const Board &board, const std::string &part, const std::string &pinRef) {
	// Matching order among pins with component->name == part (spec section 4.2):
	// 1) name  2) number  3) UniqueId / export id  4) PinOverlayKey
	// First match in board.Pins() order within the first successful tier.
	const auto &pins = mutableBoard(board).Pins();

	auto belongs = [&](const Pin &p) {
		return p.component && p.component->name == part;
	};

	for (const auto &sp : pins) {
		if (!sp || !belongs(*sp)) continue;
		if (sp->name == pinRef) return sp.get();
	}
	for (const auto &sp : pins) {
		if (!sp || !belongs(*sp)) continue;
		if (sp->number == pinRef) return sp.get();
	}
	for (size_t i = 0; i < pins.size(); ++i) {
		const auto &sp = pins[i];
		if (!sp || !belongs(*sp)) continue;
		if (sp->UniqueId() == pinRef) return sp.get();
		if (ExportPinId(*sp, i) == pinRef) return sp.get();
	}
	for (const auto &sp : pins) {
		if (!sp || !belongs(*sp)) continue;
		if (PinOverlayKey(*sp) == pinRef) return sp.get();
	}
	return nullptr;
}

void ResolvePinMeasurements(const Board &board, const Annotations &ann,
                            const Pin &pin, PinResolveResult &out) {
	out.pin = &pin;
	out.pinKey = PinOverlayKey(pin);
	out.netName = pin.net ? pin.net->name : std::string{};
	out.netId = pin.net ? computeExportNetId(board, pin.net) : 0;


	const PinInfo *info = lookupOverlayPin(ann, pin);
	if (info) {
		out.overlayNote = info->note;
		out.overlayShowName = info->show_name;
		out.overlayVoltageFlag = info->voltage_flag;
	} else {
		out.overlayNote.clear();
		out.overlayShowName.clear();
		out.overlayVoltageFlag = PinVoltageFlag::unknown;
	}

	auto resolveMode = [&](int mode) -> MeasureField {
		const std::string ov = overlayFieldValue(info, mode);
		const std::string bd = boardFieldValue(pin, mode);
		std::string prop;
		std::string propComp, propKey, propId;
		// Local wins; only look up seed when local empty.
		const std::string local = !ov.empty() ? ov : bd;
		if (local.empty() && pin.net) {
			const Pin *seed = findNetSourcePin(board, ann, pin.net, mode);
			if (seed && seed != &pin) {
				prop = localFieldValue(ann, *seed, mode);
				if (seed->component) propComp = seed->component->name;
				propKey = PinOverlayKey(*seed);
				propId = ExportPinId(*seed, pinGlobalIndex(board, seed));
			} else if (seed == &pin) {
				// Seed is self but local empty - shouldn't happen; leave none.
			}
		}
		return ResolveOneField(ov, bd, prop, propComp, propKey, propId);
	};

	out.diode = resolveMode(0);
	out.voltage = resolveMode(1);
	out.ohm = resolveMode(2);
	out.ohm_black = resolveMode(3);
}

bool ResolvePartPin(const Board &board, const Annotations &ann,
                    const std::string &part, const std::string &pinRef,
                    PinResolveResult &out, std::string &errCode) {
	if (!FindComponent(board, part)) {
		errCode = "PART_NOT_FOUND";
		return false;
	}
	const Pin *pin = FindPartPin(board, part, pinRef);
	if (!pin) {
		errCode = "PIN_NOT_FOUND";
		return false;
	}
	errCode.clear();
	ResolvePinMeasurements(board, ann, *pin, out);
	return true;
}

std::string ExportPinResolveJson(const std::string &boardId,
                                 const std::string &sourceName,
                                 const std::string &part,
                                 const PinResolveResult &r) {
	if (!r.pin) return {};
	const Pin &p = *r.pin;
	// Approximate global index for export id (self pin).
	// Callers that need exact id may pass already-resolved pin from a board; we recompute.
	// Without board in this API, use 0 for nail-like index (component pins ignore index).
	const size_t selfIdx = 0;
	const std::string selfId = ExportPinId(p, selfIdx);

	std::ostringstream os;
	os << "{\"boardId\":";
	appendEscaped(os, boardId);
	os << ",\"sourceName\":";
	appendEscaped(os, sourceName);
	os << ",\"part\":";
	appendEscaped(os, part);
	os << ",\"pinKey\":";
	appendEscaped(os, r.pinKey);
	os << ",\"pin\":{";
	os << "\"id\":";
	appendEscaped(os, selfId);
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
	os << ",\"show_name\":";
	appendEscaped(os, p.show_name.empty() ? p.name : p.show_name);
	os << ",\"type\":";
	appendEscaped(os, pinTypeToString(p.type));
	os << ",\"netId\":";
	if (p.net) {
		os << r.netId;
	} else {
		os << "null";
	}
	os << ",\"netName\":";
	appendEscaped(os, r.netName);
	os << ",\"side\":";
	appendEscaped(os, sideToString(p.board_side));
	os << ",\"pos\":{\"x\":";
	appendNumber(os, p.position.x);
	os << ",\"y\":";
	appendNumber(os, p.position.y);
	os << "},\"shape\":";
	appendEscaped(os, shapeToString(p.shape));
	os << ",\"diameter\":";
	appendNumber(os, p.diameter);
	os << ",\"size\":{\"x\":";
	appendNumber(os, p.size.x);
	os << ",\"y\":";
	appendNumber(os, p.size.y);
	os << "},\"angle\":" << p.angle;
	os << "},\"measurements\":{";
	os << "\"diode\":";
	appendMeasureField(os, r.diode);
	os << ",\"voltage\":";
	appendMeasureField(os, r.voltage);
	os << ",\"ohm\":";
	appendMeasureField(os, r.ohm);
	os << ",\"ohm_black\":";
	appendMeasureField(os, r.ohm_black);
	os << "},\"overlay\":{";
	os << "\"note\":";
	appendEscaped(os, r.overlayNote);
	os << ",\"show_name\":";
	appendEscaped(os, r.overlayShowName);
	os << ",\"voltage_flag\":";
	appendEscaped(os, voltageFlagToString(r.overlayVoltageFlag));
	os << "}}";
	return os.str();
}

std::string ExportPartSummaryJson(const Board &board, const Annotations &ann,
                                  const std::string &boardId,
                                  const std::string &sourceName,
                                  const std::string &part) {
	const Component *comp = FindComponent(board, part);
	if (!comp) return {};
	const Component &c = *comp;

	// Center + outline: match board_json deriveCompGeom / appendComponentOutline.
	const CompGeom geom = deriveCompGeom(c);
	const float cx = geom.cx;
	const float cy = geom.cy;


	const char *mountStr = "unknown";
	switch (c.mount_type) {
		case Component::kMountTypeSMD: mountStr = "smd"; break;
		case Component::kMountTypeDIP: mountStr = "dip"; break;
		default: break;
	}
	const char *typeStr = "unknown";
	switch (c.component_type) {
		case Component::kComponentTypeDummy: typeStr = "dummy"; break;
		case Component::kComponentTypeConnector: typeStr = "connector"; break;
		case Component::kComponentTypeIC: typeStr = "ic"; break;
		case Component::kComponentTypeResistor: typeStr = "resistor"; break;
		case Component::kComponentTypeCapacitor: typeStr = "capacitor"; break;
		case Component::kComponentTypeDiode: typeStr = "diode"; break;
		case Component::kComponentTypeTransistor: typeStr = "transistor"; break;
		case Component::kComponentTypeCrystal: typeStr = "crystal"; break;
		case Component::kComponentTypeJellyBean: typeStr = "jellybean"; break;
		case Component::kComponentTypeBoard: typeStr = "board"; break;
		case Component::kComponentTypeInductor: typeStr = "inductor"; break;
		default: break;
	}

	// Global pin index for export ids (component pins ignore index for non-nails).
	std::unordered_map<const Pin *, size_t> pinGlobalIndex;
	{
		const auto &allPins = mutableBoard(board).Pins();
		pinGlobalIndex.reserve(allPins.size());
		for (size_t i = 0; i < allPins.size(); ++i) {
			pinGlobalIndex.emplace(allPins[i].get(), i);
		}
	}

	// Export net ids matching board JSON order.
	std::unordered_map<const Net *, int> netIds;
	int nextNetId = 1;
	auto assignNet = [&](const Net *n) {
		if (!n) return;
		if (netIds.find(n) != netIds.end()) return;
		netIds.emplace(n, nextNetId++);
	};
	{
		Board &b = mutableBoard(board);
		for (const auto &n : b.Nets()) {
			if (n) assignNet(n.get());
		}
		for (const auto &p : b.Pins()) {
			if (p && p->net) assignNet(p->net);
		}
		for (const auto &t : b.Tracks()) {
			if (t && t->net) assignNet(t->net);
		}
		for (const auto &v : b.Vias()) {
			if (v && v->net) assignNet(v->net);
		}
		for (const auto &a : b.arcs()) {
			if (a && a->net) assignNet(a->net);
		}
	}

	auto exportNetIdOf = [&](const Net *n) -> int {
		if (!n) return 0;
		const auto it = netIds.find(n);
		return it != netIds.end() ? it->second : 0;
	};

	std::ostringstream os;
	os << "{\"boardId\":";
	appendEscaped(os, boardId);
	os << ",\"sourceName\":";
	appendEscaped(os, sourceName);
	os << ",\"part\":{";
	os << "\"name\":";
	appendEscaped(os, c.name);
	os << ",\"side\":";
	appendEscaped(os, sideToString(c.board_side));
	os << ",\"mount\":";
	appendEscaped(os, mountStr);
	os << ",\"type\":";
	appendEscaped(os, typeStr);
	os << ",\"mfgcode\":";
	appendEscaped(os, c.mfgcode);
	os << ",\"center\":";
	appendPoint(os, cx, cy);
	appendComponentOutline(os, c, geom);
	os << ",\"pins\":[";
	for (size_t pi = 0; pi < c.pins.size(); ++pi) {
		if (pi) os << ',';
		if (!c.pins[pi]) {
			appendEscaped(os, "");
			continue;
		}
		size_t globalIdx = pi;
		auto git = pinGlobalIndex.find(c.pins[pi].get());
		if (git != pinGlobalIndex.end()) globalIdx = git->second;
		appendEscaped(os, ExportPinId(*c.pins[pi], globalIdx));
	}
	os << "]}";

	// Detailed pin list (netId + netName).
	os << ",\"pins\":[";
	{
		bool first = true;
		for (size_t pi = 0; pi < c.pins.size(); ++pi) {
			if (!c.pins[pi]) continue;
			const Pin &p = *c.pins[pi];
			if (!first) os << ',';
			first = false;
			size_t globalIdx = pi;
			auto git = pinGlobalIndex.find(&p);
			if (git != pinGlobalIndex.end()) globalIdx = git->second;
			os << "{\"id\":";
			appendEscaped(os, ExportPinId(p, globalIdx));
			os << ",\"number\":";
			appendEscaped(os, p.number);
			os << ",\"name\":";
			appendEscaped(os, p.name);
			os << ",\"type\":";
			appendEscaped(os, pinTypeToString(p.type));
			os << ",\"netId\":";
			if (p.net) {
				os << exportNetIdOf(p.net);
			} else {
				os << "null";
			}
			os << ",\"netName\":";
			appendEscaped(os, p.net ? p.net->name : std::string{});
			os << '}';
		}
	}
	os << ']';

	// Overlay partInfo (defaults when missing).
	const PartInfo *pi = nullptr;
	{
		const auto it = ann.partInfos.find(part);
		if (it != ann.partInfos.end()) pi = &it->second;
	}
	os << ",\"partInfo\":{";
	os << "\"part_type\":";
	appendEscaped(os, pi ? pi->part_type : std::string{});
	os << ",\"angle\":" << static_cast<int>(pi ? pi->angle : PartAngle::_0);
	os << ",\"operating_conditions\":[";
	if (pi) {
		for (size_t i = 0; i < pi->operating_conditions.size(); ++i) {
			if (i) os << ',';
			const auto &oc = pi->operating_conditions[i];
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
			afield("inputs", oc.inputs);
			afield("outputs", oc.outputs);
			afield("enables", oc.enables);
			sfield("note", oc.note);
			os << '}';
		}
	}
	os << "]}}";
	return os.str();
}


std::string AllocateConditionId(const std::vector<OperatingCondition> &ocs) {
	std::set<std::string> used;
	for (const auto &oc : ocs) {
		if (!oc.id.empty()) used.insert(oc.id);
	}
	// Deterministic: oc_0001, oc_0002, ... next free
	for (unsigned n = 1; n < 100000u; ++n) {
		std::ostringstream os;
		os << "oc_" << std::setw(4) << std::setfill('0') << n;
		const std::string candidate = os.str();
		if (!used.count(candidate)) return candidate;
	}
	// Extremely unlikely fallback
	return "oc_ffff";
}

std::string AllocateConditionId(const PartInfo &part) {
	return AllocateConditionId(part.operating_conditions);
}

bool NormalizeOperatingCondition(OperatingCondition &oc, std::string &err) {
	err.clear();
	oc.id = trimCopy(oc.id);
	oc.name = trimCopy(oc.name);
	oc.note = trimCopy(oc.note);

	if (oc.id.size() > 64) {
		err = "id exceeds 64 characters";
		return false;
	}
	if (oc.name.size() > 2048) {
		err = "name exceeds 2048 characters";
		return false;
	}
	if (oc.note.size() > 2048) {
		err = "note exceeds 2048 characters";
		return false;
	}

	bool ok = true;
	normalizeLabelArray(oc.inputs, err, ok);
	if (!ok) return false;
	normalizeLabelArray(oc.outputs, err, ok);
	if (!ok) return false;
	normalizeLabelArray(oc.enables, err, ok);
	if (!ok) return false;
	return true;
}

} // namespace obv
