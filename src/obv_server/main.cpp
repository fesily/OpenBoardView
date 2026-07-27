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

	httplib::Server svr;

	// Future board upload limit (Task 6); set early so all routes share it.
	svr.set_payload_max_length(cfg.maxUploadBytes);

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

	// static later: svr.set_mount_point("/", webDist);

	std::cout << "listening " << cfg.host << ":" << cfg.port
			  << " dataRoot=" << cfg.dataRoot.string() << "\n";

	if (!svr.listen(cfg.host, cfg.port)) {
		std::cerr << "obv_server: failed to listen on " << cfg.host << ":" << cfg.port << "\n";
		return 1;
	}
	return 0;
}
