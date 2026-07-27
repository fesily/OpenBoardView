#pragma once

#include "obv_core/decrypt_keys.h"
#include "obv_core/filesystem_impl.h"

#include <cstddef>
#include <string>

namespace obv_server {

struct ServerConfig {
	std::string host = "127.0.0.1";
	int port = 8080;
	filesystem::path dataRoot;  // contains boards/, overlays/, config/ (runtime)
	filesystem::path boardRoot; // library directory scanned for board files
	filesystem::path webRoot;   // optional SPA/static root (web/dist); empty = API only
	obv::DecryptKeys keys;
	size_t maxUploadBytes = 64 * 1024 * 1024;
	bool allowDelete = false;
};

// Load JSON (preferred) or simple TOML-like key=value lines.
// Missing path or empty content returns defaults (no throw).
ServerConfig LoadConfig(const filesystem::path &jsonOrTomlPath);

// Apply CLI: --config PATH, --host HOST, --port N, --data PATH, --boards DIR, --www DIR.
// Later flags override earlier / config file values.
ServerConfig ParseArgs(int argc, char **argv);

} // namespace obv_server
