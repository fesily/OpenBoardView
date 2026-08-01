#include "routes.h"

#include "obv_core/board_json.h"
#include "obv_core/overlay_store.h"
#include "obv_core/part_render.h"
#include "obv_core/pin_grid.h"
#include "obv_core/pin_resolve.h"

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <optional>
#include <sstream>
#include <map>
#include <string>
#include <vector>

namespace obv_server {
namespace {

std::string jsonEscape(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

void setError(httplib::Response &res, int status, const char *code, const std::string &message) {
	res.status = status;
	const std::string body = std::string("{\"error\":{\"code\":\"") + code + "\",\"message\":\"" +
							 jsonEscape(message) + "\"}}";
	res.set_content(body, "application/json");
}

std::string entryListJson(const std::vector<BoardRegistry::Entry> &entries) {
	std::ostringstream os;
	os << '[';
	for (size_t i = 0; i < entries.size(); ++i) {
		const auto &e = entries[i];
		if (i) {
			os << ',';
		}
		os << "{\"id\":\"" << jsonEscape(e.id) << "\",\"name\":\"" << jsonEscape(e.name)
		   << "\",\"ok\":" << (e.ok ? "true" : "false") << ",\"error\":\""
		   << jsonEscape(e.parseError) << "\"";
		if (!e.displayPath.empty()) {
			os << ",\"path\":\"" << jsonEscape(e.displayPath) << "\"";
		}
		os << '}';
	}
	os << ']';
	return os.str();
}

std::string uploadResponseJson(const BoardRegistry::Entry &e,
							   const std::shared_ptr<const obv::BoardSnapshot> &snap) {
	std::ostringstream os;
	os << "{\"id\":\"" << jsonEscape(e.id) << "\",\"ok\":" << (e.ok ? "true" : "false")
	   << ",\"error\":\"" << jsonEscape(e.parseError) << "\",\"meta\":";
	if (e.ok && snap) {
		const std::string meta = obv::ExportMetaJson(*snap, e.id);
		os << (meta.empty() ? "null" : meta);
	} else {
		os << "null";
	}
	os << '}';
	return os.str();
}

// Read upload via ContentReader so application/x-www-form-urlencoded is not
// pre-parsed (cpp-httplib's 8 KiB form limit would 413 raw board bodies).
bool readBoardUpload(const httplib::Request &req, const httplib::ContentReader &reader,
					 size_t maxBytes, std::string &name, std::string &body, std::string &err,
					 bool &tooLarge) {
	tooLarge = false;
	name.clear();
	body.clear();
	err.clear();

	if (req.is_multipart_form_data()) {
		struct Part {
			std::string name;
			std::string filename;
			std::string content;
		};
		std::vector<Part> parts;
		Part *cur = nullptr;

		const bool readOk = reader(
			[&](const httplib::MultipartFormData &file) {
				parts.push_back(Part{file.name, file.filename, {}});
				cur = &parts.back();
				return true;
			},
			[&](const char *data, size_t len) {
				if (!cur) {
					return false;
				}
				if (cur->content.size() + len > maxBytes) {
					tooLarge = true;
					return false;
				}
				cur->content.append(data, len);
				return true;
			});

		if (tooLarge) {
			err = "upload exceeds maxUploadBytes";
			return false;
		}
		if (!readOk) {
			err = "failed to read multipart upload";
			return false;
		}

		const Part *chosen = nullptr;
		for (const auto &p : parts) {
			if (p.name == "file") {
				chosen = &p;
				break;
			}
		}
		if (!chosen) {
			for (const auto &p : parts) {
				if (!p.filename.empty()) {
					chosen = &p;
					break;
				}
			}
		}
		if (!chosen && !parts.empty()) {
			// Match prior fallback: first form part if present.
			chosen = &parts.front();
		}
		if (!chosen) {
			err = "multipart upload missing file field";
			return false;
		}

		name = chosen->filename.empty() ? "upload.bin" : chosen->filename;
		body = chosen->content;
		return true;
	}

	// Raw body (any non-multipart Content-Type, including form-urlencoded / missing).
	if (req.has_header("X-Filename")) {
		name = req.get_header_value("X-Filename");
	} else {
		name = "upload.bin";
	}

	const bool readOk = reader([&](const char *data, size_t len) {
		if (body.size() + len > maxBytes) {
			tooLarge = true;
			return false;
		}
		body.append(data, len);
		return true;
	});

	if (tooLarge) {
		err = "upload exceeds maxUploadBytes";
		return false;
	}
	if (!readOk) {
		err = "failed to read upload body";
		return false;
	}
	if (body.empty()) {
		err = "empty upload body";
		return false;
	}
	return true;
}

std::string pathParam(const httplib::Request &req, const char *name) {
	const auto it = req.path_params.find(name);
	if (it == req.path_params.end()) {
		return {};
	}
	return it->second;
}

// boardId must be lowercase sha256 hex — blocks ../ and other path segments.
bool requireBoardId(const std::string &id, httplib::Response &res) {
	if (id.empty()) {
		setError(res, 400, "BAD_REQUEST", "missing board id");
		return false;
	}
	if (!BoardRegistry::IsValidBoardId(id)) {
		setError(res, 400, "BAD_REQUEST", "board id must be 64 lowercase hex characters");
		return false;
	}
	return true;
}

// Resolve boardId | unique displayPath | unique basename; writes error on failure.
bool applyBoardRef(BoardRegistry &registry, const std::string &ref, httplib::Response &res,
				   std::string &boardId) {
	auto r = registry.ResolveRef(ref);
	if (r.status == BoardRegistry::BoardRefStatus::Ambiguous) {
		std::string msg = "ambiguous board ref";
		for (const auto &c : r.candidates) {
			msg += "; ";
			msg += c;
		}
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
	if (registry.TryGetEntry(boardId, e) && !e.displayPath.empty()) {
		return e.displayPath;
	}
	if (registry.TryGetEntry(boardId, e)) {
		return e.name;
	}
	return boardId;
}

bool parsePartRenderOpts(const httplib::Request &req, obv::PartRenderOpts &opts, std::string &err) {
	opts = {};
	if (req.has_param("scale")) {
		opts.scale = std::atof(req.get_param_value("scale").c_str());
		if (!(opts.scale > 0) || opts.scale > 100) {
			err = "invalid scale";
			return false;
		}
	}
	if (req.has_param("padding")) {
		opts.padding = std::atof(req.get_param_value("padding").c_str());
		if (opts.padding < 0) {
			err = "invalid padding";
			return false;
		}
	}
	if (req.has_param("maxEdge")) {
		opts.maxEdge = std::atoi(req.get_param_value("maxEdge").c_str());
		if (opts.maxEdge < 64 || opts.maxEdge > 2048) {
			err = "invalid maxEdge";
			return false;
		}
	}
	if (req.has_param("labels")) {
		opts.labels = req.get_param_value("labels") != "0";
	}
	if (req.has_param("partName")) {
		opts.partName = req.get_param_value("partName") != "0";
	}
	return true;
}

void mapPartRenderError(httplib::Response &res, const std::string &errCode,
						const std::string &errMessage) {
	if (errCode == "PART_NOT_FOUND") {
		setError(res, 404, "PART_NOT_FOUND",
				 errMessage.empty() ? "part not found" : errMessage);
	} else if (errCode == "PART_NO_GEOMETRY" || errCode == "BAD_REQUEST") {
		setError(res, 400, errCode.c_str(),
				 errMessage.empty() ? "bad request" : errMessage);
	} else if (errCode == "RENDER_FAILED") {
		setError(res, 500, "RENDER_FAILED",
				 errMessage.empty() ? "render failed" : errMessage);
	} else {
		setError(res, 500, errCode.empty() ? "RENDER_FAILED" : errCode.c_str(),
				 errMessage.empty() ? "render failed" : errMessage);
	}
}

// Lowercase file extension including leading '.' (empty if none).
std::string fileExtensionLower(const std::string &name) {
	const auto slash = name.find_last_of("/\\");
	const std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
	const auto dot = base.find_last_of('.');
	if (dot == std::string::npos || dot + 1 >= base.size()) {
		return {};
	}
	std::string ext = base.substr(dot);
	for (char &c : ext) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return ext;
}

// Known board / content-sniffed formats. Rejects executables and scripts.
bool isAllowedUploadExtension(const std::string &name) {
	static const char *kAllowed[] = {".brd",  ".brd2", ".bdv", ".bvr", ".bvr3", ".fz",
									 ".cae",  ".bom",  ".asc", ".cst", ".json", ".cad",
									 ".pcb",  ".alg",  ".xzz", ".bin", ".txt",  ".gencad"};
	const std::string ext = fileExtensionLower(name);
	if (ext.empty()) {
		// Content-detected formats often omit/ignore extension; allow bare names.
		return true;
	}
	for (const char *a : kAllowed) {
		if (ext == a) {
			return true;
		}
	}
	return false;
}


// Minimal JSON helpers for annotation bodies (not full JSON parser).
struct MiniJson {
	const std::string &s;
	size_t i = 0;
	std::string err;

	explicit MiniJson(const std::string &json) : s(json) {}

	void skipWs() {
		while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
			++i;
		}
	}

	bool match(char c) {
		skipWs();
		if (i < s.size() && s[i] == c) {
			++i;
			return true;
		}
		return false;
	}

	bool expect(char c) {
		if (!match(c)) {
			err = std::string("expected '") + c + "'";
			return false;
		}
		return true;
	}

	bool parseString(std::string &out) {
		skipWs();
		if (i >= s.size() || s[i] != '"') {
			err = "expected string";
			return false;
		}
		++i;
		out.clear();
		while (i < s.size()) {
			const char c = s[i++];
			if (c == '"') {
				return true;
			}
			if (c == '\\') {
				if (i >= s.size()) {
					err = "unterminated escape";
					return false;
				}
				const char e = s[i++];
				switch (e) {
				case '"':
				case '\\':
				case '/':
					out.push_back(e);
					break;
				case 'b':
					out.push_back('\b');
					break;
				case 'f':
					out.push_back('\f');
					break;
				case 'n':
					out.push_back('\n');
					break;
				case 'r':
					out.push_back('\r');
					break;
				case 't':
					out.push_back('\t');
					break;
				case 'u': {
					if (i + 4 > s.size()) {
						err = "bad unicode escape";
						return false;
					}
					// MVP: accept \uXXXX as raw hex digits into '?' for non-ASCII; keep BMP latin.
					unsigned code = 0;
					for (int k = 0; k < 4; ++k) {
						const char h = s[i++];
						code <<= 4;
						if (h >= '0' && h <= '9') {
							code |= static_cast<unsigned>(h - '0');
						} else if (h >= 'a' && h <= 'f') {
							code |= static_cast<unsigned>(h - 'a' + 10);
						} else if (h >= 'A' && h <= 'F') {
							code |= static_cast<unsigned>(h - 'A' + 10);
						} else {
							err = "bad unicode escape";
							return false;
						}
					}
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
				default:
					err = "bad escape";
					return false;
				}
			} else {
				out.push_back(c);
			}
		}
		err = "unterminated string";
		return false;
	}

	bool parseNumber(double &out) {
		skipWs();
		const size_t start = i;
		if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
			++i;
		}
		bool any = false;
		while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
			any = true;
			++i;
		}
		if (i < s.size() && s[i] == '.') {
			++i;
			while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
				any = true;
				++i;
			}
		}
		if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
			++i;
			if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
				++i;
			}
			while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
				++i;
			}
		}
		if (!any) {
			err = "expected number";
			return false;
		}
		try {
			out = std::stod(s.substr(start, i - start));
		} catch (...) {
			err = "invalid number";
			return false;
		}
		return true;
	}

	bool parseInt(int &out) {
		double d = 0;
		if (!parseNumber(d)) {
			return false;
		}
		out = static_cast<int>(d);
		return true;
	}

	bool skipValue() {
		skipWs();
		if (i >= s.size()) {
			err = "unexpected end";
			return false;
		}
		const char c = s[i];
		if (c == '"') {
			std::string tmp;
			return parseString(tmp);
		}
		if (c == '{' ) {
			if (!expect('{')) {
				return false;
			}
			skipWs();
			if (match('}')) {
				return true;
			}
			for (;;) {
				std::string key;
				if (!parseString(key)) {
					return false;
				}
				if (!expect(':')) {
					return false;
				}
				if (!skipValue()) {
					return false;
				}
				skipWs();
				if (match('}')) {
					return true;
				}
				if (!expect(',')) {
					return false;
				}
			}
		}
		if (c == '[') {
			if (!expect('[')) {
				return false;
			}
			skipWs();
			if (match(']')) {
				return true;
			}
			for (;;) {
				if (!skipValue()) {
					return false;
				}
				skipWs();
				if (match(']')) {
					return true;
				}
				if (!expect(',')) {
					return false;
				}
			}
		}
		if (s.compare(i, 4, "true") == 0) {
			i += 4;
			return true;
		}
		if (s.compare(i, 5, "false") == 0) {
			i += 5;
			return true;
		}
		if (s.compare(i, 4, "null") == 0) {
			i += 4;
			return true;
		}
		double d = 0;
		return parseNumber(d);
	}
};

