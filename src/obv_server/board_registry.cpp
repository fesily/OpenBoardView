#include "board_registry.h"

#include "sha256.h"

#include "obv_core/parse.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace obv_server {
namespace {

std::vector<char> toBuffer(const std::string &body) {
	return std::vector<char>(body.begin(), body.end());
}

std::vector<char> readAll(const filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return {};
	}
	in.seekg(0, std::ios::end);
	const auto n = in.tellg();
	if (n <= 0) {
		return {};
	}
	in.seekg(0, std::ios::beg);
	std::vector<char> buf(static_cast<size_t>(n));
	in.read(buf.data(), n);
	if (!in) {
		return {};
	}
	return buf;
}

bool writeAll(const filesystem::path &path, const std::string &body) {
	std::error_code ec;
	const auto parent = path.parent_path();
	if (!parent.empty()) {
		filesystem::create_directories(parent, ec);
	}
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}
	if (!body.empty()) {
		out.write(body.data(), static_cast<std::streamsize>(body.size()));
	}
	return static_cast<bool>(out);
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

// Best-effort move of overlay sidecars when the board file is renamed.
// Failures are non-fatal (same policy as board rename).
void renameOverlaySidecars(const filesystem::path &oldBoardPath,
						   const filesystem::path &newBoardPath) {
	if (oldBoardPath == newBoardPath) {
		return;
	}
	const filesystem::path oldYaml = overlayYamlPath(oldBoardPath);
	const filesystem::path newYaml = overlayYamlPath(newBoardPath);
	const filesystem::path oldSql = overlaySqlitePath(oldBoardPath);
	const filesystem::path newSql = overlaySqlitePath(newBoardPath);

	std::error_code ec;
	if (filesystem::exists(oldYaml, ec) && !ec && oldYaml != newYaml) {
		ec.clear();
		filesystem::rename(oldYaml, newYaml, ec);
	}
	ec.clear();
	if (filesystem::exists(oldSql, ec) && !ec && oldSql != newSql) {
		ec.clear();
		filesystem::rename(oldSql, newSql, ec);
	}
}

BoardRegistry::BoardRegistry(ServerConfig cfg)
	: cfg_(std::move(cfg)), boardsDir_(cfg_.dataRoot / "boards") {
	ensureBoardsDir();
}

