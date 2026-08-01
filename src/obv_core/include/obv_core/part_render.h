#pragma once

#include "Board.h"
#include "annotations.h"

#include <string>

namespace obv {

struct PartRenderOpts {
	double scale = 0;    // 0 = auto from maxEdge
	double padding = -1; // <0 = auto 5% of max(w,h), min 1.0
	int maxEdge = 512;   // 64..2048
	bool labels = true;
	bool partName = true;
};

struct PartRenderBounds {
	double minX = 0, minY = 0, maxX = 0, maxY = 0;
	double padding = 0;
};

// overlay show_name > board show_name > name > number
std::string PinDisplayLabel(const Pin &pin, const Annotations *ann /*nullable*/);

// Returns false if part missing or no outline and no pins
bool ComputePartBounds(const Board &board, const std::string &part,
                       double paddingOrAuto, PartRenderBounds &out, std::string &errCode);
// errCode: PART_NOT_FOUND | PART_NO_GEOMETRY

// Board → image pixel (flipY=true):
// imageX = (boardX - originBoardX) * scale
// imageY = (originBoardY - boardY) * scale
struct BoardToImage {
	double originBoardX = 0;
	double originBoardY = 0; // top of crop in board Y when flipY
	double scale = 1;
	bool flipY = true;
	int width = 0;
	int height = 0;
};

// Build transform from bounds + opts; clamps max edge to 2048 / opts.maxEdge
bool BuildBoardToImage(const PartRenderBounds &b, const PartRenderOpts &opts,
                       BoardToImage &out, std::string &err);

void BoardToImagePoint(const BoardToImage &t, double boardX, double boardY,
                       double &imageX, double &imageY);

} // namespace obv
