#pragma once

#include <array>
#include <cstdint>

namespace obv {

// Matches GUI/Config.h: FZKey/CAEKey = array<uint32_t,44>, XZZPCBKey = uint64_t.
struct DecryptKeys {
	std::array<uint32_t, 44> fzKey{};
	std::array<uint32_t, 44> caeKey{};
	uint64_t xzzKey = 0;
	bool hasFz = false;
	bool hasCae = false;
	bool hasXzz = false;
};

} // namespace obv
