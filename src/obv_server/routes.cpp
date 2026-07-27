#include "routes.h"

#include "obv_core/board_json.h"

#include <sstream>
#include <cstdio>
#include <string>

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

bool extractUpload(const httplib::Request &req, std::string &name, std::string &body,
				   std::string &err) {
	if (req.is_multipart_form_data()) {
		if (req.has_file("file")) {
			const auto file = req.get_file_value("file");
			body = file.content;
			name = file.filename.empty() ? "upload.bin" : file.filename;
			return true;
		}
		// Accept first file part if named differently.
		if (!req.files.empty()) {
			const auto &file = req.files.begin()->second;
			body = file.content;
			name = file.filename.empty() ? "upload.bin" : file.filename;
			return true;
		}
		err = "multipart upload missing file field";
		return false;
	}

	if (req.body.empty()) {
		err = "empty upload body";
		return false;
	}
	body = req.body;
	if (req.has_header("X-Filename")) {
		name = req.get_header_value("X-Filename");
	} else {
		name = "upload.bin";
	}
	return true;
}

} // namespace

void RegisterBoardRoutes(httplib::Server &svr, BoardRegistry &registry) {
	svr.Get("/api/v1/boards", [&registry](const httplib::Request &, httplib::Response &res) {
		const auto list = registry.List();
		res.set_content(entryListJson(list), "application/json");
	});

	svr.Post("/api/v1/boards", [&registry](const httplib::Request &req, httplib::Response &res) {
		std::string name;
		std::string body;
		std::string err;
		if (!extractUpload(req, name, body, err)) {
			setError(res, 400, "BAD_REQUEST", err);
			return;
		}
		if (body.size() > registry.maxUploadBytes()) {
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
