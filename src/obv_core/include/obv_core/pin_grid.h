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
	// Local coordinates after PCA alignment (board - centroid, rotated by -θ).
	double localX = 0;
	double localY = 0;
	int row = 0;
	int col = 0;
	// peripheral: top|bottom|left|right|thermal ; empty for non-peripheral
	std::string side;
	// index along side (0-based); thermal usually 0; -1 if unset
	int index = -1;
};

struct PinGridResult {
	std::string part;
	// single | row | column | grid | sparse | peripheral | unordered
	std::string kind;
	// Same as kind for v2 clients; kept explicit.
	std::string layout;
	int rows = 0;
	int cols = 0;
	double pitchX = 0;
	double pitchY = 0;
	double originX = 0;
	double originY = 0;
	// PCA alignment (degrees). 0 if not applied. NEVER from part.angle.
	double rotationDeg = 0;
	double centroidX = 0;
	double centroidY = 0;
	// constants for clients (local axes after PCA)
	std::string row0 = "min_local_y";
	std::string col0 = "min_local_x";
	double fillRatio = 0;
	std::vector<std::string> warnings;
	std::vector<PinGridPin> pins;
};

// Infer layout from pin board positions.
// Rotation: PCA principal axis only — do NOT use PartInfo.angle.
// errCode: PART_NOT_FOUND | PART_NO_PINS
// ann may be null; used only for displayLabel via PinDisplayLabel.
bool InferPinGrid(const Board &board, const std::string &part, const Annotations *ann,
                  PinGridResult &out, std::string &errCode);

std::string ExportPinGridJson(const std::string &boardId, const std::string &sourceName,
                              const PinGridResult &g);

} // namespace obv
