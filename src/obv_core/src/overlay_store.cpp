// Overlay store: desktop-compatible YAML (Version 0.0.2 PartInfos/NetInfos) +
// optional SQLite freeform annotations, plus JSON export/import for the API.
//
// On-disk naming (must match Annotations::Load / SavePinInfos):
//   yaml   = boardPath.string() + ".yaml"
//   sqlite = boardPath with last '.' replaced by '_' + ".sqlite3"
//
// ApplyOverlayJson replaces partInfos/netInfos only; freeform annotation CRUD
// stays on Annotations::Add/Update/Remove/GenerateList.

#include "obv_core/overlay_store.h"

#include "platform.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace obv {
namespace {

// ---------- JSON write helpers (same style as board_json.cpp) ----------

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

const char *voltageFlagToString(PinVoltageFlag f) {
	switch (f) {
		case PinVoltageFlag::input: return "input";
		case PinVoltageFlag::output: return "output";
		case PinVoltageFlag::unknown:
		default: return "unknown";
	}
}

bool voltageFlagFromString(const std::string &s, PinVoltageFlag &out) {
	if (s == "input") {
		out = PinVoltageFlag::input;
		return true;
	}
	if (s == "output") {
		out = PinVoltageFlag::output;
		return true;
	}
	if (s == "unknown" || s.empty()) {
		out = PinVoltageFlag::unknown;
		return true;
	}
	return false;
}

// PartAngle is serialized as integer matching the enum (_0=0, _270=1, _180=2, _90=3, sorted=4).
bool partAngleFromInt(int v, PartAngle &out) {
	switch (v) {
		case 0: out = PartAngle::_0; return true;
		case 1: out = PartAngle::_270; return true;
		case 2: out = PartAngle::_180; return true;
		case 3: out = PartAngle::_90; return true;
		case 4: out = PartAngle::sorted; return true;
		default: return false;
	}
}

void appendPinInfo(std::ostringstream &os, const PinInfo &pi) {
	os << '{';
	bool first = true;
	auto field = [&](const char *key, const std::string &val) {
		if (val.empty()) return;
		if (!first) os << ',';
		first = false;
		appendEscaped(os, key);
		os << ':';
		appendEscaped(os, val);
	};
	field("show_name", pi.show_name);
	field("diode", pi.diode);
	field("voltage", pi.voltage);
	field("ohm", pi.ohm);
	field("ohm_black", pi.ohm_black);
	field("note", pi.note);
	if (pi.voltage_flag != PinVoltageFlag::unknown) {
		if (!first) os << ',';
		first = false;
		appendEscaped(os, "voltage_flag");
		os << ':';
		appendEscaped(os, voltageFlagToString(pi.voltage_flag));
	}
	os << '}';
}

void appendPartInfo(std::ostringstream &os, const PartInfo &pi) {
	os << '{';
	bool first = true;
	if (!pi.part_type.empty()) {
		appendEscaped(os, "part_type");
		os << ':';
		appendEscaped(os, pi.part_type);
		first = false;
	}
	if (pi.angle != PartAngle::_0) {
		if (!first) os << ',';
		first = false;
		appendEscaped(os, "angle");
		os << ':' << static_cast<int>(pi.angle);
	}
	if (!pi.pins.empty()) {
		if (!first) os << ',';
		first = false;
		appendEscaped(os, "pins");
		os << ":{";
		bool pfirst = true;
		for (const auto &[pinName, pinInfo] : pi.pins) {
			if (!pfirst) os << ',';
			pfirst = false;
			appendEscaped(os, pinName);
			os << ':';
			appendPinInfo(os, pinInfo);
		}
		os << '}';
	}
	(void)first;
	os << '}';
}

void appendNetInfo(std::ostringstream &os, const NetInfo &ni) {
	os << '{';
	bool first = true;
	if (!ni.showname.empty()) {
		appendEscaped(os, "showname");
		os << ':';
		appendEscaped(os, ni.showname);
		first = false;
	}
	if (!ni.note.empty()) {
		if (!first) os << ',';
		first = false;
		appendEscaped(os, "note");
		os << ':';
		appendEscaped(os, ni.note);
	}
	(void)first;
	os << '}';
}

// ---------- Minimal JSON reader for ApplyOverlayJson ----------

struct JsonCursor {
	const std::string &s;
	size_t i = 0;
	std::string err;

