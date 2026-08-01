// Chip library store: per-part_type YAML under a root directory.
// Atomic-ish write: path + ".tmp" then rename; Windows fallback writes in place.

#include "obv_core/chip_store.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace obv {
namespace {

std::string trimCopy(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
	return s.substr(b, e - b);
}

bool isKeptFilenameChar(unsigned char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
	       c == '.' || c == '_' || c == '+' || c == '-';
}

void appendEscapedYaml(std::ostringstream &os, const std::string &s) {
	os << '"';
	for (unsigned char c : s) {
		switch (c) {
			case '"': os << "\\\""; break;
			case '\\': os << "\\\\"; break;
			case '\n': os << "\\n"; break;
			case '\r': os << "\\r"; break;
			case '\t': os << "\\t"; break;
			default:
				if (c < 0x20) {
					// Drop control chars rather than emit invalid YAML.
					break;
				}
				os << static_cast<char>(c);
				break;
		}
	}
	os << '"';
}

void appendStringArray(std::ostringstream &os, const std::vector<std::string> &arr) {
	os << '[';
	for (size_t i = 0; i < arr.size(); ++i) {
		if (i) os << ", ";
		appendEscapedYaml(os, arr[i]);
	}
	os << ']';
}

std::string emitChipYaml(const ChipRecord &rec) {
	std::ostringstream os;
	os << "part_type: ";
	appendEscapedYaml(os, rec.part_type);
	os << "\nnote: ";
	appendEscapedYaml(os, rec.note);
	os << "\noperating_conditions:\n";
	if (rec.operating_conditions.empty()) {
		os << "  []\n";
		return os.str();
	}
	for (const auto &oc : rec.operating_conditions) {
		os << "  - id: ";
		appendEscapedYaml(os, oc.id);
		os << "\n    name: ";
		appendEscapedYaml(os, oc.name);
		os << "\n    inputs: ";
		appendStringArray(os, oc.inputs);
		os << "\n    outputs: ";
		appendStringArray(os, oc.outputs);
		os << "\n    enables: ";
		appendStringArray(os, oc.enables);
		os << "\n    note: ";
		appendEscapedYaml(os, oc.note);
		os << "\n";
	}
	return os.str();
}

// Minimal YAML reader for ChipRecord files (known keys only).
struct YamlParser {
	std::string src;
	size_t i = 0;

	explicit YamlParser(std::string s) : src(std::move(s)) {}

	void skipWs(bool newlines = true) {
		while (i < src.size()) {
			const char c = src[i];
			if (c == ' ' || c == '\t' || c == '\r' || (newlines && c == '\n')) {
				++i;
				continue;
			}
			if (c == '#') {
				while (i < src.size() && src[i] != '\n') ++i;
				continue;
			}
			break;
		}
	}

	bool peek(char c) const { return i < src.size() && src[i] == c; }
	bool consume(char c) {
		if (!peek(c)) return false;
		++i;
		return true;
	}

	std::string readIdent() {
		size_t b = i;
		while (i < src.size()) {
			const unsigned char c = static_cast<unsigned char>(src[i]);
			if (std::isalnum(c) || c == '_' || c == '-') {
				++i;
				continue;
			}
			break;
		}
		return src.substr(b, i - b);
	}

	bool parseString(std::string &out) {
		skipWs(false);
		if (i >= src.size()) return false;
		if (src[i] == '"' || src[i] == '\'') {
			const char q = src[i++];
			std::string s;
			while (i < src.size() && src[i] != q) {
				if (src[i] == '\\' && i + 1 < src.size()) {
					++i;
					const char e = src[i++];
					switch (e) {
						case 'n': s.push_back('\n'); break;
						case 'r': s.push_back('\r'); break;
						case 't': s.push_back('\t'); break;
						case '"': s.push_back('"'); break;
						case '\'': s.push_back('\''); break;
						case '\\': s.push_back('\\'); break;
						default: s.push_back(e); break;
					}
				} else {
					s.push_back(src[i++]);
				}
			}
			if (i >= src.size() || src[i] != q) return false;
			++i;
			out = std::move(s);
			return true;
		}
		// Unquoted scalar until comment/newline/comma/]/]
		size_t b = i;
		while (i < src.size()) {
			const char c = src[i];
			if (c == '\n' || c == '\r' || c == '#' || c == ',' || c == ']') break;
			++i;
		}
		out = trimCopy(src.substr(b, i - b));
		return true;
	}

