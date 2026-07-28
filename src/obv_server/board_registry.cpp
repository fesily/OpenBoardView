#include "board_registry.h"

#include "sha256.h"

#include "obv_core/core_utils.h"
#include "obv_core/parse.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace obv_server {
namespace {

std::vector<char> toBuffer(const std::string &body) {
	return std::vector<char>(body.begin(), body.end());
}

// Read whole file; on failure leave err with a human-readable reason (path + errno).
std::vector<char> readAll(const filesystem::path &path, std::string &err) {
	err.clear();
	// Prefer shared IO helper (regular-file check + binary read).
	std::string ioErr;
	std::vector<char> buf = obv::file_as_buffer(path, ioErr);
	if (!buf.empty()) {
		return buf;
	}
	// file_as_buffer leaves empty buffer for empty files too — distinguish.
	std::error_code ec;
	const bool exists = filesystem::exists(path, ec) && !ec;
	const bool regular = exists && filesystem::is_regular_file(path, ec) && !ec;
	if (!exists) {
		err = "file not found: " + path.string();
		return {};
	}
	if (!regular) {
		err = "not a regular file: " + path.string();
		return {};
	}
	// Empty board files are invalid for parse; keep a distinct message.
	const auto sz = filesystem::file_size(path, ec);
	if (!ec && sz == 0) {
		err = "empty board file: " + path.string();
		return {};
	}
	// Permission / IO failure (common when Docker UID cannot read host mounts).
	if (!ioErr.empty()) {
		err = ioErr + ": " + path.string();
	} else {
		err = std::string("failed to read: ") + path.string() + " (" +
			  (errno ? std::strerror(errno) : "unknown") + ")";
	}
	return {};
}

std::string toLowerAscii(std::string s) {
	for (char &c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

// Preferred-separator absolute path string used as the board id key material.
std::string preferredAbsString(filesystem::path p) {
	std::error_code ec;
	filesystem::path abs = filesystem::absolute(p, ec);
	if (ec) {
		abs = p;
	}
	abs = abs.lexically_normal();
	abs.make_preferred();
	return abs.string();
}

} // namespace

// Match Annotations::Load / SavePinInfos naming:
//   yaml   = boardPath.string() + ".yaml"
//   sqlite = last '.' replaced by '_' + ".sqlite3"
filesystem::path overlayYamlPath(const filesystem::path &boardPath) {
	return filesystem::path(boardPath.string() + ".yaml");
}

filesystem::path overlaySqlitePath(const filesystem::path &boardPath) {
	std::string sqlfn = boardPath.string();
	const auto pos = sqlfn.rfind('.');
	if (pos != std::string::npos) {
		sqlfn[pos] = '_';
	}
	sqlfn += ".sqlite3";
	return filesystem::path(sqlfn);
}

BoardRegistry::BoardRegistry(ServerConfig cfg) : cfg_(std::move(cfg)) {
	libraryDir_ = normalizeAbs(cfg_.boardRoot);
	cfg_.boardRoot = libraryDir_;
}

void BoardRegistry::Rescan() {
	std::lock_guard<std::mutex> lock(mu_);
	byId_.clear();
	scanDiskLocked();
}

std::string BoardRegistry::sha256Hex(const std::string &body) {
	char hex[65];
	sha256_hex(body.data(), body.size(), hex);
	return std::string(hex);
}

bool BoardRegistry::isHexId(const std::string &id) {
	if (id.size() != 64) {
		return false;
	}
	// Strict: lowercase hex only (sha256_hex output). Rejects path segments and A-F.
	for (unsigned char c : id) {
		const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool BoardRegistry::IsValidBoardId(const std::string &id) {
	return isHexId(id);
}

std::intmax_t BoardRegistry::fileMtime(const filesystem::path &path) {
	std::error_code ec;
	const auto ft = filesystem::last_write_time(path, ec);
	if (ec) {
		return 0;
	}
	// Portable integer form of file_time (not wall clock; only used for change detect).
	return static_cast<std::intmax_t>(ft.time_since_epoch().count());
}

filesystem::path BoardRegistry::normalizeAbs(const filesystem::path &p) {
	std::error_code ec;
	filesystem::path abs = filesystem::absolute(p, ec);
	if (ec) {
		abs = p;
	}
	abs = abs.lexically_normal();
	abs.make_preferred();
	return abs;
}

std::string BoardRegistry::pathIdKey(const filesystem::path &absPath) {
	// id = sha256 of absolute normalized path with preferred separators (UTF-8 path string).
	return sha256Hex(preferredAbsString(absPath));
}

std::string BoardRegistry::relativeDisplayPath(const filesystem::path &root,
											   const filesystem::path &absPath) {
	std::error_code ec;
	filesystem::path rel = filesystem::relative(absPath, root, ec);
	if (ec || rel.empty()) {
		return absPath.filename().string();
	}
	rel.make_preferred();
	return rel.string();
}

bool BoardRegistry::isSidecarOrConfig(const filesystem::path &path) {
	const std::string name = path.filename().string();
	const std::string lower = toLowerAscii(name);
	// Skip overlay sidecars and conf files that live next to boards.
	if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".yaml") == 0) {
		return true;
	}
	if (lower.size() >= 8 && lower.compare(lower.size() - 8, 8, ".sqlite3") == 0) {
		return true;
	}
	if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".conf") == 0) {
		return true;
	}
	return false;
}

