#include "board_registry.h"
#include "routes.h"
#include "server_config.h"

#include "httplib.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kServerName = "obv_server";
// Core boardSchemaVersion / library API level exposed to clients (no secrets).
constexpr const char *kCoreVersion = "1";
constexpr const char *kServerVersion = "0.1.0";

} // namespace

int main(int argc, char **argv) {
	const obv_server::ServerConfig cfg = obv_server::ParseArgs(argc, argv);

	obv_server::BoardRegistry registry(cfg);

	httplib::Server svr;

	// Board upload limit; set early so all routes share it.
	svr.set_payload_max_length(cfg.maxUploadBytes);

	// Framework-level 413 (payload max) uses default HTML/text unless handled.
	svr.set_error_handler([](const httplib::Request &, httplib::Response &res) {
		if (res.status == 413) {
			res.set_content(
				R"({"error":{"code":"PAYLOAD_TOO_LARGE","message":"upload exceeds maxUploadBytes"}})",
				"application/json");
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

	// static later: svr.set_mount_point("/", webDist);

	std::cout << "listening " << cfg.host << ":" << cfg.port
			  << " dataRoot=" << cfg.dataRoot.string() << "\n";

	if (!svr.listen(cfg.host, cfg.port)) {
		std::cerr << "obv_server: failed to listen on " << cfg.host << ":" << cfg.port << "\n";
		return 1;
	}
	return 0;
}