struct NewAnnotationBody {
	int side = 0;
	double x = 0;
	double y = 0;
	std::string net;
	std::string part;
	std::string pin;
	std::string note;
	bool gotSide = false;
	bool gotX = false;
	bool gotY = false;
	bool gotNote = false;
};

bool parseNewAnnotation(const std::string &json, NewAnnotationBody &out, std::string &err) {
	MiniJson cur(json);
	if (!cur.expect('{')) {
		err = cur.err.empty() ? "invalid JSON object" : cur.err;
		return false;
	}
	cur.skipWs();
	if (cur.match('}')) {
		err = "annotation body missing required fields";
		return false;
	}
	for (;;) {
		std::string key;
		if (!cur.parseString(key)) {
			err = cur.err;
			return false;
		}
		if (!cur.expect(':')) {
			err = cur.err;
			return false;
		}
		if (key == "side") {
			if (!cur.parseInt(out.side)) {
				err = cur.err;
				return false;
			}
			out.gotSide = true;
		} else if (key == "x") {
			if (!cur.parseNumber(out.x)) {
				err = cur.err;
				return false;
			}
			out.gotX = true;
		} else if (key == "y") {
			if (!cur.parseNumber(out.y)) {
				err = cur.err;
				return false;
			}
			out.gotY = true;
		} else if (key == "net") {
			if (!cur.parseString(out.net)) {
				err = cur.err;
				return false;
			}
		} else if (key == "part") {
			if (!cur.parseString(out.part)) {
				err = cur.err;
				return false;
			}
		} else if (key == "pin") {
			if (!cur.parseString(out.pin)) {
				err = cur.err;
				return false;
			}
		} else if (key == "note") {
			if (!cur.parseString(out.note)) {
				err = cur.err;
				return false;
			}
			out.gotNote = true;
		} else {
			if (!cur.skipValue()) {
				err = cur.err;
				return false;
			}
		}
		cur.skipWs();
		if (cur.match('}')) {
			break;
		}
		if (!cur.expect(',')) {
			err = cur.err;
			return false;
		}
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after annotation JSON";
		return false;
	}
	if (!out.gotX || !out.gotY) {
		err = "annotation requires x and y";
		return false;
	}
	return true;
}

bool parseNotePatch(const std::string &json, std::string &note, std::string &err) {
	MiniJson cur(json);
	if (!cur.expect('{')) {
		err = cur.err.empty() ? "invalid JSON object" : cur.err;
		return false;
	}
	bool gotNote = false;
	cur.skipWs();
	if (cur.match('}')) {
		err = "patch body requires note";
		return false;
	}
	for (;;) {
		std::string key;
		if (!cur.parseString(key)) {
			err = cur.err;
			return false;
		}
		if (!cur.expect(':')) {
			err = cur.err;
			return false;
		}
		if (key == "note") {
			if (!cur.parseString(note)) {
				err = cur.err;
				return false;
			}
			gotNote = true;
		} else {
			if (!cur.skipValue()) {
				err = cur.err;
				return false;
			}
		}
		cur.skipWs();
		if (cur.match('}')) {
			break;
		}
		if (!cur.expect(',')) {
			err = cur.err;
			return false;
		}
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after patch JSON";
		return false;
	}
	if (!gotNote) {
		err = "patch body requires note";
		return false;
	}
	return true;
}

bool parseAnnId(const std::string &s, int &out) {
	if (s.empty()) {
		return false;
	}
	size_t idx = 0;
	try {
		out = std::stoi(s, &idx, 10);
	} catch (...) {
		return false;
	}
	return idx == s.size() && out > 0;
}

struct AnnotationJson {
	int id = 0;
	int side = 0;
	double x = 0;
	double y = 0;
	std::string net;
	std::string part;
	std::string pin;
	std::string note;
};

std::string annotationObjectJson(const AnnotationJson &a) {
	std::ostringstream os;
	os << "{\"id\":" << a.id << ",\"side\":" << a.side << ",\"x\":" << a.x << ",\"y\":" << a.y
	   << ",\"net\":\"" << jsonEscape(a.net) << "\",\"part\":\"" << jsonEscape(a.part)
	   << "\",\"pin\":\"" << jsonEscape(a.pin) << "\",\"note\":\"" << jsonEscape(a.note)
	   << "\",\"visible\":true}";
	return os.str();
}

std::optional<AnnotationJson> findAnnotation(const Annotations &ann, int id) {
	for (const auto &a : ann.annotations) {
		if (a.id == id) {
			AnnotationJson j;
			j.id = a.id;
			j.side = a.side;
			j.x = a.x;
			j.y = a.y;
			j.net = a.net;
			j.part = a.part;
			j.pin = a.pin;
			j.note = a.note;
			return j;
		}
	}
	return std::nullopt;
}

