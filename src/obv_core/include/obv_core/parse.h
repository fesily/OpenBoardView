#pragma once

#include "obv_core/board_snapshot.h"
#include "obv_core/decrypt_keys.h"
#include "obv_core/filesystem_impl.h"

#include <vector>

namespace obv {

// filepath optional (used for ASC relative assets + overlay naming)
BoardSnapshot ParseBoardBuffer(std::vector<char> buffer,
                               const filesystem::path &filepath,
                               const DecryptKeys &keys);
BoardSnapshot ParseBoardFile(const filesystem::path &filepath,
                             const DecryptKeys &keys);

} // namespace obv
