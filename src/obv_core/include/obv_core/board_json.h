#pragma once

#include "obv_core/board_snapshot.h"

#include <string>

namespace obv {

// boardSchemaVersion = 1
// Full board document (§5.2). Returns empty string when snap is not ok().
std::string ExportBoardJson(const BoardSnapshot &snap, const std::string &boardId);

// Meta-only: schema version, boardId, sourceName, bounds, sides.
// Returns empty string when snap is not ok().
std::string ExportMetaJson(const BoardSnapshot &snap, const std::string &boardId);

} // namespace obv
