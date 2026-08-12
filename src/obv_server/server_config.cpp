#include "server_config.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace obv_server {
namespace {

std::string ReadFileText(const filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return {};
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string Trim(std::string s) {
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

std::string Unquote(std::string s) {
	s = Trim(std::move(s));
	if (s.size() >= 2) {
		const char a = s.front();
		const char b = s.back();
		if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) {
			return s.substr(1, s.size() - 2);
		}
	}
	return s;
}

// Extract a JSON string value for "key" (first match). Empty if missing.
std::string JsonString(const std::string &text, const char *key) {
	const std::string needle = std::string("\"") + key + "\"";
	size_t pos = text.find(needle);
	if (pos == std::string::npos) {
		return {};
	}
	pos = text.find(':', pos + needle.size());
	if (pos == std::string::npos) {
		return {};
	}
	++pos;
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
		++pos;
	}
	if (pos >= text.size() || text[pos] != '"') {
		return {};
	}
	++pos;
	std::string out;
	while (pos < text.size()) {
		const char c = text[pos++];
		if (c == '\\' && pos < text.size()) {
			out.push_back(text[pos++]);
			continue;
		}
		if (c == '"') {
			break;
		}
		out.push_back(c);
	}
	return out;
}

// Extract a JSON number (int/size) for "key". Returns defaultVal if missing.
long long JsonNumber(const std::string &text, const char *key, long long defaultVal) {
	const std::string needle = std::string("\"") + key + "\"";
	size_t pos = text.find(needle);
	if (pos == std::string::npos) {
		return defaultVal;
	}
	pos = text.find(':', pos + needle.size());
	if (pos == std::string::npos) {
		return defaultVal;
	}
	++pos;
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
		++pos;
	}
	if (pos >= text.size()) {
		return defaultVal;
	}
	char *end = nullptr;
	const long long v = std::strtoll(text.c_str() + pos, &end, 10);
	if (end == text.c_str() + pos) {
		return defaultVal;
	}
	return v;
}

bool JsonBool(const std::string &text, const char *key, bool defaultVal) {
	const std::string needle = std::string("\"") + key + "\"";
	size_t pos = text.find(needle);
	if (pos == std::string::npos) {
		return defaultVal;
	}
	pos = text.find(':', pos + needle.size());
	if (pos == std::string::npos) {
		return defaultVal;
	}
	++pos;
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
		++pos;
	}
	if (text.compare(pos, 4, "true") == 0) {
		return true;
	}
	if (text.compare(pos, 5, "false") == 0) {
		return false;
	}
	return defaultVal;
}

// Decode "0x.., 0x.." list into up to 44 u32 values (desktop Confparse style).
// Sets hasKey when at least one value decoded.
std::array<uint32_t, 44> DecodeKey44(const std::string &keytext, bool &hasKey) {
	std::array<uint32_t, 44> key{};
	hasKey = false;
	if (keytext.empty()) {
		return key;
	}
	const char *p = keytext.c_str();
	const char *limit = p + keytext.size();
	int ki = 0;
	while (p < limit && ki < 44) {
		while (p < limit && *p != '0') {
			++p;
		}
		if (p >= limit) {
			break;
		}
		char *ep = nullptr;
		const unsigned long long v = std::strtoull(p, &ep, 16);
		if (ep == p) {
			++p;
			continue;
		}
		key[static_cast<size_t>(ki)] = static_cast<uint32_t>(v);
		++ki;
		p = ep;
		hasKey = true;
	}
	return key;
}

void ApplyKeyStrings(ServerConfig &cfg, const std::string &fz, const std::string &cae, const std::string &xzz) {
	if (!fz.empty()) {
		bool has = false;
		cfg.keys.fzKey = DecodeKey44(fz, has);
		cfg.keys.hasFz = has;
	}
	if (!cae.empty()) {
		bool has = false;
		cfg.keys.caeKey = DecodeKey44(cae, has);
		cfg.keys.hasCae = has;
	}
	if (!xzz.empty()) {
		try {
			cfg.keys.xzzKey = std::stoull(Trim(xzz), nullptr, 0);
			cfg.keys.hasXzz = true;
		} catch (...) {
			// leave unset
		}
	}
}

void ApplyFromJson(ServerConfig &cfg, const std::string &text) {
	if (const auto host = JsonString(text, "host"); !host.empty()) {
		cfg.host = host;
	}
	const long long port = JsonNumber(text, "port", -1);
	if (port > 0 && port <= 65535) {
		cfg.port = static_cast<int>(port);
	}

	if (const auto boards = JsonString(text, "boardRoot"); !boards.empty()) {
		cfg.boardRoot = boards;
	} else if (const auto boards = JsonString(text, "boards"); !boards.empty()) {
		cfg.boardRoot = boards;
	}
	if (const auto www = JsonString(text, "webRoot"); !www.empty()) {
		cfg.webRoot = www;
	} else if (const auto www = JsonString(text, "www"); !www.empty()) {
		cfg.webRoot = www;
	}
	const long long maxUp = JsonNumber(text, "maxUploadBytes", -1);
	if (maxUp > 0) {
		cfg.maxUploadBytes = static_cast<size_t>(maxUp);
	}
	cfg.allowDelete = JsonBool(text, "allowDelete", cfg.allowDelete);

	// Keys may be top-level strings or nested under "keys".
	std::string fz = JsonString(text, "FZKey");
	std::string cae = JsonString(text, "CAEKey");
	std::string xzz = JsonString(text, "XZZPCBKey");
	if (fz.empty()) {
		fz = JsonString(text, "fzKey");
	}
	if (cae.empty()) {
		cae = JsonString(text, "caeKey");
	}
	if (xzz.empty()) {
		xzz = JsonString(text, "xzzKey");
	}
	ApplyKeyStrings(cfg, fz, cae, xzz);
}