bool BoardRegistry::isAllowedBoardExtension(const filesystem::path &path) {
	static const char *kAllowed[] = {".brd",  ".brd2", ".bdv", ".bvr", ".bvr3", ".fz",
									 ".cae",  ".bom",  ".asc", ".cst", ".json", ".cad",
									 ".pcb",  ".alg",  ".xzz", ".bin", ".txt",  ".gencad"};
	std::string ext = path.extension().string();
	if (ext.empty()) {
		// Bare names without extension are not listed in library mode (too noisy).
		return false;
	}
	ext = toLowerAscii(ext);
	for (const char *a : kAllowed) {
		if (ext == a) {
			return true;
		}
	}
	return false;
}

void BoardRegistry::scanDiskLocked() {
	// Full rescan index: caller may clear byId_ first (Rescan/List).
	std::error_code ec;
	if (libraryDir_.empty() || !filesystem::exists(libraryDir_, ec) ||
		!filesystem::is_directory(libraryDir_, ec)) {
		return;
	}

	// Preserve parse cache for files that still exist (same id + mtime).
	std::unordered_map<std::string, CacheSlot> previous = std::move(byId_);
	byId_.clear();

	const auto opts = filesystem::directory_options::skip_permission_denied;
	for (filesystem::recursive_directory_iterator it(libraryDir_, opts, ec), end;
		 !ec && it != end; it.increment(ec)) {
		if (ec) {
			// Skip unreadable subtrees when possible.
			if (it.depth() > 0) {
				it.disable_recursion_pending();
				ec.clear();
				continue;
			}
			break;
		}
		std::error_code fec;
		if (!it->is_regular_file(fec)) {
			continue;
		}
		const filesystem::path filePath = it->path();
		if (isSidecarOrConfig(filePath)) {
			continue;
		}
		if (!isAllowedBoardExtension(filePath)) {
			continue;
		}

		const filesystem::path abs = normalizeAbs(filePath);
		const std::string id = pathIdKey(abs);
		if (!isHexId(id)) {
			continue;
		}

		const std::intmax_t mt = fileMtime(abs);
		auto prev = previous.find(id);
		if (prev != previous.end() && prev->second.mtime == mt &&
			prev->second.entry.path == abs) {
			// Keep cached parse result; refresh display metadata.
			CacheSlot slot = std::move(prev->second);
			slot.entry.id = id;
			slot.entry.path = abs;
			slot.entry.name = abs.filename().string();
			slot.entry.displayPath = relativeDisplayPath(libraryDir_, abs);
			// If never opened, ok stays true with empty error (available).
			if (!slot.snap) {
				slot.entry.ok = true;
				slot.entry.parseError.clear();
			}
			byId_.emplace(id, std::move(slot));
			continue;
		}

		Entry e;
		e.id = id;
		e.path = abs;
		e.name = abs.filename().string();
		e.displayPath = relativeDisplayPath(libraryDir_, abs);
		e.ok = true; // present / available; parse status filled on GetParsed
		e.parseError.clear();
		CacheSlot slot;
		slot.entry = std::move(e);
		slot.mtime = mt;
		byId_.emplace(id, std::move(slot));
	}
}