void BoardRegistry::ensureBoardsDir() const {
	std::error_code ec;
	filesystem::create_directories(boardsDir_, ec);
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


std::string BoardRegistry::safeFileName(const std::string &originalName) {
	std::string base = originalName;
	// Strip directories.
	const auto slash = base.find_last_of("/\\");
	if (slash != std::string::npos) {
		base = base.substr(slash + 1);
	}
	if (base.empty()) {
		base = "upload.bin";
	}
	std::string out;
	out.reserve(base.size());
	for (unsigned char c : base) {
		if (std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '+') {
			out.push_back(static_cast<char>(c));
		} else if (c == ' ') {
			out.push_back('_');
		} else {
			out.push_back('_');
		}
	}
	// Avoid empty / hidden-only names.
	if (out.empty() || out == "." || out == "..") {
		out = "upload.bin";
	}
	// Cap length so path stays reasonable.
	if (out.size() > 120) {
		out.resize(120);
	}
	return out;
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

void BoardRegistry::scanDiskLocked() {
	ensureBoardsDir();
	std::error_code ec;
	if (!filesystem::exists(boardsDir_, ec)) {
		scanned_ = true;
		return;
	}
	for (const auto &ent : filesystem::directory_iterator(boardsDir_, ec)) {
		if (ec) {
			break;
		}
		if (!ent.is_regular_file(ec)) {
			continue;
		}
		const auto name = ent.path().filename().string();
		// Expected: <64hex>_<safeName>
		if (name.size() < 66 || name[64] != '_') {
			continue;
		}
		const std::string id = name.substr(0, 64);
		if (!isHexId(id)) {
			continue;
		}
		if (byId_.count(id)) {
			continue;
		}
		Entry e;
		e.id = id;
		e.path = ent.path();
		e.name = name.substr(65);
		e.ok = false;
		e.parseError = "not parsed yet";
		CacheSlot slot;
		slot.entry = std::move(e);
		slot.mtime = fileMtime(ent.path());
		byId_.emplace(id, std::move(slot));
	}
	scanned_ = true;
}

BoardRegistry::Entry BoardRegistry::parseAndStoreLocked(const std::string &id,
														const std::string &name,
														const filesystem::path &path,
														const std::string &bodyOrEmpty) {
	Entry e;
	e.id = id;
	e.path = path;
	e.name = name;

	std::vector<char> buf;
	if (!bodyOrEmpty.empty()) {
		buf = toBuffer(bodyOrEmpty);
	} else {
		buf = readAll(path);
	}

	obv::BoardSnapshot snap = obv::ParseBoardBuffer(std::move(buf), path, cfg_.keys);
	// Always expose original upload filename, not storage path (path still used for ASC relatives).
	snap.sourceName = name;
	e.ok = snap.ok();
	e.parseError = e.ok ? std::string() : (snap.error.empty() ? "parse failed" : snap.error);

	CacheSlot slot;
	slot.entry = e;
	slot.mtime = fileMtime(path);
	slot.snap = std::make_shared<const obv::BoardSnapshot>(std::move(snap));
	byId_[id] = std::move(slot);
	return e;
}

BoardRegistry::Entry BoardRegistry::ImportUpload(const std::string &originalName,
												 const std::string &body) {
	const std::string id = sha256Hex(body);
	const std::string name = safeFileName(originalName);
	const filesystem::path path = boardsDir_ / (id + "_" + name);

	std::lock_guard<std::mutex> lock(mu_);
	if (!scanned_) {
		scanDiskLocked();
	}

	// Already present with same content id - refresh display name to latest upload.
	auto it = byId_.find(id);
	if (it != byId_.end() && filesystem::exists(it->second.entry.path)) {
		filesystem::path storePath = it->second.entry.path;
		if (it->second.entry.name != name) {
			const filesystem::path newPath = boardsDir_ / (id + "_" + name);
			if (storePath != newPath) {
				std::error_code renEc;
				filesystem::rename(storePath, newPath, renEc);
				if (!renEc && filesystem::exists(newPath)) {
					renameOverlaySidecars(storePath, newPath);
					storePath = newPath;
				}
				// Rename failure is non-fatal: keep existing path, still update display name.
			}
		}
		// Always re-parse with current sanitized upload name (list/meta/sourceName).
		return parseAndStoreLocked(id, name, storePath, body);
	}

	if (!writeAll(path, body)) {
		Entry e;
		e.id = id;
		e.name = name;
		e.path = path;
		e.ok = false;
		e.parseError = "failed to write board file";
		return e;
	}
	return parseAndStoreLocked(id, name, path, body);
}

std::vector<BoardRegistry::Entry> BoardRegistry::List() const {
	std::lock_guard<std::mutex> lock(mu_);
	if (!scanned_) {
		const_cast<BoardRegistry *>(this)->scanDiskLocked();
	}
	// Ensure parse status for disk-scanned entries (lazy).
	std::vector<Entry> out;
	out.reserve(byId_.size());
	for (auto &kv : const_cast<BoardRegistry *>(this)->byId_) {
		CacheSlot &slot = kv.second;
		if (!slot.snap) {
			const_cast<BoardRegistry *>(this)->loadParsedLocked(kv.first);
		}
		out.push_back(slot.entry);
	}
	std::sort(out.begin(), out.end(),
			  [](const Entry &a, const Entry &b) { return a.name < b.name; });
	return out;
}

std::shared_ptr<const obv::BoardSnapshot>
BoardRegistry::loadParsedLocked(const std::string &id) {
	auto it = byId_.find(id);
	if (it == byId_.end()) {
		// Try discover by prefix on disk.
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

	auto buf = readAll(slot.entry.path);
	obv::BoardSnapshot snap =
		obv::ParseBoardBuffer(std::move(buf), slot.entry.path, cfg_.keys);
	// Prefer registry display name over full storage path.
	snap.sourceName = slot.entry.name;
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
	if (!scanned_) {
		scanDiskLocked();
	}
	return loadParsedLocked(id);
}

filesystem::path BoardRegistry::BoardPath(const std::string &id) const {
	if (!isHexId(id)) {
		return {};
	}
	std::lock_guard<std::mutex> lock(mu_);
	if (!scanned_) {
		const_cast<BoardRegistry *>(this)->scanDiskLocked();
	}
	const auto it = byId_.find(id);
	if (it == byId_.end()) {
		return {};
	}
	return it->second.entry.path;
}

bool BoardRegistry::Remove(const std::string &id) {
	if (!cfg_.allowDelete || !isHexId(id)) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mu_);
	if (!scanned_) {
		scanDiskLocked();
	}
	auto it = byId_.find(id);
	if (it == byId_.end()) {
		return false;
	}
	std::error_code ec;
	const bool deleted = filesystem::remove(it->second.entry.path, ec);
	// false + ec means real I/O failure; false + clear ec means already gone.
	if (!deleted && ec) {
		return false;
	}
	byId_.erase(it);
	return true;
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
