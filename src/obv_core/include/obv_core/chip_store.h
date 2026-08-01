#pragma once

#include "obv_core/filesystem_impl.h"

#include "annotations.h"

#include <mutex>
#include <string>
#include <vector>

namespace obv {

struct ChipRecord {
	std::string part_type;
	std::string note;
	std::vector<OperatingCondition> operating_conditions;
};

// Returns false + err if empty after trim or maps to empty/./..
bool SanitizePartTypeFilename(const std::string &partType, std::string &outFileStem, std::string &err);

enum class ConditionSource { None, Board, Chip };

struct MergedConditions {
	ConditionSource source = ConditionSource::None;
	std::vector<OperatingCondition> effective;
	std::vector<OperatingCondition> board;
	std::vector<OperatingCondition> chip;
};

MergedConditions MergeOperatingConditions(
	const std::vector<OperatingCondition> *boardOrNull,
	const std::vector<OperatingCondition> *chipOrNull);

// Free helpers for YAML without holding lock (used by ChipStore and tests)
bool LoadChipRecordFile(const filesystem::path &path, ChipRecord &out, std::string &err);
bool SaveChipRecordFile(const filesystem::path &path, const ChipRecord &rec, std::string &err);

class ChipStore {
public:
	explicit ChipStore(filesystem::path rootDir);
	const filesystem::path &root() const;

	// Thread-safe. codes: empty / CHIP_NOT_FOUND / CHIP_PATH_COLLISION / CHIP_STORE_FAILED / INVALID_PART_TYPE
	bool Get(const std::string &partType, ChipRecord &out, std::string &errCode, std::string &errMsg);
	bool List(std::vector<ChipRecord> &out, std::string &errCode, std::string &errMsg);
	bool Put(const ChipRecord &rec, bool replaceConditionsIfPresent, std::string &errCode, std::string &errMsg);
	// PutUpsertConditions: replace conditions array entirely for partType (create if missing)
	bool ReplaceConditions(const std::string &partType, std::vector<OperatingCondition> ocs,
	                       std::string &errCode, std::string &errMsg);
	bool Delete(const std::string &partType, std::string &errCode, std::string &errMsg);

	std::mutex &mutex(); // for external lock-order with board overlay
private:
	filesystem::path root_;
	std::mutex mu_;
};

} // namespace obv