std::vector<BoardRegistry::Entry> BoardRegistry::List() const {
	std::lock_guard<std::mutex> lock(mu_);
	// Always rescan: extension-only, no parse — safe for large libraries.
	const_cast<BoardRegistry *>(this)->scanDiskLocked();
	std::vector<Entry> out;
	out.reserve(byId_.size());
	for (const auto &kv : byId_) {
		out.push_back(kv.second.entry);
	}
	std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
		if (a.displayPath != b.displayPath) {
			return a.displayPath < b.displayPath;
		}
		return a.name < b.name;
	});
	return out;
}

std::shared_ptr<const obv::BoardSnapshot>
BoardRegistry::loadParsedLocked(const std::string &id) {
	auto it = byId_.find(id);
	if (it == byId_.end()) {
		// Discover latest library contents.
		scanDiskLocked();
		it = byId_.find(id);
		if (it == byId_.end()) {
			return nullptr;
		}
	}

	CacheSlot &slot = it->second;
	const std::intmax_t mt = fileMtime(slot.entry.path);
	if (slot.snap && slot.mtime == mt) {
		return slot.snap;
	}

	std::string readErr;
	auto buf = readAll(slot.entry.path, readErr);
	if (buf.empty()) {
		slot.entry.ok = false;
		slot.entry.parseError =
			readErr.empty() ? "failed to read board file" : readErr;
		slot.mtime = mt;
		slot.snap.reset();
		// Return a failed snapshot so callers can surface the error.
		obv::BoardSnapshot failed;
		failed.error = slot.entry.parseError;
		failed.sourceName =
			slot.entry.displayPath.empty() ? slot.entry.name : slot.entry.displayPath;
		slot.snap = std::make_shared<const obv::BoardSnapshot>(std::move(failed));
		return slot.snap;
	}

	obv::BoardSnapshot snap =
		obv::ParseBoardBuffer(std::move(buf), slot.entry.path, cfg_.keys);
	// Prefer relative library path for UI display.
	snap.sourceName =
		slot.entry.displayPath.empty() ? slot.entry.name : slot.entry.displayPath;
	slot.entry.ok = snap.ok();
	slot.entry.parseError =
		slot.entry.ok ? std::string() : (snap.error.empty() ? "parse failed" : snap.error);
	slot.mtime = mt;
	slot.snap = std::make_shared<const obv::BoardSnapshot>(std::move(snap));
	return slot.snap;
}

std::shared_ptr<const obv::BoardSnapshot> BoardRegistry::GetParsed(const std::string &id) {
	if (!isHexId(id)) {
		return nullptr;
	}
	std::lock_guard<std::mutex> lock(mu_);
	return loadParsedLocked(id);
}

filesystem::path BoardRegistry::BoardPath(const std::string &id) const {
	if (!isHexId(id)) {
		return {};
	}
	std::lock_guard<std::mutex> lock(mu_);
	auto it = byId_.find(id);
	if (it == byId_.end()) {
		const_cast<BoardRegistry *>(this)->scanDiskLocked();
		it = byId_.find(id);
		if (it == byId_.end()) {
			return {};
		}
	}
	return it->second.entry.path;
}

bool BoardRegistry::Remove(const std::string &id) {
	// Library mode: never delete real board files under boardRoot.
	(void)id;
	return false;
}

std::mutex &BoardRegistry::OverlayMutex(const std::string &id) {
	// Only allocate mutexes for well-formed ids (callers should validate first).
	static std::mutex invalidMu;
	if (!isHexId(id)) {
		return invalidMu;
	}
	std::lock_guard<std::mutex> lock(overlayMapMu_);
	auto it = overlayMu_.find(id);
	if (it == overlayMu_.end()) {
		it = overlayMu_.emplace(id, std::make_unique<std::mutex>()).first;
	}
	return *it->second;
}

} // namespace obv_server