// Simple key = value lines (TOML-ish / conf). Comments start with #.
void ApplyFromKeyValue(ServerConfig &cfg, const std::string &text) {
	std::istringstream in(text);
	std::string line;
	std::string fz, cae, xzz;
	while (std::getline(in, line)) {
		const auto hash = line.find('#');
		if (hash != std::string::npos) {
			line = line.substr(0, hash);
		}
		line = Trim(line);
		if (line.empty()) {
			continue;
		}
		const auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const std::string key = Trim(line.substr(0, eq));
		const std::string val = Unquote(line.substr(eq + 1));
		if (key == "host") {
			cfg.host = val;
		} else if (key == "port") {
			const int p = std::atoi(val.c_str());
			if (p > 0 && p <= 65535) {
				cfg.port = p;
			}

		} else if (key == "boardRoot" || key == "boards") {
			cfg.boardRoot = val;
		} else if (key == "webRoot" || key == "www") {
			cfg.webRoot = val;
		} else if (key == "maxUploadBytes") {
			const long long n = std::strtoll(val.c_str(), nullptr, 10);
			if (n > 0) {
				cfg.maxUploadBytes = static_cast<size_t>(n);
			}
		} else if (key == "allowDelete") {
			cfg.allowDelete = (val == "true" || val == "1" || val == "yes");
		} else if (key == "FZKey" || key == "fzKey") {
			fz = val;
		} else if (key == "CAEKey" || key == "caeKey") {
			cae = val;
		} else if (key == "XZZPCBKey" || key == "xzzKey") {
			xzz = val;
		}
	}
	ApplyKeyStrings(cfg, fz, cae, xzz);
}

bool LooksLikeJson(const std::string &text) {
	const std::string t = Trim(text);
	return !t.empty() && t.front() == '{';
}

} // namespace

ServerConfig LoadConfig(const filesystem::path &jsonOrTomlPath) {
	ServerConfig cfg;
	if (jsonOrTomlPath.empty()) {
		return cfg;
	}
	const std::string text = ReadFileText(jsonOrTomlPath);
	if (text.empty()) {
		std::cerr << "obv_server: config not readable or empty: " << jsonOrTomlPath.string() << "\n";
		return cfg;
	}
	if (LooksLikeJson(text)) {
		ApplyFromJson(cfg, text);
	} else {
		ApplyFromKeyValue(cfg, text);
	}
	return cfg;
}

ServerConfig ParseArgs(int argc, char **argv) {
	ServerConfig cfg;
	filesystem::path configPath;

	// First pass: locate --config so file defaults apply before CLI overrides.
	for (int i = 1; i < argc; ++i) {
		const char *a = argv[i];
		if (std::strcmp(a, "--config") == 0 && i + 1 < argc) {
			configPath = argv[++i];
		} else if (std::strncmp(a, "--config=", 9) == 0) {
			configPath = a + 9;
		}
	}
	if (!configPath.empty()) {
		cfg = LoadConfig(configPath);
	}

	for (int i = 1; i < argc; ++i) {
		const char *a = argv[i];
		auto needVal = [&](const char *flag) -> const char * {
			if (std::strcmp(a, flag) == 0 && i + 1 < argc) {
				return argv[++i];
			}
			const size_t n = std::strlen(flag);
			if (std::strncmp(a, flag, n) == 0 && a[n] == '=') {
				return a + n + 1;
			}
			return nullptr;
		};

		if (const char *v = needVal("--host")) {
			cfg.host = v;
		} else if (const char *v = needVal("--port")) {
			const int p = std::atoi(v);
			if (p > 0 && p <= 65535) {
				cfg.port = p;
			}

		} else if (const char *v = needVal("--boards")) {
			cfg.boardRoot = v;
		} else if (const char *v = needVal("--www")) {
			cfg.webRoot = v;
		} else if (const char *v = needVal("--config")) {
			(void)v; // already applied
		} else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
			std::cout
				<< "obv_server - OpenBoardView local HTTP server\n"
			<< "Usage: obv_server [--host 127.0.0.1] [--port 8080] [--boards DIR] [--www DIR] [--config PATH]\n"
			<< "  --host ADDR   Bind address (default 127.0.0.1 local-only; use 0.0.0.0 for LAN)\n"
			<< "  --port N      Listen port (default 8080)\n"
			<< "  --boards DIR  Library root: recursive board file scan; all data (overlays, chips) lives here (default BaiduSyncdisk/pcb on Windows)\n"
			<< "  --www DIR     Serve SPA/static files from DIR (e.g. web/dist); SPA fallback for non-API 404\n"
			<< "  --config P    JSON or key=value config (keys stay server-side; never exposed via API)\n"
			<< "Config keys: host, port, boardRoot/boards, webRoot/www, maxUploadBytes, allowDelete, FZKey/CAEKey/XZZPCBKey\n"
				<< "Security: default bind is loopback. Prefer a reverse proxy for TLS/gzip on LAN.\n"
				<< "Library mode: POST /api/v1/boards (upload) is disabled; open boards from boardRoot.\n";
			std::exit(0);
		}
	}


	if (cfg.boardRoot.empty()) {
		// User library default (Windows path; other platforms can override via --boards / config).
		cfg.boardRoot = R"(C:\Users\fesil\Documents\BaiduSyncdisk\pcb)";
	}
	return cfg;
}

} // namespace obv_server