	bool parseStringArray(std::vector<std::string> &out) {
		out.clear();
		skipWs(false);
		if (!consume('[')) return false;
		skipWs();
		if (consume(']')) return true;
		for (;;) {
			std::string item;
			if (!parseString(item)) return false;
			out.push_back(std::move(item));
			skipWs();
			if (consume(']')) return true;
			if (!consume(',')) return false;
			skipWs();
		}
	}


	bool parse(ChipRecord &out) {
		out = ChipRecord{};
		skipWs();
		while (i < src.size()) {
			skipWs();
			if (i >= src.size()) break;

			// list item under operating_conditions handled inside that branch
			const std::string key = readIdent();
			if (key.empty()) {
				// maybe a list dash without key context — skip line
				while (i < src.size() && src[i] != '\n') ++i;
				if (peek('\n')) ++i;
				continue;
			}
			skipWs(false);
			if (!consume(':')) return false;
			skipWs(false);

			if (key == "part_type") {
				if (!parseString(out.part_type)) return false;
			} else if (key == "note") {
				if (!parseString(out.note)) return false;
			} else if (key == "operating_conditions") {
				skipWs(false);
				if (consume('[')) {
					// flow style empty or list — rare
					skipWs();
					if (!consume(']')) {
						// unsupported multi flow; fail
						return false;
					}
				} else {
					// block sequence
					for (;;) {
						// advance to next non-empty line
						while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r')) ++i;
						if (i < src.size() && src[i] == '\n') {
							++i;
							continue;
						}
						if (i >= src.size()) break;
						if (src[i] == '#') {
							while (i < src.size() && src[i] != '\n') ++i;
							continue;
						}
						// empty flow []
						if (src[i] == '[') {
							skipWs(false);
							if (!consume('[')) return false;
							skipWs();
							if (!consume(']')) return false;
							break;
						}
						// indent then '-'
						size_t indent = 0;
						while (i + indent < src.size() &&
						       (src[i + indent] == ' ' || src[i + indent] == '\t'))
							++indent;
						const size_t content = i + indent;
						if (content >= src.size()) break;
						if (src[content] != '-') {
							// next top-level key
							i = content;
							break;
						}
						i = content + 1;
						skipWs(false);
						OperatingCondition oc;
						// optional inline first key after "- "
						if (i < src.size() && src[i] != '\n') {
							// first field may be on same line: "- id: x"
							const std::string fk = readIdent();
							if (!fk.empty()) {
								skipWs(false);
								if (!consume(':')) return false;
								skipWs(false);
								if (fk == "id") {
									if (!parseString(oc.id)) return false;
								} else if (fk == "name") {
									if (!parseString(oc.name)) return false;
								} else if (fk == "note") {
									if (!parseString(oc.note)) return false;
								} else if (fk == "inputs") {
									if (!parseStringArray(oc.inputs)) return false;
								} else if (fk == "outputs") {
									if (!parseStringArray(oc.outputs)) return false;
								} else if (fk == "enables") {
									if (!parseStringArray(oc.enables)) return false;
								} else {
									std::string dump;
									if (!parseString(dump)) return false;
								}
								while (i < src.size() && src[i] != '\n') ++i;
							}
						}
						// remaining fields of this condition
						for (;;) {
							if (i < src.size() && src[i] == '\n') ++i;
							size_t line = i;
							size_t ind = 0;
							while (line + ind < src.size() &&
							       (src[line + ind] == ' ' || src[line + ind] == '\t'))
								++ind;
							const size_t cpos = line + ind;
							if (cpos >= src.size()) {
								i = cpos;
								break;
							}
							if (src[cpos] == '\n') {
								i = cpos;
								continue;
							}
							if (src[cpos] == '#') {
								i = cpos;
								while (i < src.size() && src[i] != '\n') ++i;
								continue;
							}
							if (src[cpos] == '-' || ind == 0) {
								i = line;
								break;
							}
							i = cpos;
							const std::string fk = readIdent();
							if (fk.empty()) return false;
							skipWs(false);
							if (!consume(':')) return false;
							skipWs(false);
							if (fk == "id") {
								if (!parseString(oc.id)) return false;
							} else if (fk == "name") {
								if (!parseString(oc.name)) return false;
							} else if (fk == "note") {
								if (!parseString(oc.note)) return false;
							} else if (fk == "inputs") {
								if (!parseStringArray(oc.inputs)) return false;
							} else if (fk == "outputs") {
								if (!parseStringArray(oc.outputs)) return false;
							} else if (fk == "enables") {
								if (!parseStringArray(oc.enables)) return false;
							} else {
								if (peek('[')) {
									std::vector<std::string> dump;
									if (!parseStringArray(dump)) return false;
								} else {
									std::string dump;
									if (!parseString(dump)) return false;
								}
							}
							while (i < src.size() && src[i] != '\n') ++i;
						}
						out.operating_conditions.push_back(std::move(oc));
					}
					continue; // already positioned for next key
				}
			} else {
				// unknown top-level: skip value line / empty
				if (peek('[')) {
					std::vector<std::string> dump;
					if (!parseStringArray(dump)) return false;
				} else if (i < src.size() && src[i] != '\n') {
					std::string dump;
					if (!parseString(dump)) return false;
				}
			}
			while (i < src.size() && src[i] != '\n') ++i;
			if (peek('\n')) ++i;
		}
		return true;
	}
};

filesystem::path chipPathForStem(const filesystem::path &root, const std::string &stem) {
	return root / (stem + ".yaml");
}

bool writeFileAtomic(const filesystem::path &path, const std::string &content, std::string &err) {
	err.clear();
	const filesystem::path tmp = filesystem::path(path.string() + ".tmp");
	{
		std::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
		if (!out.good()) {
			err = "failed to open temp file for write";
			return false;
		}
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
		if (!out.good()) {
			err = "failed to write temp file";
			return false;
		}
	}
	std::error_code ec;
	filesystem::rename(tmp, path, ec);
	if (!ec) return true;

	// Windows: if target exists, rename may fail — write in place then verify.
	{
		std::ofstream out(path.string(), std::ios::binary | std::ios::trunc);
		if (!out.good()) {
			err = "failed to open target for write";
			filesystem::remove(tmp, ec);
			return false;
		}
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
		if (!out.good()) {
			err = "failed to write target file";
			filesystem::remove(tmp, ec);
			return false;
		}
	}
	filesystem::remove(tmp, ec);

	// Reload-verify
	std::ifstream in(path.string(), std::ios::binary);
	if (!in.good()) {
		err = "failed to verify written file";
		return false;
	}
	std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (got != content) {
		err = "write verify mismatch";
		return false;
	}
	return true;
}

bool readFileText(const filesystem::path &path, std::string &out, std::string &err) {
	out.clear();
	err.clear();
	std::ifstream in(path.string(), std::ios::binary);
	if (!in.good()) {
		err = "failed to open file";
		return false;
	}
	out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return true;
}

} // namespace