	explicit JsonCursor(const std::string &in) : s(in) {}

	void skipWs() {
		while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
	}

	bool fail(const char *msg) {
		if (err.empty()) err = msg;
		return false;
	}

	bool expect(char c) {
		skipWs();
		if (i >= s.size() || s[i] != c) return fail("expected character");
		++i;
		return true;
	}

	bool peek(char c) {
		skipWs();
		return i < s.size() && s[i] == c;
	}

	bool parseString(std::string &out) {
		skipWs();
		if (i >= s.size() || s[i] != '"') return fail("expected string");
		++i;
		out.clear();
		while (i < s.size()) {
			char c = s[i++];
			if (c == '"') return true;
			if (c == '\\') {
				if (i >= s.size()) return fail("truncated escape");
				char e = s[i++];
				switch (e) {
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'u': {
						if (i + 4 > s.size()) return fail("truncated unicode escape");
						unsigned code = 0;
						for (int k = 0; k < 4; ++k) {
							char h = s[i++];
							code <<= 4;
							if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
							else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
							else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
							else return fail("bad unicode escape");
						}
						// BMP only; encode as UTF-8
						if (code < 0x80) {
							out.push_back(static_cast<char>(code));
						} else if (code < 0x800) {
							out.push_back(static_cast<char>(0xC0 | (code >> 6)));
							out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
						} else {
							out.push_back(static_cast<char>(0xE0 | (code >> 12)));
							out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
							out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
						}
						break;
					}
					default: return fail("unknown escape");
				}
			} else {
				out.push_back(c);
			}
		}
		return fail("unterminated string");
	}

