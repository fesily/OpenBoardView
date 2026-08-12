#include "board_registry.h"
#include "routes.h"
#include "server_config.h"

#include "httplib.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr const char *kServerName = "obv_server";
// Core boardSchemaVersion / library API level exposed to clients (no secrets).
constexpr const char *kCoreVersion = "1";
constexpr const char *kServerVersion = "0.1.0";

bool PathStartsWith(const std::string &path, const char *prefix) {
	const size_t n = std::char_traits<char>::length(prefix);
	return path.size() >= n && path.compare(0, n, prefix) == 0;
}

// Read a small text file into out; returns false if unreadable.
bool ReadFileToString(const filesystem::path &path, std::string &out) {
	std::ifstream in(path.string(), std::ios::in | std::ios::binary);
	if (!in) {
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

} // namespace

int main(int argc, char **argv) {
	const obv_server::ServerConfig cfg = obv_server::ParseArgs(argc, argv);

	obv_server::BoardRegistry registry(cfg);

	httplib::Server svr;

	// Board upload limit; set early so all routes share it.
	svr.set_payload_max_length(cfg.maxUploadBytes);

	const filesystem::path webRoot = cfg.webRoot;
	const filesystem::path spaIndex = webRoot / "index.html";

	// Framework-level 413 (payload max) uses default HTML/text unless handled.
	// SPA fallback: non-API 404 → index.html when --www is set.
	svr.set_error_handler([webRoot, spaIndex](const httplib::Request &req, httplib::Response &res) {
		if (res.status == 413) {
			res.set_content(
				R"({"error":{"code":"PAYLOAD_TOO_LARGE","message":"upload exceeds maxUploadBytes"}})",
				"application/json");
			return;
		}
		if (res.status == 404 && !webRoot.empty() && !PathStartsWith(req.path, "/api/")) {
			std::string html;
			if (ReadFileToString(spaIndex, html)) {
				res.status = 200;
				res.set_content(html, "text/html; charset=utf-8");
			}
		}
	});

	svr.Get("/api/v1/health", [](const httplib::Request &, httplib::Response &res) {
		res.set_content(R"({"status":"ok"})", "application/json");
	});

	svr.Get("/api/v1/version", [](const httplib::Request &, httplib::Response &res) {
		// Never include decrypt keys or data paths in this response.
		const std::string body =
			std::string("{\"server\":\"") + kServerName + "\",\"serverVersion\":\"" + kServerVersion +
			"\",\"core\":\"" + kCoreVersion + "\"}";
		res.set_content(body, "application/json");
	});

	obv_server::RegisterBoardRoutes(svr, registry);

	if (!webRoot.empty()) {
		const std::string www = webRoot.string();
		if (!svr.set_mount_point("/", www)) {
			std::cerr << "obv_server: failed to mount web root (not a directory?): " << www << "\n";
			return 1;
		}
		std::cout << "serving static " << www << "\n";
	}

	std::cout << "listening " << cfg.host << ":" << cfg.port
			  << " boardRoot=" << cfg.boardRoot.string();
	if (!webRoot.empty()) {
		std::cout << " webRoot=" << webRoot.string();
	}
	std::cout << "\n";

	if (!svr.listen(cfg.host, cfg.port)) {
		std::cerr << "obv_server: failed to listen on " << cfg.host << ":" << cfg.port << "\n";
		return 1;
	}
	return 0;
}
