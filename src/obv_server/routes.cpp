#include "routes.h"

#include "obv_core/board_json.h"

#include <sstream>
#include <cstdio>
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

	// More specific path first.
	svr.Get("/api/v1/boards/:id/meta",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const auto it = req.path_params.find("id");
				if (it == req.path_params.end()) {
					setError(res, 400, "BAD_REQUEST", "missing board id");
					return;
				}
				const std::string &id = it->second;
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

	svr.Get("/api/v1/boards/:id",
			[&registry](const httplib::Request &req, httplib::Response &res) {
				const auto it = req.path_params.find("id");
				if (it == req.path_params.end()) {
					setError(res, 400, "BAD_REQUEST", "missing board id");
					return;
				}
				const std::string &id = it->second;
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
				   const auto it = req.path_params.find("id");
				   if (it == req.path_params.end()) {
					   setError(res, 400, "BAD_REQUEST", "missing board id");
					   return;
				   }
				   const std::string &id = it->second;
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