	bool parseNumber(double &out) {
		skipWs();
		size_t start = i;
		if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
		if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])))
			return fail("expected number");
		while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
		if (i < s.size() && s[i] == '.') {
			++i;
			while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
		}
		if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
			++i;
			if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
			while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
		}
		try {
			out = std::stod(s.substr(start, i - start));
		} catch (...) {
			return fail("bad number");
		}
		return true;
	}

	bool parseBool(bool &out) {
		skipWs();
		if (s.compare(i, 4, "true") == 0) {
			i += 4;
			out = true;
			return true;
		}
		if (s.compare(i, 5, "false") == 0) {
			i += 5;
			out = false;
			return true;
		}
		return fail("expected bool");
	}

	bool parseNull() {
		skipWs();
		if (s.compare(i, 4, "null") == 0) {
			i += 4;
			return true;
		}
		return fail("expected null");
	}

	// Skip any JSON value (used to ignore annotations array / unknown fields).
	bool skipValue() {
		skipWs();
		if (i >= s.size()) return fail("unexpected end");
		char c = s[i];
		if (c == '"') {
			std::string tmp;
			return parseString(tmp);
		}
		if (c == '{' ) {
			if (!expect('{')) return false;
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			for (;;) {
				std::string key;
				if (!parseString(key)) return false;
				if (!expect(':')) return false;
				if (!skipValue()) return false;
				skipWs();
				if (peek('}')) {
					++i;
					return true;
				}
				if (!expect(',')) return false;
			}
		}
		if (c == '[') {
			if (!expect('[')) return false;
			skipWs();
			if (peek(']')) {
				++i;
				return true;
			}
			for (;;) {
				if (!skipValue()) return false;
				skipWs();
				if (peek(']')) {
					++i;
					return true;
				}
				if (!expect(',')) return false;
			}
		}
		if (c == 't' || c == 'f') {
			bool b;
			return parseBool(b);
		}
		if (c == 'n') return parseNull();
		if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c))) {
			double d;
			return parseNumber(d);
		}
		return fail("unknown value");
	}

	bool parsePinInfo(PinInfo &pi) {
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string key;
			if (!parseString(key)) return false;
			if (!expect(':')) return false;
			if (key == "show_name" || key == "diode" || key == "voltage" || key == "ohm" ||
			    key == "ohm_black" || key == "note") {
				std::string val;
				if (!parseString(val)) return false;
				if (key == "show_name") pi.show_name = std::move(val);
				else if (key == "diode") pi.diode = std::move(val);
				else if (key == "voltage") pi.voltage = std::move(val);
				else if (key == "ohm") pi.ohm = std::move(val);
				else if (key == "ohm_black") pi.ohm_black = std::move(val);
				else if (key == "note") pi.note = std::move(val);
			} else if (key == "voltage_flag") {
				std::string val;
				if (!parseString(val)) return false;
				if (!voltageFlagFromString(val, pi.voltage_flag))
					return fail("bad voltage_flag");
			} else {
				if (!skipValue()) return false;
			}
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}

	bool parsePinsMap(std::map<std::string, PinInfo> &pins, const std::string &partName) {
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string pinName;
			if (!parseString(pinName)) return false;
			if (!expect(':')) return false;
			PinInfo pi;
			pi.partName = partName;
			pi.pinName = pinName;
			if (!parsePinInfo(pi)) return false;
			pins[pinName] = std::move(pi);
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}

	bool parsePartInfo(PartInfo &pi) {
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string key;
			if (!parseString(key)) return false;
			if (!expect(':')) return false;
			if (key == "part_type") {
				if (!parseString(pi.part_type)) return false;
			} else if (key == "angle") {
				double d = 0;
				if (!parseNumber(d)) return false;
				if (!partAngleFromInt(static_cast<int>(d), pi.angle))
					return fail("bad part angle");
			} else if (key == "pins") {
				if (!parsePinsMap(pi.pins, pi.partName)) return false;
			} else {
				if (!skipValue()) return false;
			}
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}

	bool parsePartInfos(std::map<std::string, PartInfo> &out) {
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string partName;
			if (!parseString(partName)) return false;
			if (!expect(':')) return false;
			PartInfo pi;
			pi.partName = partName;
			if (!parsePartInfo(pi)) return false;
			// Ensure pin ownership fields after parse
			for (auto &[pn, pin] : pi.pins) {
				pin.partName = partName;
				pin.pinName = pn;
			}
			out[partName] = std::move(pi);
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}

	bool parseNetInfo(NetInfo &ni) {
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string key;
			if (!parseString(key)) return false;
			if (!expect(':')) return false;
			if (key == "showname") {
				if (!parseString(ni.showname)) return false;
			} else if (key == "note") {
				if (!parseString(ni.note)) return false;
			} else {
				if (!skipValue()) return false;
			}
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}

	bool parseNetInfos(std::map<std::string, NetInfo> &out) {
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string netName;
			if (!parseString(netName)) return false;
			if (!expect(':')) return false;
			NetInfo ni;
			ni.name = netName;
			if (!parseNetInfo(ni)) return false;
			out[netName] = std::move(ni);
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}

	bool parseRoot(std::map<std::string, PartInfo> &parts, std::map<std::string, NetInfo> &nets,
	               bool &gotParts, bool &gotNets) {
		gotParts = false;
		gotNets = false;
		if (!expect('{')) return false;
		skipWs();
		if (peek('}')) {
			++i;
			return true;
		}
		for (;;) {
			std::string key;
			if (!parseString(key)) return false;
			if (!expect(':')) return false;
			// Accept both camelCase API names and desktop YAML-ish keys.
			if (key == "partInfos" || key == "PartInfos") {
				parts.clear();
				if (!parsePartInfos(parts)) return false;
				gotParts = true;
			} else if (key == "netInfos" || key == "NetInfos") {
				nets.clear();
				if (!parseNetInfos(nets)) return false;
				gotNets = true;
			} else {
				// Ignore annotations and any other fields.
				if (!skipValue()) return false;
			}
			skipWs();
			if (peek('}')) {
				++i;
				return true;
			}
			if (!expect(',')) return false;
		}
	}
};

} // namespace

