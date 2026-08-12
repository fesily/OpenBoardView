#pragma once

#include "server_config.h"

#include "obv_core/board_snapshot.h"
#include "obv_core/filesystem_impl.h"
#include "obv_core/chip_store.h"

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
		std::string displayPath; // relative path from boardRoot (preferred separators)
		std::string parseError;
		bool ok = false; // true when listed / parse ok; false after failed open/parse
	};

	enum class BoardRefStatus { Ok, NotFound, Ambiguous, InvalidId };

	struct BoardRefResult {
		BoardRefStatus status = BoardRefStatus::NotFound;
		std::string boardId; // set when Ok
		// Ambiguous: "id displayPath" lines for message only (no absolute paths)
		std::vector<std::string> candidates;
	};

	// Resolve boardId | unique displayPath | unique basename (see §3.2).
	BoardRefResult ResolveRef(const std::string &ref) const;

	// Lookup Entry by id after resolve; rescans when missing from cache.
	bool TryGetEntry(const std::string &id, Entry &out) const;

	// Re-scan boardRoot (clears index and rebuilds). List() always rescans.
	void Rescan();

	std::vector<Entry> List() const;

	// Returns nullptr when id unknown. Shared snapshot may be !ok() if parse failed.
	std::shared_ptr<const obv::BoardSnapshot> GetParsed(const std::string &id);

	filesystem::path BoardPath(const std::string &id) const;

	// true when removed; false when missing or delete disabled (check allowDelete separately).
	// In library mode, never deletes the real board file under boardRoot.
	bool Remove(const std::string &id);

	// Per-board mutex for overlay/annotation write serialization (last-write-wins).
	// Mutex lifetime is process-long; never destroyed while a lock may be held.
	std::mutex &OverlayMutex(const std::string &id);

	bool allowDelete() const { return cfg_.allowDelete; }
	size_t maxUploadBytes() const { return cfg_.maxUploadBytes; }
	// boardId = lowercase sha256 hex only ([0-9a-f]{64}); blocks path traversal.
	static bool IsValidBoardId(const std::string &id);

	const ServerConfig &config() const { return cfg_; }
	obv::ChipStore &chips();

	const filesystem::path &libraryDir() const { return libraryDir_; }
	filesystem::path OverlayPath(const std::string &id) const;



private:
	struct CacheSlot {
		Entry entry;
		std::shared_ptr<const obv::BoardSnapshot> snap;
		std::intmax_t mtime = 0;
	};

	void scanDiskLocked();
	std::shared_ptr<const obv::BoardSnapshot> loadParsedLocked(const std::string &id);
	static std::string sha256Hex(const std::string &body);
	static bool isHexId(const std::string &id);
	static std::intmax_t fileMtime(const filesystem::path &path);
	static bool isAllowedBoardExtension(const filesystem::path &path);
	static bool isSidecarOrConfig(const filesystem::path &path);
	static filesystem::path normalizeAbs(const filesystem::path &p);
	static std::string pathIdKey(const filesystem::path &absPath);
	static std::string relativeDisplayPath(const filesystem::path &root,
										   const filesystem::path &absPath);

	ServerConfig cfg_;
	filesystem::path libraryDir_;
	mutable std::mutex mu_;
	std::unordered_map<std::string, CacheSlot> byId_;


	mutable std::mutex overlayMapMu_;
	std::unordered_map<std::string, std::unique_ptr<std::mutex>> overlayMu_;
	obv::ChipStore chips_;
};

} // namespace obv_server