bool SanitizePartTypeFilename(const std::string &partType, std::string &outFileStem, std::string &err) {
	outFileStem.clear();
	err.clear();
	const std::string trimmed = trimCopy(partType);
	if (trimmed.empty()) {
		err = "part_type is empty";
		return false;
	}
	outFileStem.reserve(trimmed.size());
	for (unsigned char c : trimmed) {
		outFileStem.push_back(isKeptFilenameChar(c) ? static_cast<char>(c) : '_');
	}
	if (outFileStem.empty() || outFileStem == "." || outFileStem == "..") {
		err = "part_type sanitizes to invalid filename";
		outFileStem.clear();
		return false;
	}
	return true;
}

MergedConditions MergeOperatingConditions(
	const std::vector<OperatingCondition> *boardOrNull,
	const std::vector<OperatingCondition> *chipOrNull) {
	MergedConditions m;
	if (boardOrNull) m.board = *boardOrNull;
	if (chipOrNull) m.chip = *chipOrNull;

	if (!m.board.empty()) {
		m.source = ConditionSource::Board;
		m.effective = m.board;
	} else if (!m.chip.empty()) {
		m.source = ConditionSource::Chip;
		m.effective = m.chip;
	} else {
		m.source = ConditionSource::None;
	}
	return m;
}

