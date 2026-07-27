#pragma once

#include "server_config.h"

#include "obv_core/board_snapshot.h"
#include "obv_core/filesystem_impl.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace obv_server {

class BoardRegistry {
public:
	explicit BoardRegistry(ServerConfig cfg);

	struct Entry {
		std::string id;
		filesystem::path path;
		std::string name;
		std::string parseError;
		bool ok = false;
	};

	// content-addressed id = sha256 hex of file bytes
	Entry ImportUpload(const std::string &originalName, const std::string &body);

	std::vector<Entry> List() const;

	// Returns nullptr when id unknown. Shared snapshot may be !ok() if parse failed.
	std::shared_ptr<const obv::BoardSnapshot> GetParsed(const std::string &id);

	filesystem::path BoardPath(const std::string &id) const;

	// true when removed; false when missing or delete disabled (check allowDelete separately).
	bool Remove(const std::string &id);

	bool allowDelete() const { return cfg_.allowDelete; }
	size_t maxUploadBytes() const { return cfg_.maxUploadBytes; }
	const ServerConfig &config() const { return cfg_; }

private:
	struct CacheSlot {
		Entry entry;
		std::shared_ptr<const obv::BoardSnapshot> snap;
		std::intmax_t mtime = 0;
	};

	void ensureBoardsDir() const;
	void scanDiskLocked();
	Entry parseAndStoreLocked(const std::string &id, const std::string &name,
							  const filesystem::path &path, const std::string &bodyOrEmpty);
	std::shared_ptr<const obv::BoardSnapshot> loadParsedLocked(const std::string &id);
	static std::string safeFileName(const std::string &originalName);
	static std::string sha256Hex(const std::string &body);
	static bool isHexId(const std::string &id);
	static std::intmax_t fileMtime(const filesystem::path &path);

	ServerConfig cfg_;
	filesystem::path boardsDir_;
	mutable std::mutex mu_;
	std::unordered_map<std::string, CacheSlot> byId_;
	bool scanned_ = false;
};

} // namespace obv_server
