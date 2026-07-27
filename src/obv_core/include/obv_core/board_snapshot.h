#pragma once

#include "BRDBoard.h"
#include "FileFormats/BRDFileBase.h"
#include "obv_core/decrypt_keys.h"

#include <memory>
#include <string>

namespace obv {

struct BoardBounds {
	float minX = 0.f;
	float minY = 0.f;
	float maxX = 0.f;
	float maxY = 0.f;
};

// Ownership: BRDBoard stores a raw const BRDFileBase* - `file` MUST outlive `board`.
// Member declaration order ensures `board` is destroyed before `file`.
struct BoardSnapshot {
	std::unique_ptr<BRDFileBase> file;
	std::unique_ptr<BRDBoard> board;
	BoardBounds bounds{};
	std::string sourceName;
	std::string error;

	bool ok() const { return file && file->valid && board; }
};

} // namespace obv