bool LoadOverlayForBoard(const filesystem::path &boardPath, Annotations &out, std::string &err) {
	err.clear();
	if (boardPath.empty()) {
		err = "board path is empty";
		return false;
	}
	// Close any previous sqlite handle before rebinding filename.
	out.Close();
	out.annotations.clear();
	out.partInfos.clear();
	out.netInfos.clear();
	out.SetFilename(boardPath.string());
	out.Load();           // sqlite when HAVE_SQLITE3
	out.RefreshPinInfos(); // yaml sidecar
	return true;
}

bool SavePartNetYaml(const filesystem::path &boardPath, const Annotations &ann, std::string &err) {
	err.clear();
	if (boardPath.empty()) {
		err = "board path is empty";
		return false;
	}

	// Annotations::SavePinInfos -> serialize ignores file_write_text's bool return
	// (desktop always voids the write result). Detect failure here via create /
	// mtime / size change, Version header, and a full PartInfos/NetInfos reload.
	const auto yamlPath = boardPath.string() + ".yaml";
	const filesystem::path yamlFs(yamlPath);
	std::error_code ec;
	const bool existed = filesystem::exists(yamlFs, ec) && !ec;
	uintmax_t sizeBefore = 0;
	filesystem::file_time_type mtimeBefore{};
	bool haveMtimeBefore = false;
	if (existed) {
		sizeBefore = filesystem::file_size(yamlFs, ec);
		if (ec) sizeBefore = 0;
		mtimeBefore = filesystem::last_write_time(yamlFs, ec);
		haveMtimeBefore = !ec;
	}

	// SavePinInfos is non-const (prunes empty map entries). Copy only YAML-relevant
	// fields so we never share/close the caller's sqlite handle.
	Annotations copy;
	copy.filename = boardPath.string();
	copy.partInfos = ann.partInfos;
	copy.netInfos = ann.netInfos;
	copy.SavePinInfos();

	// Post-prune keys that serialize was supposed to persist.
	std::vector<std::string> expectedParts;
	expectedParts.reserve(copy.partInfos.size());
	for (const auto &kv : copy.partInfos) expectedParts.push_back(kv.first);
	std::vector<std::string> expectedNets;
	expectedNets.reserve(copy.netInfos.size());
	for (const auto &kv : copy.netInfos) expectedNets.push_back(kv.first);

	if (!filesystem::exists(yamlFs, ec) || ec) {
		err = "failed to write " + yamlPath;
		return false;
	}

	const uintmax_t sizeAfter = filesystem::file_size(yamlFs, ec);
	if (ec) {
		err = "failed to stat " + yamlPath;
		return false;
	}
	const auto mtimeAfter = filesystem::last_write_time(yamlFs, ec);
	const bool mtimeChanged = haveMtimeBefore && !ec && (mtimeAfter != mtimeBefore);
	const bool sizeChanged = sizeAfter != sizeBefore;
	const bool newlyCreated = !existed;
	if (!newlyCreated && !mtimeChanged && !sizeChanged) {
		err = "failed to write " + yamlPath + " (file unchanged after SavePinInfos)";
		return false;
	}

	// Content check: serialize always emits Version 0.0.2 even for empty maps.
	const std::string content = file_read_text(yamlPath);
	if (content.find("0.0.2") == std::string::npos) {
		err = "failed to write " + yamlPath + " (missing Version 0.0.2)";
		return false;
	}

	// Reload into a temporary Annotations so partial writes (Version header only,
	// truncated PartInfos/NetInfos) fail closed. Do not mutate caller's maps.
	Annotations tmp;
	tmp.filename = boardPath.string();
	tmp.RefreshPinInfos();

	for (const auto &key : expectedParts) {
		auto it = tmp.partInfos.find(key);
		if (it == tmp.partInfos.end()) {
			err = "incomplete write " + yamlPath + " (missing PartInfos key: " + key + ")";
			return false;
		}
		const auto &want = copy.partInfos.at(key);
		const auto &got = it->second;
		if (!want.part_type.empty() && got.part_type != want.part_type) {
			err = "incomplete write " + yamlPath + " (PartInfos part_type mismatch: " + key + ")";
			return false;
		}
		for (const auto &pkv : want.pins) {
			auto pit = got.pins.find(pkv.first);
			if (pit == got.pins.end()) {
				err = "incomplete write " + yamlPath + " (missing pin " + pkv.first + " under " + key + ")";
				return false;
			}
			if (!pkv.second.note.empty() && pit->second.note != pkv.second.note) {
				err = "incomplete write " + yamlPath + " (pin note mismatch: " + key + "/" + pkv.first + ")";
				return false;
			}
			if (!pkv.second.show_name.empty() && pit->second.show_name != pkv.second.show_name) {
				err = "incomplete write " + yamlPath + " (pin show_name mismatch: " + key + "/" + pkv.first + ")";
				return false;
			}
		}
	}
	for (const auto &key : expectedNets) {
		auto it = tmp.netInfos.find(key);
		if (it == tmp.netInfos.end()) {
			err = "incomplete write " + yamlPath + " (missing NetInfos key: " + key + ")";
			return false;
		}
		const auto &want = copy.netInfos.at(key);
		const auto &got = it->second;
		if (!want.note.empty() && got.note != want.note) {
			err = "incomplete write " + yamlPath + " (NetInfos note mismatch: " + key + ")";
			return false;
		}
		if (!want.showname.empty() && got.showname != want.showname) {
			err = "incomplete write " + yamlPath + " (NetInfos showname mismatch: " + key + ")";
			return false;
		}
	}
	return true;
}