std::string operatingConditionObjectJson(const OperatingCondition &oc) {
	std::ostringstream os;
	os << '{';
	bool first = true;
	auto sfield = [&](const char *k, const std::string &v) {
		if (v.empty()) {
			return;
		}
		if (!first) {
			os << ',';
		}
		first = false;
		os << '"' << k << "\":\"" << jsonEscape(v) << '"';
	};
	auto afield = [&](const char *k, const std::vector<std::string> &arr) {
		if (!first) {
			os << ',';
		}
		first = false;
		os << '"' << k << "\":[";
		for (size_t j = 0; j < arr.size(); ++j) {
			if (j) {
				os << ',';
			}
			os << '"' << jsonEscape(arr[j]) << '"';
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
	return os.str();
}

std::string operatingConditionsArrayJson(const std::vector<OperatingCondition> &ocs) {
	std::ostringstream os;
	os << '[';
	for (size_t i = 0; i < ocs.size(); ++i) {
		if (i) {
			os << ',';
		}
		os << operatingConditionObjectJson(ocs[i]);
	}
	os << ']';
	return os.str();
}

std::string operatingConditionsListJson(const std::string &boardId, const std::string &part,
										const std::vector<OperatingCondition> &ocs) {
	std::ostringstream os;
	os << "{\"boardId\":\"" << jsonEscape(boardId) << "\",\"part\":\"" << jsonEscape(part)
	   << "\",\"operating_conditions\":" << operatingConditionsArrayJson(ocs) << '}';
	return os.str();
}

const char *conditionSourceString(obv::ConditionSource src) {
	switch (src) {
	case obv::ConditionSource::Board:
		return "board";
	case obv::ConditionSource::Chip:
		return "chip";
	case obv::ConditionSource::None:
	default:
		return "none";
	}
}

std::string operatingConditionsMergedListJson(const std::string &boardId, const std::string &part,
											  const std::string &partType,
											  const obv::MergedConditions &merged) {
	std::ostringstream os;
	os << "{\"boardId\":\"" << jsonEscape(boardId) << "\",\"part\":\"" << jsonEscape(part)
	   << "\",\"part_type\":\"" << jsonEscape(partType) << "\",\"source\":\""
	   << conditionSourceString(merged.source) << "\",\"operating_conditions\":"
	   << operatingConditionsArrayJson(merged.effective) << ",\"board\":"
	   << operatingConditionsArrayJson(merged.board) << ",\"chip\":"
	   << operatingConditionsArrayJson(merged.chip) << '}';
	return os.str();
}

bool loadChipConditions(BoardRegistry &registry, const std::string &partType,
						std::vector<OperatingCondition> &out) {
	out.clear();
	if (partType.empty()) {
		return true;
	}
	obv::ChipRecord rec;
	std::string code, msg;
	if (!registry.chips().Get(partType, rec, code, msg)) {
		if (code == "CHIP_NOT_FOUND") {
			return true; // empty chip layer
		}
		return false; // real error — caller maps to 500 CHIP_STORE_FAILED
	}
	out = std::move(rec.operating_conditions);
	return true;
}

bool parseStringArray(MiniJson &cur, std::vector<std::string> &out) {
	out.clear();
	if (!cur.expect('[')) {
		return false;
	}
	cur.skipWs();
	if (cur.match(']')) {
		return true;
	}
	for (;;) {
		std::string item;
		if (!cur.parseString(item)) {
			return false;
		}
		out.push_back(std::move(item));
		cur.skipWs();
		if (cur.match(']')) {
			return true;
		}
		if (!cur.expect(',')) {
			return false;
		}
	}
}

bool parseOperatingConditionObject(MiniJson &cur, OperatingCondition &out) {
	out = OperatingCondition{};
	if (!cur.expect('{')) {
		return false;
	}
	cur.skipWs();
	if (cur.match('}')) {
		return true;
	}
	for (;;) {
		std::string key;
		if (!cur.parseString(key)) {
			return false;
		}
		if (!cur.expect(':')) {
			return false;
		}
		if (key == "id") {
			if (!cur.parseString(out.id)) {
				return false;
			}
		} else if (key == "name") {
			if (!cur.parseString(out.name)) {
				return false;
			}
		} else if (key == "note") {
			if (!cur.parseString(out.note)) {
				return false;
			}
		} else if (key == "inputs") {
			if (!parseStringArray(cur, out.inputs)) {
				return false;
			}
		} else if (key == "outputs") {
			if (!parseStringArray(cur, out.outputs)) {
				return false;
			}
		} else if (key == "enables") {
			if (!parseStringArray(cur, out.enables)) {
				return false;
			}
		} else {
			if (!cur.skipValue()) {
				return false;
			}
		}
		cur.skipWs();
		if (cur.match('}')) {
			return true;
		}
		if (!cur.expect(',')) {
			return false;
		}
	}
}

bool parseOperatingConditionBody(const std::string &json, OperatingCondition &out, std::string &err) {
	MiniJson cur(json);
	if (!parseOperatingConditionObject(cur, out)) {
		err = cur.err.empty() ? "invalid operating condition JSON" : cur.err;
		return false;
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after operating condition JSON";
		return false;
	}
	return true;
}

bool parseOperatingConditionsReplaceBody(const std::string &json, std::vector<OperatingCondition> &out,
										 std::string &err) {
	MiniJson cur(json);
	if (!cur.expect('{')) {
		err = cur.err.empty() ? "invalid JSON object" : cur.err;
		return false;
	}
	bool got = false;
	out.clear();
	cur.skipWs();
	if (cur.match('}')) {
		err = "body requires operating_conditions array";
		return false;
	}
	for (;;) {
		std::string key;
		if (!cur.parseString(key)) {
			err = cur.err;
			return false;
		}
		if (!cur.expect(':')) {
			err = cur.err;
			return false;
		}
		if (key == "operating_conditions") {
			if (!cur.expect('[')) {
				err = cur.err.empty() ? "operating_conditions must be array" : cur.err;
				return false;
			}
			cur.skipWs();
			out.clear();
			if (!cur.match(']')) {
				for (;;) {
					OperatingCondition oc;
					if (!parseOperatingConditionObject(cur, oc)) {
						err = cur.err.empty() ? "invalid operating condition" : cur.err;
						return false;
					}
					out.push_back(std::move(oc));
					cur.skipWs();
					if (cur.match(']')) {
						break;
					}
					if (!cur.expect(',')) {
						err = cur.err;
						return false;
					}
				}
			}
			got = true;
		} else {
			if (!cur.skipValue()) {
				err = cur.err;
				return false;
			}
		}
		cur.skipWs();
		if (cur.match('}')) {
			break;
		}
		if (!cur.expect(',')) {
			err = cur.err;
			return false;
		}
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after operating_conditions JSON";
		return false;
	}
	if (!got) {
		err = "body requires operating_conditions array";
		return false;
	}
	return true;
}

std::string trimWs(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
		++b;
	}
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
		--e;
	}
	return s.substr(b, e - b);
}

// { "pins": { "1": { "show_name": "VCC" }, ... } }
// out: pinKey -> show_name (empty string means clear overlay override).
bool parsePinsShowNamePatch(const std::string &json, std::map<std::string, std::string> &out,
							std::string &err) {
	out.clear();
	MiniJson cur(json);
	if (!cur.expect('{')) {
		err = cur.err.empty() ? "invalid JSON object" : cur.err;
		return false;
	}
	bool gotPins = false;
	cur.skipWs();
	if (cur.match('}')) {
		err = "body requires pins object";
		return false;
	}
	for (;;) {
		std::string key;
		if (!cur.parseString(key)) {
			err = cur.err;
			return false;
		}
		if (!cur.expect(':')) {
			err = cur.err;
			return false;
		}
		if (key == "pins") {
			if (!cur.expect('{')) {
				err = cur.err.empty() ? "pins must be an object" : cur.err;
				return false;
			}
			gotPins = true;
			cur.skipWs();
			if (cur.match('}')) {
				err = "pins object is empty";
				return false;
			}
			for (;;) {
				std::string pinKey;
				if (!cur.parseString(pinKey)) {
					err = cur.err;
					return false;
				}
				if (!cur.expect(':')) {
					err = cur.err;
					return false;
				}
				if (!cur.expect('{')) {
					err = cur.err.empty() ? "pin value must be an object" : cur.err;
					return false;
				}
				bool gotShowName = false;
				std::string showName;
				cur.skipWs();
				if (!cur.match('}')) {
					for (;;) {
						std::string field;
						if (!cur.parseString(field)) {
							err = cur.err;
							return false;
						}
						if (!cur.expect(':')) {
							err = cur.err;
							return false;
						}
						if (field == "show_name") {
							if (!cur.parseString(showName)) {
								err = cur.err;
								return false;
							}
							gotShowName = true;
						} else {
							err = "unknown field in pin object: " + field;
							return false;
						}
						cur.skipWs();
						if (cur.match('}')) {
							break;
						}
						if (!cur.expect(',')) {
							err = cur.err;
							return false;
						}
					}
				}
				if (!gotShowName) {
					err = "pin object requires show_name";
					return false;
				}
				showName = trimWs(showName);
				if (showName.size() > 128) {
					err = "show_name exceeds 128 characters";
					return false;
				}
				if (out.size() >= 512) {
					err = "pins limit is 512 keys";
					return false;
				}
				// Last write wins on duplicate keys (stable map overwrite).
				out[pinKey] = std::move(showName);
				cur.skipWs();
				if (cur.match('}')) {
					break;
				}
				if (!cur.expect(',')) {
					err = cur.err;
					return false;
				}
			}
			if (out.empty()) {
				err = "pins object is empty";
				return false;
			}
		} else {
			if (!cur.skipValue()) {
				err = cur.err;
				return false;
			}
		}
		cur.skipWs();
		if (cur.match('}')) {
			break;
		}
		if (!cur.expect(',')) {
			err = cur.err;
			return false;
		}
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after pins patch JSON";
		return false;
	}
	if (!gotPins) {
		err = "body requires pins object";
		return false;
	}
	return true;
}


// Load overlay under mutex, ensure part exists on board, mutate PartInfo, save YAML.
// Creates PartInfo entry only when the part is present on the board.
bool withPartOverlay(BoardRegistry &registry, const std::string &boardId, const std::string &part,
					 httplib::Response &res, bool requirePartOnBoard,
					 const std::function<bool(Annotations &, PartInfo &, httplib::Response &)> &fn) {
	const auto snap = registry.GetParsed(boardId);
	if (!snap) {
		setError(res, 404, "NOT_FOUND", "board not found");
		return false;
	}
	if (!snap->ok()) {
		setError(res, 400, "PARSE_FAILED", snap->error.empty() ? "parse failed" : snap->error);
		return false;
	}
	if (requirePartOnBoard && !obv::FindComponent(*snap->board, part)) {
		setError(res, 404, "PART_NOT_FOUND", "part not found");
		return false;
	}
	const auto boardPath = registry.BoardPath(boardId);
	if (boardPath.empty()) {
		setError(res, 404, "NOT_FOUND", "board not found");
		return false;
	}
	std::lock_guard<std::mutex> lock(registry.OverlayMutex(boardId));
	Annotations ann;
	std::string err;
	if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
		setError(res, 500, "OVERLAY_LOAD_FAILED", err.empty() ? "failed to load overlay" : err);
		ann.Close();
		return false;
	}
	auto &pi = ann.partInfos[part];
	pi.partName = part;
	if (!fn(ann, pi, res)) {
		ann.Close();
		return false;
	}
	if (!obv::SavePartNetYaml(boardPath, ann, err)) {
		setError(res, 500, "OVERLAY_SAVE_FAILED", err.empty() ? "failed to save overlay yaml" : err);
		ann.Close();
		return false;
	}
	ann.Close();
	return true;
}