bool LoadChipRecordFile(const filesystem::path &path, ChipRecord &out, std::string &err) {
	out = ChipRecord{};
	std::string text;
	if (!readFileText(path, text, err)) return false;
	YamlParser p(std::move(text));
	if (!p.parse(out)) {
		err = "failed to parse chip yaml";
		out = ChipRecord{};
		return false;
	}
	out.part_type = trimCopy(out.part_type);
	out.note = trimCopy(out.note);
	return true;
}

bool SaveChipRecordFile(const filesystem::path &path, const ChipRecord &rec, std::string &err) {
	const std::string yaml = emitChipYaml(rec);
	if (!writeFileAtomic(path, yaml, err)) return false;

	// Always reload-verify structure
	ChipRecord verify;
	std::string verr;
	if (!LoadChipRecordFile(path, verify, verr)) {
		err = verr.empty() ? "reload verify failed" : verr;
		return false;
	}
	if (trimCopy(verify.part_type) != trimCopy(rec.part_type)) {
		err = "reload verify part_type mismatch";
		return false;
	}
	return true;
}

ChipStore::ChipStore(filesystem::path rootDir) : root_(std::move(rootDir)) {}

const filesystem::path &ChipStore::root() const { return root_; }

std::mutex &ChipStore::mutex() { return mu_; }

bool ChipStore::Get(const std::string &partType, ChipRecord &out, std::string &errCode, std::string &errMsg) {
	std::lock_guard<std::mutex> lock(mu_);
	out = ChipRecord{};
	errCode.clear();
	errMsg.clear();

	std::string stem;
	if (!SanitizePartTypeFilename(partType, stem, errMsg)) {
		errCode = "INVALID_PART_TYPE";
		return false;
	}
	const auto path = chipPathForStem(root_, stem);
	std::error_code ec;
	if (!filesystem::exists(path, ec) || ec) {
		errCode = "CHIP_NOT_FOUND";
		errMsg = "chip not found";
		return false;
	}
	std::string ferr;
	if (!LoadChipRecordFile(path, out, ferr)) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = ferr.empty() ? "failed to load chip" : ferr;
		out = ChipRecord{};
		return false;
	}
	const std::string want = trimCopy(partType);
	if (trimCopy(out.part_type) != want) {
		// Path collision / wrong chip — do not return it.
		out = ChipRecord{};
		errCode = "CHIP_NOT_FOUND";
		errMsg = "chip not found";
		return false;
	}
	return true;
}

bool ChipStore::List(std::vector<ChipRecord> &out, std::string &errCode, std::string &errMsg) {
	std::lock_guard<std::mutex> lock(mu_);
	out.clear();
	errCode.clear();
	errMsg.clear();

	std::error_code ec;
	if (!filesystem::exists(root_, ec)) {
		// empty library is fine
		return true;
	}
	if (ec) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = "failed to access chip root";
		return false;
	}

	for (filesystem::directory_iterator it(root_, ec), end; !ec && it != end; it.increment(ec)) {
		if (ec) break;
		const auto &entry = *it;
		std::error_code e2;
		if (!entry.is_regular_file(e2) || e2) continue;
		const auto p = entry.path();
		if (p.extension() != ".yaml") continue;
		ChipRecord rec;
		std::string ferr;
		if (!LoadChipRecordFile(p, rec, ferr)) continue;
		if (trimCopy(rec.part_type).empty()) continue;
		out.push_back(std::move(rec));
	}
	if (ec) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = "failed to list chip directory";
		out.clear();
		return false;
	}
	std::sort(out.begin(), out.end(), [](const ChipRecord &a, const ChipRecord &b) {
		return a.part_type < b.part_type;
	});
	return true;
}

