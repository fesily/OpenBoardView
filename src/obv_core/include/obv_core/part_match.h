#pragma once

#include "obv_core/board_snapshot.h"

#include <string>
#include <vector>

namespace obv {

// Geometric fingerprint of one part (same fields as part-layout).
struct PartFingerprint {
	std::string name;
	double cx = 0;
	double cy = 0;
	int pinCount = 0;
};

// Collect parts with pinCount >= minPins; sorted y,x,name. Empty if snap not ok.
std::vector<PartFingerprint> CollectPartFingerprints(const BoardSnapshot &snap, int minPins);

// Per-board view for match.
struct PartMatchSide {
	std::string boardId;
	std::string sourceName;
	int rot = 0; // 0,90,180,270 (CW degrees)
	// all|left|right|top|bottom — applied after rotation on rotated AABB midlines
	std::string region = "all";
};

struct PartMatchPair {
	std::string partA;
	std::string partB;
	int pinCount = 0;
	double dist = 0;
	double canvasAx = 0;
	double canvasAy = 0;
	double canvasBx = 0;
	double canvasBy = 0;
};

struct PartMatchResult {
	PartMatchSide a;
	PartMatchSide b;
	std::string split = "none"; // none|vertical|horizontal (echo)
	int minPins = 2;
	double maxDist = 50;
	std::string align = "region_centroid";
	int partCountA = 0;
	int partCountB = 0;
	std::vector<PartMatchPair> matches;
	std::vector<std::string> unmatchedA;
	std::vector<std::string> unmatchedB;
};

// errCode: BAD_REQUEST | EMPTY_REGION | PARSE_FAILED
// rot must be 0/90/180/270; region/split validated.
bool MatchBoardParts(const BoardSnapshot &snapA, const std::string &boardIdA,
                     const std::string &sourceNameA, int rotA, const std::string &regionA,
                     const BoardSnapshot &snapB, const std::string &boardIdB,
                     const std::string &sourceNameB, int rotB, const std::string &regionB,
                     const std::string &split, int minPins, double maxDist, PartMatchResult &out,
                     std::string &errCode, std::string &errMsg);

std::string ExportPartMatchJson(const PartMatchResult &r);

// true if rot is 0,90,180,270
bool IsValidPartMatchRot(int rot);
// true if region is all|left|right|top|bottom
bool IsValidPartMatchRegion(const std::string &region);
// true if split is none|vertical|horizontal
bool IsValidPartMatchSplit(const std::string &split);

} // namespace obv