// Read-only overlay access for GET condition routes.
bool withPartOverlayRead(BoardRegistry &registry, const std::string &boardId, const std::string &part,
						 httplib::Response &res,
						 const std::function<void(const Annotations &, const PartInfo *)> &fn) {
	const auto snap = registry.GetParsed(boardId);
	if (!snap) {
		setError(res, 404, "NOT_FOUND", "board not found");
		return false;
	}
	if (!snap->ok()) {
		setError(res, 400, "PARSE_FAILED", snap->error.empty() ? "parse failed" : snap->error);
		return false;
	}
	if (!obv::FindComponent(*snap->board, part)) {
		setError(res, 404, "PART_NOT_FOUND", "part not found");
		return false;
	}
	const auto boardPath = registry.BoardPath(boardId);
	if (boardPath.empty()) {
		setError(res, 404, "NOT_FOUND", "board not found");
		return false;
	}
	std::lock_guard<std::mutex> lock(registry.OverlayMutex(boardId));
	Annotations ann;
	std::string err;
	if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
		setError(res, 500, "OVERLAY_LOAD_FAILED", err.empty() ? "failed to load overlay" : err);
		ann.Close();
		return false;
	}
	const PartInfo *pi = nullptr;
	const auto it = ann.partInfos.find(part);
	if (it != ann.partInfos.end()) {
		pi = &it->second;
	}
	fn(ann, pi);
	ann.Close();
	return true;
}


#ifndef HAVE_SQLITE3
void setSqliteRequired(httplib::Response &res) {
	setError(res, 501, "SQLITE_REQUIRED",
			 "freeform annotations require ENABLE_SQLITE3 / HAVE_SQLITE3");
}
#endif

void mapChipStoreError(httplib::Response &res, const std::string &code, const std::string &msg) {
	if (code == "INVALID_PART_TYPE") {
		setError(res, 400, "INVALID_PART_TYPE", msg.empty() ? "invalid part_type" : msg);
	} else if (code == "CHIP_NOT_FOUND") {
		setError(res, 404, "CHIP_NOT_FOUND", msg.empty() ? "chip not found" : msg);
	} else if (code == "CHIP_PATH_COLLISION") {
		setError(res, 409, "CHIP_PATH_COLLISION", msg.empty() ? "chip path collision" : msg);
	} else if (code == "CONDITION_ID_CONFLICT") {
		setError(res, 409, "CONDITION_ID_CONFLICT",
				 msg.empty() ? "operating condition id already exists" : msg);
	} else if (code == "CHIP_STORE_FAILED") {
		setError(res, 500, "CHIP_STORE_FAILED", msg.empty() ? "chip store failed" : msg);
	} else {
		setError(res, 500, code.empty() ? "CHIP_STORE_FAILED" : code.c_str(),
				 msg.empty() ? "chip store failed" : msg);
	}
}

std::string chipRecordJson(const obv::ChipRecord &rec) {
	std::ostringstream os;
	os << "{\"part_type\":\"" << jsonEscape(rec.part_type) << "\",\"note\":\""
	   << jsonEscape(rec.note) << "\",\"operating_conditions\":"
	   << operatingConditionsArrayJson(rec.operating_conditions) << '}';
	return os.str();
}

std::string chipListJson(const std::vector<obv::ChipRecord> &chips) {
	std::ostringstream os;
	os << "{\"chips\":[";
	for (size_t i = 0; i < chips.size(); ++i) {
		const auto &c = chips[i];
		if (i) {
			os << ',';
		}
		os << "{\"part_type\":\"" << jsonEscape(c.part_type) << "\",\"conditionCount\":"
		   << c.operating_conditions.size();
		if (!c.note.empty()) {
			os << ",\"note\":\"" << jsonEscape(c.note) << '"';
		}
		os << '}';
	}
	os << "]}";
	return os.str();
}

std::string chipConditionsListJson(const std::string &partType,
								   const std::vector<OperatingCondition> &ocs) {
	std::ostringstream os;
	os << "{\"part_type\":\"" << jsonEscape(partType) << "\",\"operating_conditions\":"
	   << operatingConditionsArrayJson(ocs) << '}';
	return os.str();
}

// PUT chip body: { "note"?: string, "operating_conditions"?: Condition[] }
// hasConditions true only when operating_conditions key present.
bool parseChipPutBody(const std::string &json, std::string &note, bool &hasNote,
					  std::vector<OperatingCondition> &ocs, bool &hasConditions, std::string &err) {
	note.clear();
	hasNote = false;
	ocs.clear();
	hasConditions = false;
	MiniJson cur(json);
	if (!cur.expect('{')) {
		err = cur.err.empty() ? "invalid JSON object" : cur.err;
		return false;
	}
	cur.skipWs();
	if (cur.match('}')) {
		return true;
	}
	for (;;) {
		std::string key;
		if (!cur.parseString(key)) {
			err = cur.err;
			return false;
		}
		if (!cur.expect(':')) {
			err = cur.err;
			return false;
		}
		if (key == "note") {
			if (!cur.parseString(note)) {
				err = cur.err.empty() ? "note must be string" : cur.err;
				return false;
			}
			hasNote = true;
		} else if (key == "operating_conditions") {
			if (!cur.expect('[')) {
				err = cur.err.empty() ? "operating_conditions must be array" : cur.err;
				return false;
			}
			ocs.clear();
			cur.skipWs();
			if (!cur.match(']')) {
				for (;;) {
					OperatingCondition oc;
					if (!parseOperatingConditionObject(cur, oc)) {
						err = cur.err.empty() ? "invalid operating condition" : cur.err;
						return false;
					}
					ocs.push_back(std::move(oc));
					cur.skipWs();
					if (cur.match(']')) {
						break;
					}
					if (!cur.expect(',')) {
						err = cur.err;
						return false;
					}
				}
			}
			hasConditions = true;
		} else {
			if (!cur.skipValue()) {
				err = cur.err;
				return false;
			}
		}
		cur.skipWs();
		if (cur.match('}')) {
			break;
		}
		if (!cur.expect(',')) {
			err = cur.err;
			return false;
		}
	}
	cur.skipWs();
	if (cur.i < cur.s.size()) {
		err = "trailing data after chip JSON";
		return false;
	}
	return true;
}

bool normalizeConditionList(std::vector<OperatingCondition> &body, std::string &errCode,
							std::string &errMsg) {
	errCode.clear();
	errMsg.clear();
	if (body.size() > 256) {
		errCode = "BAD_REQUEST";
		errMsg = "operating_conditions limit is 256";
		return false;
	}
	std::vector<OperatingCondition> normalized;
	normalized.reserve(body.size());
	for (auto oc : body) {
		std::string nerr;
		if (!obv::NormalizeOperatingCondition(oc, nerr)) {
			errCode = "BAD_REQUEST";
			errMsg = nerr.empty() ? "invalid operating condition" : nerr;
			return false;
		}
		if (oc.id.empty()) {
			errCode = "BAD_REQUEST";
			errMsg = "operating condition id is required";
			return false;
		}
		for (const auto &prev : normalized) {
			if (prev.id == oc.id) {
				errCode = "CONDITION_ID_CONFLICT";
				errMsg = "duplicate operating condition id in body";
				return false;
			}
		}
		normalized.push_back(std::move(oc));
	}
	body = std::move(normalized);
	return true;
}


} // namespace


