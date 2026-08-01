#pragma once

#include "Board.h"
#include "annotations.h"

#include <string>
#include <vector>

namespace obv {

struct PinGridPin {
	std::string key;
	std::string id;
	std::string number;
	std::string name;
	std::string displayLabel;
	double boardX = 0;
	double boardY = 0;
	int row = 0;
	int col = 0;
};

struct PinGridResult {
	std::string part;
	// single | row | column | grid | sparse
	std::string kind;
	int rows = 0;
	int cols = 0;
	double pitchX = 0;
	double pitchY = 0;
	double originX = 0;
	double originY = 0;
	// constants for clients
	std::string row0 = "min_y";
	std::string col0 = "min_x";
	double fillRatio = 0;
	std::vector<std::string> warnings;
	std::vector<PinGridPin> pins;
};

// Infer row/col from pin board positions (1D projection clustering).
// errCode: PART_NOT_FOUND | PART_NO_PINS
// ann may be null; used only for displayLabel via PinDisplayLabel.
bool InferPinGrid(const Board &board, const std::string &part, const Annotations *ann,
                  PinGridResult &out, std::string &errCode);

std::string ExportPinGridJson(const std::string &boardId, const std::string &sourceName,
                              const PinGridResult &g);

} // namespace obv