std::string ExportOverlayJson(const Annotations &ann) {
	std::ostringstream os;
	os << '{';

	// annotations
	os << "\"annotations\":[";
	bool first = true;
	for (const auto &a : ann.annotations) {
		if (!first) os << ',';
		first = false;
		os << '{';
		os << "\"id\":" << a.id;
		os << ",\"side\":" << a.side;
		os << ",\"x\":";
		appendNumber(os, a.x);
		os << ",\"y\":";
		appendNumber(os, a.y);
		os << ",\"net\":";
		appendEscaped(os, a.net);
		os << ",\"part\":";
		appendEscaped(os, a.part);
		os << ",\"pin\":";
		appendEscaped(os, a.pin);
		os << ",\"note\":";
		appendEscaped(os, a.note);
		// Rows loaded into memory are visible=1; soft-deleted never appear.
		os << ",\"visible\":true";
		os << '}';
	}
	os << ']';

	// partInfos
	os << ",\"partInfos\":{";
	first = true;
	for (const auto &[name, pi] : ann.partInfos) {
		if (!first) os << ',';
		first = false;
		appendEscaped(os, name);
		os << ':';
		appendPartInfo(os, pi);
	}
	os << '}';

	// netInfos
	os << ",\"netInfos\":{";
	first = true;
	for (const auto &[name, ni] : ann.netInfos) {
		if (!first) os << ',';
		first = false;
		appendEscaped(os, name);
		os << ':';
		appendNetInfo(os, ni);
	}
	os << '}';

	os << '}';
	return os.str();
}

bool ApplyOverlayJson(Annotations &ann, const std::string &json, std::string &err) {
	err.clear();
	JsonCursor cur(json);
	std::map<std::string, PartInfo> parts;
	std::map<std::string, NetInfo> nets;
	bool gotParts = false;
	bool gotNets = false;
	if (!cur.parseRoot(parts, nets, gotParts, gotNets)) {
		err = cur.err.empty() ? "invalid overlay JSON" : cur.err;
		return false;
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after overlay JSON";
		return false;
	}
	// PUT replaces maps when the corresponding key is present; missing key leaves existing map.
	// Empty object `{}` is valid and a no-op for maps (nothing to replace).
	if (gotParts) ann.partInfos = std::move(parts);
	if (gotNets) ann.netInfos = std::move(nets);
	return true;
}

} // namespace obv