void RegisterBoardRoutes(httplib::Server &svr, BoardRegistry &registry) {
	svr.Get("/api/v1/config", [&registry](const httplib::Request &, httplib::Response &res) {
		const auto &cfg = registry.config();
		std::ostringstream os;
		os << "{\"boardRoot\":\"" << jsonEscape(cfg.boardRoot.string()) << "\",\"host\":\""
		   << jsonEscape(cfg.host) << "\",\"port\":" << cfg.port << '}';
		res.set_content(os.str(), "application/json");
	});

	svr.Get("/api/v1/boards", [&registry](const httplib::Request &, httplib::Response &res) {
		const auto list = registry.List();
		res.set_content(entryListJson(list), "application/json");
	});

	// Library mode: uploads disabled — boards come from boardRoot scan.
	svr.Post("/api/v1/boards",
			 [](const httplib::Request &, httplib::Response &res, const httplib::ContentReader &) {
				 setError(res, 405, "UPLOAD_DISABLED", "use boardRoot library");
			 });

	// More specific paths first.
	// Agent pin resolve + part summary (use :ref — id | displayPath | unique name).
	svr.Get(R"(/api/v1/boards/:ref/parts/:part/pins/:pin)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				const std::string pin = pathParam(req, "pin");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				const auto snap = registry.GetParsed(boardId);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
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

	// Part screenshot meta (more specific than plain screenshot).
	svr.Get(R"(/api/v1/boards/:ref/parts/:part/screenshot/meta)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				const auto snap = registry.GetParsed(boardId);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				if (!snap->ok()) {
					setError(res, 400, "PARSE_FAILED",
							 snap->error.empty() ? "parse failed" : snap->error);
					return;
				}
				obv::PartRenderOpts opts;
				std::string optErr;
				if (!parsePartRenderOpts(req, opts, optErr)) {
					setError(res, 400, "BAD_REQUEST", optErr);
					return;
				}
				const auto boardPath = registry.BoardPath(boardId);
				if (boardPath.empty()) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				std::lock_guard<std::mutex> lock(registry.OverlayMutex(boardId));
				Annotations ann;
				std::string err;
				if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					setError(res, 500, "OVERLAY_LOAD_FAILED",
							 err.empty() ? "failed to load overlay" : err);
					ann.Close();
					return;
				}
				obv::PartScreenshotResult result;
				std::string errCode, errMessage;
				if (!obv::RenderPartScreenshot(*snap->board, ann, part, opts, result, errCode,
											   errMessage)) {
					mapPartRenderError(res, errCode, errMessage);
					ann.Close();
					return;
				}
				const std::string js = obv::ExportPartScreenshotMetaJson(
					boardId, publicSourceName(registry, boardId), result.meta);
				ann.Close();
				res.set_content(js, "application/json");
			});

	// Part screenshot PNG.
	svr.Get(R"(/api/v1/boards/:ref/parts/:part/screenshot)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				const auto snap = registry.GetParsed(boardId);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				if (!snap->ok()) {
					setError(res, 400, "PARSE_FAILED",
							 snap->error.empty() ? "parse failed" : snap->error);
					return;
				}
				obv::PartRenderOpts opts;
				std::string optErr;
				if (!parsePartRenderOpts(req, opts, optErr)) {
					setError(res, 400, "BAD_REQUEST", optErr);
					return;
				}
				const auto boardPath = registry.BoardPath(boardId);
				if (boardPath.empty()) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				std::lock_guard<std::mutex> lock(registry.OverlayMutex(boardId));
				Annotations ann;
				std::string err;
				if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					setError(res, 500, "OVERLAY_LOAD_FAILED",
							 err.empty() ? "failed to load overlay" : err);
					ann.Close();
					return;
				}
				obv::PartScreenshotResult result;
				std::string errCode, errMessage;
				if (!obv::RenderPartScreenshot(*snap->board, ann, part, opts, result, errCode,
											   errMessage)) {
					mapPartRenderError(res, errCode, errMessage);
					ann.Close();
					return;
				}
				ann.Close();
				res.set_header("X-Image-Width", std::to_string(result.meta.transform.width));
				res.set_header("X-Image-Height", std::to_string(result.meta.transform.height));
				res.set_content(result.png, "image/png");
			});

	// Pin grid inference (row/col from board coordinates).
	svr.Get(R"(/api/v1/boards/:ref/parts/:part/pin-grid)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				const auto snap = registry.GetParsed(boardId);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				if (!snap->ok()) {
					setError(res, 400, "PARSE_FAILED",
							 snap->error.empty() ? "parse failed" : snap->error);
					return;
				}
				const auto boardPath = registry.BoardPath(boardId);
				if (boardPath.empty()) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				// Overlay optional for displayLabel; load under mutex like other agent GETs.
				std::lock_guard<std::mutex> lock(registry.OverlayMutex(boardId));
				Annotations ann;
				std::string err;
				if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					setError(res, 500, "OVERLAY_LOAD_FAILED",
							 err.empty() ? "failed to load overlay" : err);
					ann.Close();
					return;
				}
				obv::PinGridResult grid;
				std::string errCode;
				if (!obv::InferPinGrid(*snap->board, part, &ann, grid, errCode)) {
					if (errCode == "PART_NOT_FOUND") {
						setError(res, 404, "PART_NOT_FOUND", "part not found");
					} else if (errCode == "PART_NO_PINS") {
						setError(res, 400, "PART_NO_PINS", "part has no pins");
					} else {
						setError(res, 400, "BAD_REQUEST",
								 errCode.empty() ? "pin grid failed" : errCode);
					}
					ann.Close();
					return;
				}
				const std::string js =
					obv::ExportPinGridJson(boardId, publicSourceName(registry, boardId), grid);
				ann.Close();
				res.set_content(js, "application/json");
			});

	// Batch pin show_name overlay write (agent rename).
	svr.Patch(R"(/api/v1/boards/:ref/parts/:part/pins)",
			  [&registry](const httplib::Request &req, httplib::Response &res) {
				  const std::string ref = pathParam(req, "ref");
				  const std::string part = pathParam(req, "part");
				  std::string boardId;
				  if (!applyBoardRef(registry, ref, res, boardId)) {
					  return;
				  }
				  std::map<std::string, std::string> patch;
				  std::string perr;
				  if (!parsePinsShowNamePatch(req.body, patch, perr)) {
					  setError(res, 400, "BAD_REQUEST", perr);
					  return;
				  }

				  // Need Board inside withPartOverlay for FindPartPin / display labels.
				  const auto snap = registry.GetParsed(boardId);
				  if (!snap) {
					  setError(res, 404, "NOT_FOUND", "board not found");
					  return;
				  }
				  if (!snap->ok()) {
					  setError(res, 400, "PARSE_FAILED",
							   snap->error.empty() ? "parse failed" : snap->error);
					  return;
				  }
				  const Board &board = *snap->board;

				  struct UpdatedPin {
					  std::string key;
					  std::string show_name;
					  std::string displayLabel;
				  };
				  std::vector<UpdatedPin> updated;

				  if (!withPartOverlay(registry, boardId, part, res, true,
									   [&](Annotations &ann, PartInfo &pi, httplib::Response &r) {
										   std::vector<std::string> unknown;
										   std::vector<std::pair<std::string, const Pin *>> resolved;
										   resolved.reserve(patch.size());
										   for (const auto &kv : patch) {
											   const Pin *p = obv::FindPartPin(board, part, kv.first);
											   if (!p) {
												   if (unknown.size() < 8) {
													   unknown.push_back(kv.first);
												   }
												   continue;
											   }
											   resolved.emplace_back(kv.first, p);
										   }
										   if (!unknown.empty()) {
											   std::ostringstream msg;
											   msg << "unknown pin key(s): ";
											   for (size_t i = 0; i < unknown.size(); ++i) {
												   if (i) {
													   msg << ", ";
												   }
												   msg << unknown[i];
											   }
											   if (unknown.size() == 8) {
												   msg << ", ...";
											   }
											   setError(r, 400, "UNKNOWN_PIN_KEY", msg.str());
											   return false;
										   }

										   updated.reserve(resolved.size());
										   for (const auto &item : resolved) {
											   const std::string &reqKey = item.first;
											   const Pin *p = item.second;
											   const std::string canonicalKey = obv::PinOverlayKey(*p);
											   const std::string &showName = patch.at(reqKey);

											   if (showName.empty()) {
												   auto it = pi.pins.find(canonicalKey);
												   if (it != pi.pins.end()) {
													   it->second.show_name.clear();
													   if (!it->second) {
														   pi.pins.erase(it);
													   }
												   }
											   } else {
												   PinInfo &pinInfo = pi.pins[canonicalKey];
												   pinInfo.partName = part;
												   pinInfo.pinName = canonicalKey;
												   pinInfo.show_name = showName;
											   }

											   UpdatedPin u;
											   u.key = reqKey;
											   u.show_name = showName;
											   u.displayLabel = obv::PinDisplayLabel(*p, &ann);
											   updated.push_back(std::move(u));
										   }
										   return true;
									   })) {
					  return;
				  }

				  std::ostringstream os;
				  os << "{\"boardId\":\"" << jsonEscape(boardId) << "\",\"part\":\""
					 << jsonEscape(part) << "\",\"updated\":[";
				  for (size_t i = 0; i < updated.size(); ++i) {
					  if (i) {
						  os << ',';
					  }
					  const auto &u = updated[i];
					  os << "{\"key\":\"" << jsonEscape(u.key) << "\",\"show_name\":\""
						 << jsonEscape(u.show_name) << "\",\"displayLabel\":\""
						 << jsonEscape(u.displayLabel) << "\"}";
				  }
				  os << "]}";
				  res.set_content(os.str(), "application/json");
			  });


	svr.Get(R"(/api/v1/boards/:ref/parts/:part)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				const auto snap = registry.GetParsed(boardId);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
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
				if (!obv::FindComponent(*snap->board, part)) {
					setError(res, 404, "PART_NOT_FOUND", "part not found");
					ann.Close();
					return;
				}
				std::vector<OperatingCondition> chipOcs;
				{
					std::string partType;
					const auto it = ann.partInfos.find(part);
					if (it != ann.partInfos.end()) {
						partType = it->second.part_type;
					}
					if (!loadChipConditions(registry, partType, chipOcs)) {
						setError(res, 500, "CHIP_STORE_FAILED", "failed to load chip conditions");
						ann.Close();
						return;
					}
				}
				const std::string js = obv::ExportPartSummaryJson(
					*snap->board, ann, boardId, publicSourceName(registry, boardId), part,
					&chipOcs);
				ann.Close();
				if (js.empty()) {
					setError(res, 404, "PART_NOT_FOUND", "part not found");
					return;
				}
				res.set_content(js, "application/json");
			});

	// Operating-conditions CRUD (more specific :condId routes first).
	svr.Get(R"(/api/v1/boards/:ref/parts/:part/operating-conditions/:condId)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				const std::string condId = pathParam(req, "condId");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				if (condId.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing condition id");
					return;
				}
				withPartOverlayRead(registry, boardId, part, res,
									[&](const Annotations &, const PartInfo *pi) {
										const std::string partType = pi ? pi->part_type : std::string{};
										std::vector<OperatingCondition> chipOcs;
										if (!loadChipConditions(registry, partType, chipOcs)) {
											setError(res, 500, "CHIP_STORE_FAILED",
													 "failed to load chip conditions");
											return;
										}
										const std::vector<OperatingCondition> *boardOcs =
											pi ? &pi->operating_conditions : nullptr;
										const auto merged =
											obv::MergeOperatingConditions(boardOcs, &chipOcs);
										const OperatingCondition *found = nullptr;
										for (const auto &oc : merged.effective) {
											if (oc.id == condId) {
												found = &oc;
												break;
											}
										}
										if (!found) {
											setError(res, 404, "CONDITION_NOT_FOUND",
													 "operating condition not found");
											return;
										}
										res.set_content(operatingConditionObjectJson(*found),
														"application/json");
									});
			});

	svr.Put(R"(/api/v1/boards/:ref/parts/:part/operating-conditions/:condId)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				const std::string condId = pathParam(req, "condId");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				if (condId.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing condition id");
					return;
				}
				OperatingCondition body;
				std::string perr;
				if (!parseOperatingConditionBody(req.body, body, perr)) {
					setError(res, 400, "BAD_REQUEST", perr);
					return;
				}
				// Path id wins; ignore body id if present.
				body.id = condId;
				OperatingCondition saved;
				if (!withPartOverlay(registry, boardId, part, res, true,
									 [&](Annotations &, PartInfo &pi, httplib::Response &r) {
										 size_t idx = pi.operating_conditions.size();
										 for (size_t i = 0; i < pi.operating_conditions.size(); ++i) {
											 if (pi.operating_conditions[i].id == condId) {
												 idx = i;
												 break;
											 }
										 }
										 if (idx >= pi.operating_conditions.size()) {
											 setError(r, 404, "CONDITION_NOT_FOUND",
													  "operating condition not found");
											 return false;
										 }
										 std::string nerr;
										 if (!obv::NormalizeOperatingCondition(body, nerr)) {
											 setError(r, 400, "BAD_REQUEST",
													  nerr.empty() ? "invalid operating condition"
																   : nerr);
											 return false;
										 }
										 pi.operating_conditions[idx] = body;
										 saved = body;
										 return true;
									 })) {
					return;
				}
				res.set_content(operatingConditionObjectJson(saved), "application/json");
			});

	svr.Delete(R"(/api/v1/boards/:ref/parts/:part/operating-conditions/:condId)",
			   [&registry](const httplib::Request &req, httplib::Response &res) {
				   const std::string ref = pathParam(req, "ref");
				   const std::string part = pathParam(req, "part");
				   const std::string condId = pathParam(req, "condId");
				   std::string boardId;
				   if (!applyBoardRef(registry, ref, res, boardId)) {
					   return;
				   }
				   if (condId.empty()) {
					   setError(res, 400, "BAD_REQUEST", "missing condition id");
					   return;
				   }
				   if (!withPartOverlay(registry, boardId, part, res, true,
										[&](Annotations &, PartInfo &pi, httplib::Response &r) {
											const auto it = std::find_if(
												pi.operating_conditions.begin(),
												pi.operating_conditions.end(),
												[&](const OperatingCondition &oc) {
													return oc.id == condId;
												});
											if (it == pi.operating_conditions.end()) {
												setError(r, 404, "CONDITION_NOT_FOUND",
														 "operating condition not found");
												return false;
											}
											pi.operating_conditions.erase(it);
											return true;
										})) {
					   return;
				   }
				   res.status = 204;
				   res.set_content("", "application/json");
			   });

	svr.Get(R"(/api/v1/boards/:ref/parts/:part/operating-conditions)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				withPartOverlayRead(registry, boardId, part, res,
									[&](const Annotations &, const PartInfo *pi) {
										const std::string partType = pi ? pi->part_type : std::string{};
										std::vector<OperatingCondition> chipOcs;
										if (!loadChipConditions(registry, partType, chipOcs)) {
											setError(res, 500, "CHIP_STORE_FAILED",
													 "failed to load chip conditions");
											return;
										}
										const std::vector<OperatingCondition> *boardOcs =
											pi ? &pi->operating_conditions : nullptr;
										const auto merged =
											obv::MergeOperatingConditions(boardOcs, &chipOcs);
										res.set_content(
											operatingConditionsMergedListJson(boardId, part, partType,
																			  merged),
											"application/json");
									});
			});

	svr.Post(R"(/api/v1/boards/:ref/parts/:part/operating-conditions)",
			 [&registry](const httplib::Request &req, httplib::Response &res) {
				 const std::string ref = pathParam(req, "ref");
				 const std::string part = pathParam(req, "part");
				 std::string boardId;
				 if (!applyBoardRef(registry, ref, res, boardId)) {
					 return;
				 }
				 OperatingCondition body;
				 std::string perr;
				 if (!parseOperatingConditionBody(req.body, body, perr)) {
					 setError(res, 400, "BAD_REQUEST", perr);
					 return;
				 }
				 OperatingCondition created;
				 if (!withPartOverlay(registry, boardId, part, res, true,
									  [&](Annotations &, PartInfo &pi, httplib::Response &r) {
										  std::string nerr;
										  if (!obv::NormalizeOperatingCondition(body, nerr)) {
											  setError(r, 400, "BAD_REQUEST",
													   nerr.empty() ? "invalid operating condition"
																	: nerr);
											  return false;
										  }
										  if (body.id.empty()) {
											  body.id = obv::AllocateConditionId(pi);
										  } else {
											  for (const auto &x : pi.operating_conditions) {
												  if (x.id == body.id) {
													  setError(r, 409, "CONDITION_ID_CONFLICT",
															   "operating condition id already exists");
													  return false;
												  }
											  }
										  }
										  if (pi.operating_conditions.size() >= 256) {
											  setError(r, 400, "BAD_REQUEST",
													   "operating_conditions limit is 256");
											  return false;
										  }
										  pi.operating_conditions.push_back(body);
										  created = body;
										  return true;
									  })) {
					 return;
				 }
				 res.status = 201;
				 res.set_content(operatingConditionObjectJson(created), "application/json");
			 });

	svr.Put(R"(/api/v1/boards/:ref/parts/:part/operating-conditions)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string ref = pathParam(req, "ref");
				const std::string part = pathParam(req, "part");
				std::string boardId;
				if (!applyBoardRef(registry, ref, res, boardId)) {
					return;
				}
				std::vector<OperatingCondition> body;
				std::string perr;
				if (!parseOperatingConditionsReplaceBody(req.body, body, perr)) {
					setError(res, 400, "BAD_REQUEST", perr);
					return;
				}
				if (body.size() > 256) {
					setError(res, 400, "BAD_REQUEST", "operating_conditions limit is 256");
					return;
				}
				std::vector<OperatingCondition> saved;
				if (!withPartOverlay(registry, boardId, part, res, true,
									 [&](Annotations &, PartInfo &pi, httplib::Response &r) {
										 std::vector<OperatingCondition> normalized;
										 normalized.reserve(body.size());
										 for (auto oc : body) {
											 std::string nerr;
											 if (!obv::NormalizeOperatingCondition(oc, nerr)) {
												 setError(r, 400, "BAD_REQUEST",
														  nerr.empty()
															  ? "invalid operating condition"
															  : nerr);
												 return false;
											 }
											 if (oc.id.empty()) {
												 setError(r, 400, "BAD_REQUEST",
														  "operating condition id is required");
												 return false;
											 }
											 for (const auto &prev : normalized) {
												 if (prev.id == oc.id) {
													 setError(r, 409, "CONDITION_ID_CONFLICT",
															  "duplicate operating condition id in body");
													 return false;
												 }
											 }
											 normalized.push_back(std::move(oc));
										 }
										 // Replace conditions only; leave pins/part_type/angle.
										 pi.operating_conditions = std::move(normalized);
										 saved = pi.operating_conditions;
										 return true;
									 })) {
					return;
				}
				res.set_content(operatingConditionsListJson(boardId, part, saved),
								"application/json");
			});

	svr.Get("/api/v1/boards/:id/meta",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string id = pathParam(req, "id");
				if (!requireBoardId(id, res)) {
					return;
				}
				const auto snap = registry.GetParsed(id);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				if (!snap->ok()) {
					setError(res, 400, "PARSE_FAILED",
							 snap->error.empty() ? "parse failed" : snap->error);
					return;
				}
				const std::string meta = obv::ExportMetaJson(*snap, id);
				if (meta.empty()) {
					setError(res, 400, "PARSE_FAILED", "meta export failed");
					return;
				}
				res.set_content(meta, "application/json");
			});

	// GET overlays - board may be unparsed; only path presence required.
	svr.Get("/api/v1/boards/:id/overlays",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string id = pathParam(req, "id");
				if (!requireBoardId(id, res)) {
					return;
				}
				const auto boardPath = registry.BoardPath(id);
				if (boardPath.empty()) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				std::lock_guard<std::mutex> lock(registry.OverlayMutex(id));
				Annotations ann;
				std::string err;
				if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					setError(res, 500, "OVERLAY_LOAD_FAILED",
							 err.empty() ? "failed to load overlay" : err);
					ann.Close();
					return;
				}
				const std::string js = obv::ExportOverlayJson(ann);
				ann.Close();
				res.set_content(js, "application/json");
			});

	// PUT overlays - replace partInfos/netInfos (annotation array in body ignored).
	svr.Put("/api/v1/boards/:id/overlays",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string id = pathParam(req, "id");
				if (!requireBoardId(id, res)) {
					return;
				}
				const auto boardPath = registry.BoardPath(id);
				if (boardPath.empty()) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				std::lock_guard<std::mutex> lock(registry.OverlayMutex(id));
				Annotations ann;
				std::string err;
				if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					setError(res, 500, "OVERLAY_LOAD_FAILED",
							 err.empty() ? "failed to load overlay" : err);
					ann.Close();
					return;
				}
				if (!obv::ApplyOverlayJson(ann, req.body, err)) {
					setError(res, 400, "BAD_REQUEST", err.empty() ? "invalid overlay JSON" : err);
					ann.Close();
					return;
				}
				if (!obv::SavePartNetYaml(boardPath, ann, err)) {
					setError(res, 500, "OVERLAY_SAVE_FAILED",
							 err.empty() ? "failed to save overlay yaml" : err);
					ann.Close();
					return;
				}
				// Reload so export matches on-disk (yaml prune etc.).
				Annotations reloaded;
				if (!obv::LoadOverlayForBoard(boardPath, reloaded, err)) {
					setError(res, 500, "OVERLAY_LOAD_FAILED",
							 err.empty() ? "failed to reload overlay" : err);
					ann.Close();
					reloaded.Close();
					return;
				}
				const std::string js = obv::ExportOverlayJson(reloaded);
				ann.Close();
				reloaded.Close();
				res.set_content(js, "application/json");
			});

	// POST freeform annotation.
	svr.Post("/api/v1/boards/:id/annotations",
			 [&registry](const httplib::Request &req, httplib::Response &res) {
#ifndef HAVE_SQLITE3
				 (void)registry;
				 (void)req;
				 setSqliteRequired(res);
				 return;
#else
				 const std::string id = pathParam(req, "id");
				 if (!requireBoardId(id, res)) {
					 return;
				 }
				 const auto boardPath = registry.BoardPath(id);
				 if (boardPath.empty()) {
					 setError(res, 404, "NOT_FOUND", "board not found");
					 return;
				 }
				 NewAnnotationBody body;
				 std::string perr;
				 if (!parseNewAnnotation(req.body, body, perr)) {
					 setError(res, 400, "BAD_REQUEST", perr);
					 return;
				 }
				 std::lock_guard<std::mutex> lock(registry.OverlayMutex(id));
				 Annotations ann;
				 std::string err;
				 if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					 setError(res, 500, "OVERLAY_LOAD_FAILED",
							  err.empty() ? "failed to load overlay" : err);
					 ann.Close();
					 return;
				 }
				 if (ann.Add(body.side, body.x, body.y, body.net.c_str(), body.part.c_str(),
							 body.pin.c_str(), body.note.c_str()) != 0) {
					 setError(res, 500, "ANNOTATION_CREATE_FAILED", "annotation SQLite insert failed");
					 ann.Close();
					 return;
				 }
				 const int newId = static_cast<int>(sqlite3_last_insert_rowid(ann.sqldb));
				 ann.GenerateList();
				 const auto created = findAnnotation(ann, newId);
				 if (!created) {
					 setError(res, 500, "ANNOTATION_CREATE_FAILED", "annotation was not persisted");
					 ann.Close();
					 return;
				 }
				 const std::string js = annotationObjectJson(*created);
				 ann.Close();
				 res.status = 201;
				 res.set_content(js, "application/json");
#endif
			 });

	// PATCH annotation note.
	svr.Patch("/api/v1/boards/:id/annotations/:annId",
			  [&registry](const httplib::Request &req, httplib::Response &res) {
#ifndef HAVE_SQLITE3
				  (void)registry;
				  (void)req;
				  setSqliteRequired(res);
				  return;
#else
				  const std::string id = pathParam(req, "id");
				  const std::string annIdStr = pathParam(req, "annId");
				  if (!requireBoardId(id, res)) {
					  return;
				  }
				  if (annIdStr.empty()) {
					  setError(res, 400, "BAD_REQUEST", "missing annotation id");
					  return;
				  }
				  int annId = 0;
				  if (!parseAnnId(annIdStr, annId)) {
					  setError(res, 400, "BAD_REQUEST", "invalid annotation id");
					  return;
				  }
				  const auto boardPath = registry.BoardPath(id);
				  if (boardPath.empty()) {
					  setError(res, 404, "NOT_FOUND", "board not found");
					  return;
				  }
				  std::string note;
				  std::string perr;
				  if (!parseNotePatch(req.body, note, perr)) {
					  setError(res, 400, "BAD_REQUEST", perr);
					  return;
				  }
				  std::lock_guard<std::mutex> lock(registry.OverlayMutex(id));
				  Annotations ann;
				  std::string err;
				  if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					  setError(res, 500, "OVERLAY_LOAD_FAILED",
							   err.empty() ? "failed to load overlay" : err);
					  ann.Close();
					  return;
				  }
				  if (!findAnnotation(ann, annId)) {
					  setError(res, 404, "NOT_FOUND", "annotation not found");
					  ann.Close();
					  return;
				  }
				  if (ann.Update(annId, note.c_str()) != 0) {
					  setError(res, 500, "ANNOTATION_UPDATE_FAILED", "annotation SQLite update failed");
					  ann.Close();
					  return;
				  }
				  ann.GenerateList();
				  const auto updated = findAnnotation(ann, annId);
				  if (!updated) {
					  setError(res, 500, "ANNOTATION_UPDATE_FAILED", "annotation missing after update");
					  ann.Close();
					  return;
				  }
				  if (updated->note != note) {
					  setError(res, 500, "ANNOTATION_UPDATE_FAILED", "annotation note not updated");
					  ann.Close();
					  return;
				  }
				  const std::string js = annotationObjectJson(*updated);
				  ann.Close();
				  res.set_content(js, "application/json");
#endif
			  });

	// DELETE annotation - soft-delete (visible=0).
	svr.Delete("/api/v1/boards/:id/annotations/:annId",
			   [&registry](const httplib::Request &req, httplib::Response &res) {
#ifndef HAVE_SQLITE3
				   (void)registry;
				   (void)req;
				   setSqliteRequired(res);
				   return;
#else
				   const std::string id = pathParam(req, "id");
				   const std::string annIdStr = pathParam(req, "annId");
				   if (!requireBoardId(id, res)) {
					   return;
				   }
				   if (annIdStr.empty()) {
					   setError(res, 400, "BAD_REQUEST", "missing annotation id");
					   return;
				   }
				   int annId = 0;
				   if (!parseAnnId(annIdStr, annId)) {
					   setError(res, 400, "BAD_REQUEST", "invalid annotation id");
					   return;
				   }
				   const auto boardPath = registry.BoardPath(id);
				   if (boardPath.empty()) {
					   setError(res, 404, "NOT_FOUND", "board not found");
					   return;
				   }
				   std::lock_guard<std::mutex> lock(registry.OverlayMutex(id));
				   Annotations ann;
				   std::string err;
				   if (!obv::LoadOverlayForBoard(boardPath, ann, err)) {
					   setError(res, 500, "OVERLAY_LOAD_FAILED",
								err.empty() ? "failed to load overlay" : err);
					   ann.Close();
					   return;
				   }
				   if (!findAnnotation(ann, annId)) {
					   setError(res, 404, "NOT_FOUND", "annotation not found");
					   ann.Close();
					   return;
				   }
				   if (ann.Remove(annId) != 0) {
					   setError(res, 500, "ANNOTATION_DELETE_FAILED", "annotation SQLite soft-delete failed");
					   ann.Close();
					   return;
				   }
				   ann.GenerateList();
				   if (findAnnotation(ann, annId)) {
					   setError(res, 500, "ANNOTATION_DELETE_FAILED", "annotation still visible");
					   ann.Close();
					   return;
				   }
				   ann.Close();
				   res.status = 204;
				   res.set_content("", "application/json");
#endif
			   });

	svr.Get("/api/v1/boards/:id",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string id = pathParam(req, "id");
				if (!requireBoardId(id, res)) {
					return;
				}
				const auto snap = registry.GetParsed(id);
				if (!snap) {
					setError(res, 404, "NOT_FOUND", "board not found");
					return;
				}
				if (!snap->ok()) {
					setError(res, 400, "PARSE_FAILED",
							 snap->error.empty() ? "parse failed" : snap->error);
					return;
				}
				const std::string js = obv::ExportBoardJson(*snap, id);
				if (js.empty()) {
					setError(res, 400, "PARSE_FAILED", "board export failed");
					return;
				}
				res.set_content(js, "application/json");
			});

	svr.Delete("/api/v1/boards/:id",
			   [&registry](const httplib::Request &req, httplib::Response &res) {
				   if (!registry.allowDelete()) {
					   setError(res, 403, "FORBIDDEN", "delete disabled (allowDelete=false)");
					   return;
				   }
				   const std::string id = pathParam(req, "id");
				   if (!requireBoardId(id, res)) {
					   return;
				   }
				   if (registry.BoardPath(id).empty()) {
					   setError(res, 404, "NOT_FOUND", "board not found");
					   return;
				   }
				   if (!registry.Remove(id)) {
					   setError(res, 500, "DELETE_FAILED", "failed to delete board file");
					   return;
				   }
				   res.status = 204;
				   res.set_content("", "application/json");
			   });

	// --- Chip library REST (/api/v1/chips*) ---
	// More-specific :condId routes before collection routes.

	svr.Get(R"(/api/v1/chips/:partType/operating-conditions/:condId)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string partType = trimWs(pathParam(req, "partType"));
				const std::string condId = pathParam(req, "condId");
				if (partType.empty()) {
					setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					return;
				}
				if (condId.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing condition id");
					return;
				}
				obv::ChipRecord rec;
				std::string code, msg;
				if (!registry.chips().Get(partType, rec, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				const OperatingCondition *found = nullptr;
				for (const auto &oc : rec.operating_conditions) {
					if (oc.id == condId) {
						found = &oc;
						break;
					}
				}
				if (!found) {
					setError(res, 404, "CONDITION_NOT_FOUND", "operating condition not found");
					return;
				}
				res.set_content(operatingConditionObjectJson(*found), "application/json");
			});

	svr.Put(R"(/api/v1/chips/:partType/operating-conditions/:condId)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string partType = trimWs(pathParam(req, "partType"));
				const std::string condId = pathParam(req, "condId");
				if (partType.empty()) {
					setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					return;
				}
				if (condId.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing condition id");
					return;
				}
				OperatingCondition body;
				std::string perr;
				if (!parseOperatingConditionBody(req.body, body, perr)) {
					setError(res, 400, "BAD_REQUEST", perr);
					return;
				}
				body.id = condId;
				std::string nerr;
				if (!obv::NormalizeOperatingCondition(body, nerr)) {
					setError(res, 400, "BAD_REQUEST",
							 nerr.empty() ? "invalid operating condition" : nerr);
					return;
				}

				obv::ChipRecord rec;
				std::string code, msg;
				if (!registry.chips().Get(partType, rec, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				size_t idx = rec.operating_conditions.size();
				for (size_t i = 0; i < rec.operating_conditions.size(); ++i) {
					if (rec.operating_conditions[i].id == condId) {
						idx = i;
						break;
					}
				}
				if (idx >= rec.operating_conditions.size()) {
					setError(res, 404, "CONDITION_NOT_FOUND", "operating condition not found");
					return;
				}
				rec.operating_conditions[idx] = body;
				if (!registry.chips().Put(rec, true, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				res.set_content(operatingConditionObjectJson(body), "application/json");
			});

	svr.Delete(R"(/api/v1/chips/:partType/operating-conditions/:condId)",
			   [&registry](const httplib::Request &req, httplib::Response &res) {
				   const std::string partType = trimWs(pathParam(req, "partType"));
				   const std::string condId = pathParam(req, "condId");
				   if (partType.empty()) {
					   setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					   return;
				   }
				   if (condId.empty()) {
					   setError(res, 400, "BAD_REQUEST", "missing condition id");
					   return;
				   }
				   obv::ChipRecord rec;
				   std::string code, msg;
				   if (!registry.chips().Get(partType, rec, code, msg)) {
					   mapChipStoreError(res, code, msg);
					   return;
				   }
				   const auto it = std::find_if(
					   rec.operating_conditions.begin(), rec.operating_conditions.end(),
					   [&](const OperatingCondition &oc) { return oc.id == condId; });
				   if (it == rec.operating_conditions.end()) {
					   setError(res, 404, "CONDITION_NOT_FOUND", "operating condition not found");
					   return;
				   }
				   rec.operating_conditions.erase(it);
				   if (!registry.chips().Put(rec, true, code, msg)) {
					   mapChipStoreError(res, code, msg);
					   return;
				   }
				   res.status = 204;
				   res.set_content("", "application/json");
			   });

	svr.Get(R"(/api/v1/chips/:partType/operating-conditions)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string partType = trimWs(pathParam(req, "partType"));
				if (partType.empty()) {
					setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					return;
				}
				obv::ChipRecord rec;
				std::string code, msg;
				if (!registry.chips().Get(partType, rec, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				res.set_content(chipConditionsListJson(rec.part_type, rec.operating_conditions),
								"application/json");
			});

	svr.Post(R"(/api/v1/chips/:partType/operating-conditions)",
			 [&registry](const httplib::Request &req, httplib::Response &res) {
				 const std::string partType = trimWs(pathParam(req, "partType"));
				 if (partType.empty()) {
					 setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					 return;
				 }
				 OperatingCondition body;
				 std::string perr;
				 if (!parseOperatingConditionBody(req.body, body, perr)) {
					 setError(res, 400, "BAD_REQUEST", perr);
					 return;
				 }
				 std::string nerr;
				 if (!obv::NormalizeOperatingCondition(body, nerr)) {
					 setError(res, 400, "BAD_REQUEST",
							  nerr.empty() ? "invalid operating condition" : nerr);
					 return;
				 }

				 obv::ChipRecord rec;
				 std::string code, msg;
				 const bool exists = registry.chips().Get(partType, rec, code, msg);
				 if (!exists) {
					 if (code != "CHIP_NOT_FOUND") {
						 mapChipStoreError(res, code, msg);
						 return;
					 }
					 rec = obv::ChipRecord{};
					 rec.part_type = partType;
				 }
				 if (body.id.empty()) {
					 body.id = obv::AllocateConditionId(rec.operating_conditions);
				 } else {
					 for (const auto &x : rec.operating_conditions) {
						 if (x.id == body.id) {
							 setError(res, 409, "CONDITION_ID_CONFLICT",
									  "operating condition id already exists");
							 return;
						 }
					 }
				 }
				 if (rec.operating_conditions.size() >= 256) {
					 setError(res, 400, "BAD_REQUEST", "operating_conditions limit is 256");
					 return;
				 }
				 rec.operating_conditions.push_back(body);
				 if (!registry.chips().Put(rec, true, code, msg)) {
					 mapChipStoreError(res, code, msg);
					 return;
				 }
				 res.status = 201;
				 res.set_content(operatingConditionObjectJson(body), "application/json");
			 });

	svr.Put(R"(/api/v1/chips/:partType/operating-conditions)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string partType = trimWs(pathParam(req, "partType"));
				if (partType.empty()) {
					setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					return;
				}
				std::vector<OperatingCondition> body;
				std::string perr;
				if (!parseOperatingConditionsReplaceBody(req.body, body, perr)) {
					setError(res, 400, "BAD_REQUEST", perr);
					return;
				}
				std::string ncode, nmsg;
				if (!normalizeConditionList(body, ncode, nmsg)) {
					if (ncode == "CONDITION_ID_CONFLICT") {
						setError(res, 409, "CONDITION_ID_CONFLICT", nmsg);
					} else {
						setError(res, 400, "BAD_REQUEST", nmsg);
					}
					return;
				}
				std::string code, msg;
				if (!registry.chips().ReplaceConditions(partType, body, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				res.set_content(chipConditionsListJson(partType, body), "application/json");
			});

	svr.Get("/api/v1/chips", [&registry](const httplib::Request &, httplib::Response &res) {
		std::vector<obv::ChipRecord> chips;
		std::string code, msg;
		if (!registry.chips().List(chips, code, msg)) {
			mapChipStoreError(res, code, msg);
			return;
		}
		std::sort(chips.begin(), chips.end(),
				  [](const obv::ChipRecord &a, const obv::ChipRecord &b) {
					  return a.part_type < b.part_type;
				  });
		res.set_content(chipListJson(chips), "application/json");
	});

	svr.Get(R"(/api/v1/chips/:partType)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string partType = trimWs(pathParam(req, "partType"));
				if (partType.empty()) {
					setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					return;
				}
				obv::ChipRecord rec;
				std::string code, msg;
				if (!registry.chips().Get(partType, rec, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				res.set_content(chipRecordJson(rec), "application/json");
			});

	svr.Put(R"(/api/v1/chips/:partType)",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string partType = trimWs(pathParam(req, "partType"));
				if (partType.empty()) {
					setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					return;
				}
				std::string note;
				bool hasNote = false;
				std::vector<OperatingCondition> ocs;
				bool hasConditions = false;
				std::string perr;
				if (!req.body.empty()) {
					if (!parseChipPutBody(req.body, note, hasNote, ocs, hasConditions, perr)) {
						setError(res, 400, "BAD_REQUEST", perr);
						return;
					}
				}
				if (hasConditions) {
					std::string ncode, nmsg;
					if (!normalizeConditionList(ocs, ncode, nmsg)) {
						if (ncode == "CONDITION_ID_CONFLICT") {
							setError(res, 409, "CONDITION_ID_CONFLICT", nmsg);
						} else {
							setError(res, 400, "BAD_REQUEST", nmsg);
						}
						return;
					}
				}

				obv::ChipRecord rec;
				std::string code, msg;
				const bool exists = registry.chips().Get(partType, rec, code, msg);
				if (!exists) {
					if (code != "CHIP_NOT_FOUND") {
						mapChipStoreError(res, code, msg);
						return;
					}
					rec = obv::ChipRecord{};
					rec.part_type = partType;
				}
				if (hasNote) {
					rec.note = note;
				}
				if (hasConditions) {
					rec.operating_conditions = std::move(ocs);
				}
				// replaceConditions=true when body supplied conditions; else preserve existing.
				if (!registry.chips().Put(rec, hasConditions, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				// Re-read to return stored record (preserve path when conditions omitted).
				if (!registry.chips().Get(partType, rec, code, msg)) {
					mapChipStoreError(res, code, msg);
					return;
				}
				res.set_content(chipRecordJson(rec), "application/json");
			});

	svr.Delete(R"(/api/v1/chips/:partType)",
			   [&registry](const httplib::Request &req, httplib::Response &res) {
				   const std::string partType = trimWs(pathParam(req, "partType"));
				   if (partType.empty()) {
					   setError(res, 400, "INVALID_PART_TYPE", "missing part_type");
					   return;
				   }
				   std::string code, msg;
				   if (!registry.chips().Delete(partType, code, msg)) {
					   mapChipStoreError(res, code, msg);
					   return;
				   }
				   res.status = 204;
				   res.set_content("", "application/json");
			   });
}

} // namespace obv_server
