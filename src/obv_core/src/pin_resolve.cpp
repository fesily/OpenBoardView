// Pin measurement resolve helpers for agent API (mirror web pinValues.ts).
// Priority: overlay > board file field > same-net first local (board.Pins order).

#include "obv_core/pin_resolve.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

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
		os << p.net->number;
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

std::string AllocateConditionId(const PartInfo &part) {
	std::set<std::string> used;
	for (const auto &oc : part.operating_conditions) {
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
