#pragma once

#include "Board.h"
#include "annotations.h"

#include <cstdint>
#include <string>
#include <vector>

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

// Encode tightly packed RGBA8 to PNG bytes (for unit tests / internal use).
bool EncodePng(int width, int height, const unsigned char *rgba, std::string &outPng);

struct PartPinMeta {
	std::string key, id, number, name;
	std::string boardShowName, overlayShowName, displayLabel;
	double boardX = 0, boardY = 0;
	double imageX = 0, imageY = 0;
	std::string type, shape;
	double diameter = 0;
	std::string netName;
};

struct PartScreenshotMeta {
	std::string part;
	BoardToImage transform;
	PartRenderBounds bounds; // final padded bounds
	PartRenderOpts optsUsed;
	std::vector<PartPinMeta> pins;
};

struct PartScreenshotResult {
	std::string png; // binary
	PartScreenshotMeta meta;
};

// errCode: PART_NOT_FOUND | PART_NO_GEOMETRY | RENDER_FAILED | BAD_REQUEST
bool RenderPartScreenshot(const Board &board, const Annotations &ann,
                          const std::string &part, const PartRenderOpts &opts,
                          PartScreenshotResult &out, std::string &errCode,
                          std::string &errMessage);

} // namespace obv