bool ChipStore::Put(const ChipRecord &rec, bool replaceConditionsIfPresent, std::string &errCode,
                    std::string &errMsg) {
	std::lock_guard<std::mutex> lock(mu_);
	errCode.clear();
	errMsg.clear();

	const std::string partType = trimCopy(rec.part_type);
	std::string stem;
	if (!SanitizePartTypeFilename(partType, stem, errMsg)) {
		errCode = "INVALID_PART_TYPE";
		return false;
	}

	std::error_code ec;
	filesystem::create_directories(root_, ec);
	if (ec) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = "failed to create chip root";
		return false;
	}

	const auto path = chipPathForStem(root_, stem);
	ChipRecord toSave = rec;
	toSave.part_type = partType;
	toSave.note = trimCopy(rec.note);

	if (filesystem::exists(path, ec) && !ec) {
		ChipRecord existing;
		std::string ferr;
		if (!LoadChipRecordFile(path, existing, ferr)) {
			errCode = "CHIP_STORE_FAILED";
			errMsg = ferr.empty() ? "failed to load existing chip" : ferr;
			return false;
		}
		if (trimCopy(existing.part_type) != partType) {
			errCode = "CHIP_PATH_COLLISION";
			errMsg = "path collision between '" + existing.part_type + "' and '" + partType + "'";
			return false;
		}
		if (!replaceConditionsIfPresent) {
			toSave.operating_conditions = existing.operating_conditions;
		}
	}

	std::string serr;
	if (!SaveChipRecordFile(path, toSave, serr)) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = serr.empty() ? "failed to save chip" : serr;
		return false;
	}
	return true;
}

bool ChipStore::ReplaceConditions(const std::string &partType, std::vector<OperatingCondition> ocs,
                                  std::string &errCode, std::string &errMsg) {
	// Get without holding caller's expectation of nested lock: use unlocked path via Put.
	ChipRecord rec;
	{
		std::string code, msg;
		// Best-effort load existing note
		std::string stem;
		std::string serr;
		if (SanitizePartTypeFilename(partType, stem, serr)) {
			const auto path = chipPathForStem(root_, stem);
			std::error_code ec;
			if (filesystem::exists(path, ec) && !ec) {
				ChipRecord existing;
				if (LoadChipRecordFile(path, existing, serr) &&
				    trimCopy(existing.part_type) == trimCopy(partType)) {
					rec.note = existing.note;
				}
			}
		}
	}
	rec.part_type = trimCopy(partType);
	rec.operating_conditions = std::move(ocs);
	return Put(rec, true, errCode, errMsg);
}

bool ChipStore::Delete(const std::string &partType, std::string &errCode, std::string &errMsg) {
	std::lock_guard<std::mutex> lock(mu_);
	errCode.clear();
	errMsg.clear();

	std::string stem;
	if (!SanitizePartTypeFilename(partType, stem, errMsg)) {
		errCode = "INVALID_PART_TYPE";
		return false;
	}
	const auto path = chipPathForStem(root_, stem);
	std::error_code ec;
	if (!filesystem::exists(path, ec) || ec) {
		errCode = "CHIP_NOT_FOUND";
		errMsg = "chip not found";
		return false;
	}

	// Ensure content part_type matches before delete
	ChipRecord existing;
	std::string ferr;
	if (!LoadChipRecordFile(path, existing, ferr)) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = ferr.empty() ? "failed to load chip" : ferr;
		return false;
	}
	if (trimCopy(existing.part_type) != trimCopy(partType)) {
		errCode = "CHIP_NOT_FOUND";
		errMsg = "chip not found";
		return false;
	}

	filesystem::remove(path, ec);
	if (ec) {
		errCode = "CHIP_STORE_FAILED";
		errMsg = "failed to delete chip file";
		return false;
	}
	return true;
}

} // namespace obv
