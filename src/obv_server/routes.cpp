#include "routes.h"

#include "obv_core/board_json.h"
#include "obv_core/overlay_store.h"

#include <cctype>
#include <cstdio>
#include <optional>
#include <sstream>
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
		   << jsonEscape(e.parseError) << "\"}";
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


#ifndef HAVE_SQLITE3
void setSqliteRequired(httplib::Response &res) {
	setError(res, 501, "SQLITE_REQUIRED",
			 "freeform annotations require ENABLE_SQLITE3 / HAVE_SQLITE3");
}
#endif

} // namespace

void RegisterBoardRoutes(httplib::Server &svr, BoardRegistry &registry) {
	svr.Get("/api/v1/boards", [&registry](const httplib::Request &, httplib::Response &res) {
		const auto list = registry.List();
		res.set_content(entryListJson(list), "application/json");
	});

	// ContentReader path: streams body without form-urlencoded preparse.
	svr.Post("/api/v1/boards",
			 [&registry](const httplib::Request &req, httplib::Response &res,
						 const httplib::ContentReader &reader) {
				 std::string name;
				 std::string body;
				 std::string err;
				 bool tooLarge = false;
				 const size_t maxBytes = registry.maxUploadBytes();

				 if (!readBoardUpload(req, reader, maxBytes, name, body, err, tooLarge)) {
					 if (tooLarge || res.status == 413) {
						 setError(res, 413, "PAYLOAD_TOO_LARGE", "upload exceeds maxUploadBytes");
					 } else if (res.status >= 400) {
						 // Framework already set a status (e.g. bad multipart); keep JSON envelope.
						 setError(res, res.status, "BAD_REQUEST", err);
					 } else {
						 setError(res, 400, "BAD_REQUEST", err);
					 }
					 return;
				 }
				 if (body.size() > maxBytes) {
					 setError(res, 413, "PAYLOAD_TOO_LARGE", "upload exceeds maxUploadBytes");
					 return;
				 }
				 const auto entry = registry.ImportUpload(name, body);
				 if (!entry.parseError.empty() && entry.parseError == "failed to write board file") {
					 setError(res, 500, "WRITE_FAILED", entry.parseError);
					 return;
				 }
				 const auto snap = registry.GetParsed(entry.id);
				 // Always 200 with ok/error in body so clients can store content-addressed id even on parse fail.
				 res.status = 200;
				 res.set_content(uploadResponseJson(entry, snap), "application/json");
			 });

	// More specific paths first.
	svr.Get("/api/v1/boards/:id/meta",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const std::string id = pathParam(req, "id");
				if (id.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing board id");
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
				if (id.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing board id");
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
				if (id.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing board id");
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
				 if (id.empty()) {
					 setError(res, 400, "BAD_REQUEST", "missing board id");
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
				  if (id.empty() || annIdStr.empty()) {
					  setError(res, 400, "BAD_REQUEST", "missing board id or annotation id");
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
				   if (id.empty() || annIdStr.empty()) {
					   setError(res, 400, "BAD_REQUEST", "missing board id or annotation id");
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
				if (id.empty()) {
					setError(res, 400, "BAD_REQUEST", "missing board id");
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
				   if (id.empty()) {
					   setError(res, 400, "BAD_REQUEST", "missing board id");
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
}

} // namespace obv_server
