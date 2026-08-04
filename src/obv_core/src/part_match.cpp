#include "obv_core/part_match.h"

#include "Board.h"
#include "BRDBoard.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace obv {
namespace {

// Mirror of board_json deriveCompGeom center path (pin bbox midpoint).
void partCenter(const Component &c, double &cx, double &cy) {
	if (c.pins.empty()) {
		cx = c.centerpoint.x;
		cy = c.centerpoint.y;
		return;
	}
	float minX = std::numeric_limits<float>::max();
	float minY = std::numeric_limits<float>::max();
	float maxX = std::numeric_limits<float>::lowest();
	float maxY = std::numeric_limits<float>::lowest();
	int n = 0;
	for (const auto &pp : c.pins) {
		if (!pp) continue;
		const float x = pp->position.x;
		const float y = pp->position.y;
		minX = std::min(minX, x);
		minY = std::min(minY, y);
		maxX = std::max(maxX, x);
		maxY = std::max(maxY, y);
		++n;
	}
	if (n == 0) {
		cx = c.centerpoint.x;
		cy = c.centerpoint.y;
		return;
	}
	cx = 0.5 * (static_cast<double>(minX) + static_cast<double>(maxX));
	cy = 0.5 * (static_cast<double>(minY) + static_cast<double>(maxY));
}

void appendEscaped(std::ostringstream &os, const std::string &s) {
	os << '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			os << "\\\"";
			break;
		case '\\':
			os << "\\\\";
			break;
		case '\b':
			os << "\\b";
			break;
		case '\f':
			os << "\\f";
			break;
		case '\n':
			os << "\\n";
			break;
		case '\r':
			os << "\\r";
			break;
		case '\t':
			os << "\\t";
			break;
		default:
			if (c < 0x20) {
				const char *hex = "0123456789abcdef";
				os << "\\u00" << hex[(c >> 4) & 0xf] << hex[c & 0xf];
			} else {
				os << static_cast<char>(c);
			}
			break;
		}
	}
	os << '"';
}

void appendNumber(std::ostringstream &os, double v) {
	if (!std::isfinite(v)) {
		os << "0";
		return;
	}
	// trim trailing zeros lightly via default
	os << v;
}

struct CanvasPart {
	std::string name;
	int pinCount = 0;
	double x = 0;
	double y = 0;
};

// Rotate around bounds center C by rot CW degrees.
void rotateAround(double x, double y, double cx, double cy, int rot, double &ox, double &oy) {
	const double dx = x - cx;
	const double dy = y - cy;
	double rx = dx;
	double ry = dy;
	switch (rot) {
	case 90:
		rx = dy;
		ry = -dx;
		break;
	case 180:
		rx = -dx;
		ry = -dy;
		break;
	case 270:
		rx = -dy;
		ry = dx;
		break;
	default:
		break;
	}
	ox = cx + rx;
	oy = cy + ry;
}

bool prepareSide(const std::vector<PartFingerprint> &fps, int rot, const std::string &region,
                 std::vector<CanvasPart> &out, std::string &errCode, std::string &errMsg) {
	out.clear();
	if (fps.empty()) {
		errCode = "EMPTY_REGION";
		errMsg = "no parts after minPins filter";
		return false;
	}

	double minX = fps[0].cx, maxX = fps[0].cx, minY = fps[0].cy, maxY = fps[0].cy;
	for (const auto &p : fps) {
		minX = std::min(minX, p.cx);
		maxX = std::max(maxX, p.cx);
		minY = std::min(minY, p.cy);
		maxY = std::max(maxY, p.cy);
	}
	const double cx = 0.5 * (minX + maxX);
	const double cy = 0.5 * (minY + maxY);

	std::vector<CanvasPart> rotated;
	rotated.reserve(fps.size());
	for (const auto &p : fps) {
		CanvasPart c;
		c.name = p.name;
		c.pinCount = p.pinCount;
		rotateAround(p.cx, p.cy, cx, cy, rot, c.x, c.y);
		rotated.push_back(std::move(c));
	}

	minX = rotated[0].x;
	maxX = rotated[0].x;
	minY = rotated[0].y;
	maxY = rotated[0].y;
	for (const auto &p : rotated) {
		minX = std::min(minX, p.x);
		maxX = std::max(maxX, p.x);
		minY = std::min(minY, p.y);
		maxY = std::max(maxY, p.y);
	}
	const double midX = 0.5 * (minX + maxX);
	const double midY = 0.5 * (minY + maxY);

	out.reserve(rotated.size());
	for (const auto &p : rotated) {
		bool keep = true;
		if (region == "left") {
			keep = p.x <= midX;
		} else if (region == "right") {
			keep = p.x >= midX;
		} else if (region == "bottom") {
			keep = p.y <= midY;
		} else if (region == "top") {
			keep = p.y >= midY;
		} else if (region == "all") {
			keep = true;
		} else {
			errCode = "BAD_REQUEST";
			errMsg = "invalid region";
			return false;
		}
		if (keep) out.push_back(p);
	}

	if (out.empty()) {
		errCode = "EMPTY_REGION";
		errMsg = "no parts in region after filter";
		return false;
	}

	// centroid to origin
	double gx = 0, gy = 0;
	for (const auto &p : out) {
		gx += p.x;
		gy += p.y;
	}
	gx /= static_cast<double>(out.size());
	gy /= static_cast<double>(out.size());
	for (auto &p : out) {
		p.x -= gx;
		p.y -= gy;
	}
	return true;
}

} // namespace

bool IsValidPartMatchRot(int rot) {
	return rot == 0 || rot == 90 || rot == 180 || rot == 270;
}

bool IsValidPartMatchRegion(const std::string &region) {
	return region == "all" || region == "left" || region == "right" || region == "top" ||
	       region == "bottom";
}

bool IsValidPartMatchSplit(const std::string &split) {
	return split == "none" || split == "vertical" || split == "horizontal";
}

std::vector<PartFingerprint> CollectPartFingerprints(const BoardSnapshot &snap, int minPins) {
	std::vector<PartFingerprint> rows;
	if (!snap.ok() || !snap.board) {
		return rows;
	}
	if (minPins < 0) minPins = 0;
	Board &board = *snap.board;
	rows.reserve(board.Components().size());

	for (const auto &cp : board.Components()) {
		if (!cp || cp->name.empty()) continue;
		int pinCount = 0;
		for (const auto &pp : cp->pins) {
			if (pp) ++pinCount;
		}
		if (pinCount == 0) {
			for (const auto &pp : board.Pins()) {
				if (pp && pp->component && pp->component.get() == cp.get()) ++pinCount;
			}
		}
		if (pinCount < minPins) continue;
		PartFingerprint r;
		r.name = cp->name;
		partCenter(*cp, r.cx, r.cy);
		r.pinCount = pinCount;
		rows.push_back(std::move(r));
	}

	std::sort(rows.begin(), rows.end(), [](const PartFingerprint &a, const PartFingerprint &b) {
		if (a.cy != b.cy) return a.cy < b.cy;
		if (a.cx != b.cx) return a.cx < b.cx;
		return a.name < b.name;
	});
	return rows;
}

bool MatchBoardParts(const BoardSnapshot &snapA, const std::string &boardIdA,
                     const std::string &sourceNameA, int rotA, const std::string &regionA,
                     const BoardSnapshot &snapB, const std::string &boardIdB,
                     const std::string &sourceNameB, int rotB, const std::string &regionB,
                     const std::string &split, int minPins, double maxDist, PartMatchResult &out,
                     std::string &errCode, std::string &errMsg) {
	out = PartMatchResult{};
	errCode.clear();
	errMsg.clear();

	if (!IsValidPartMatchRot(rotA) || !IsValidPartMatchRot(rotB)) {
		errCode = "BAD_REQUEST";
		errMsg = "rot must be 0, 90, 180, or 270";
		return false;
	}
	if (!IsValidPartMatchRegion(regionA) || !IsValidPartMatchRegion(regionB)) {
		errCode = "BAD_REQUEST";
		errMsg = "region must be all|left|right|top|bottom";
		return false;
	}
	if (!IsValidPartMatchSplit(split)) {
		errCode = "BAD_REQUEST";
		errMsg = "split must be none|vertical|horizontal";
		return false;
	}
	if (!(maxDist > 0) || maxDist > 1e6) {
		errCode = "BAD_REQUEST";
		errMsg = "invalid maxDist";
		return false;
	}
	if (minPins < 0 || minPins > 100000) {
		errCode = "BAD_REQUEST";
		errMsg = "invalid minPins";
		return false;
	}
	if (!snapA.ok() || !snapA.board || !snapB.ok() || !snapB.board) {
		errCode = "PARSE_FAILED";
		errMsg = "board parse failed";
		return false;
	}

	const auto fpsA = CollectPartFingerprints(snapA, minPins);
	const auto fpsB = CollectPartFingerprints(snapB, minPins);

	std::vector<CanvasPart> sideA, sideB;
	if (!prepareSide(fpsA, rotA, regionA, sideA, errCode, errMsg)) return false;
	if (!prepareSide(fpsB, rotB, regionB, sideB, errCode, errMsg)) return false;

	out.a.boardId = boardIdA;
	out.a.sourceName = sourceNameA;
	out.a.rot = rotA;
	out.a.region = regionA;
	out.b.boardId = boardIdB;
	out.b.sourceName = sourceNameB;
	out.b.rot = rotB;
	out.b.region = regionB;
	out.split = split;
	out.minPins = minPins;
	out.maxDist = maxDist;
	out.align = "region_centroid";
	out.partCountA = static_cast<int>(sideA.size());
	out.partCountB = static_cast<int>(sideB.size());

	// Greedy NN: large pinCount first.
	std::vector<int> orderA(sideA.size());
	for (size_t i = 0; i < sideA.size(); ++i) orderA[i] = static_cast<int>(i);
	std::sort(orderA.begin(), orderA.end(), [&](int i, int j) {
		if (sideA[i].pinCount != sideA[j].pinCount) return sideA[i].pinCount > sideA[j].pinCount;
		return sideA[i].name < sideA[j].name;
	});

	std::vector<char> usedB(sideB.size(), 0);
	std::vector<char> usedA(sideA.size(), 0);

	for (int ia : orderA) {
		const auto &a = sideA[static_cast<size_t>(ia)];
		int bestJ = -1;
		double bestD = 0;
		for (size_t jb = 0; jb < sideB.size(); ++jb) {
			if (usedB[jb]) continue;
			const auto &b = sideB[jb];
			if (a.pinCount != b.pinCount) continue;
			const double d = std::hypot(a.x - b.x, a.y - b.y);
			if (d > maxDist) continue;
			if (bestJ < 0 || d < bestD) {
				bestJ = static_cast<int>(jb);
				bestD = d;
			}
		}
		if (bestJ < 0) continue;
		usedA[static_cast<size_t>(ia)] = 1;
		usedB[static_cast<size_t>(bestJ)] = 1;
		const auto &b = sideB[static_cast<size_t>(bestJ)];
		PartMatchPair m;
		m.partA = a.name;
		m.partB = b.name;
		m.pinCount = a.pinCount;
		m.dist = bestD;
		m.canvasAx = a.x;
		m.canvasAy = a.y;
		m.canvasBx = b.x;
		m.canvasBy = b.y;
		out.matches.push_back(std::move(m));
	}

	std::sort(out.matches.begin(), out.matches.end(),
	          [](const PartMatchPair &x, const PartMatchPair &y) {
		          if (x.dist != y.dist) return x.dist < y.dist;
		          return x.partA < y.partA;
	          });

	for (size_t i = 0; i < sideA.size(); ++i) {
		if (!usedA[i]) out.unmatchedA.push_back(sideA[i].name);
	}
	for (size_t i = 0; i < sideB.size(); ++i) {
		if (!usedB[i]) out.unmatchedB.push_back(sideB[i].name);
	}
	std::sort(out.unmatchedA.begin(), out.unmatchedA.end());
	std::sort(out.unmatchedB.begin(), out.unmatchedB.end());
	return true;
}

std::string ExportPartMatchJson(const PartMatchResult &r) {
	std::ostringstream os;
	os << '{';
	auto side = [&](const char *key, const PartMatchSide &s, int partCount) {
		os << '"' << key << "\":{";
		os << "\"boardId\":";
		appendEscaped(os, s.boardId);
		os << ",\"sourceName\":";
		appendEscaped(os, s.sourceName);
		os << ",\"rot\":" << s.rot;
		os << ",\"region\":";
		appendEscaped(os, s.region);
		os << ",\"partCount\":" << partCount;
		os << '}';
	};
	side("a", r.a, r.partCountA);
	os << ',';
	side("b", r.b, r.partCountB);
	os << ",\"split\":";
	appendEscaped(os, r.split);
	os << ",\"minPins\":" << r.minPins;
	os << ",\"maxDist\":";
	appendNumber(os, r.maxDist);
	os << ",\"align\":";
	appendEscaped(os, r.align);
	os << ",\"matchCount\":" << r.matches.size();
	os << ",\"matches\":[";
	for (size_t i = 0; i < r.matches.size(); ++i) {
		if (i) os << ',';
		const auto &m = r.matches[i];
		os << "{\"partA\":";
		appendEscaped(os, m.partA);
		os << ",\"partB\":";
		appendEscaped(os, m.partB);
		os << ",\"pinCount\":" << m.pinCount;
		os << ",\"dist\":";
		appendNumber(os, m.dist);
		os << ",\"canvasA\":{\"x\":";
		appendNumber(os, m.canvasAx);
		os << ",\"y\":";
		appendNumber(os, m.canvasAy);
		os << "},\"canvasB\":{\"x\":";
		appendNumber(os, m.canvasBx);
		os << ",\"y\":";
		appendNumber(os, m.canvasBy);
		os << "}}";
	}
	os << "],\"unmatchedA\":[";
	for (size_t i = 0; i < r.unmatchedA.size(); ++i) {
		if (i) os << ',';
		appendEscaped(os, r.unmatchedA[i]);
	}
	os << "],\"unmatchedB\":[";
	for (size_t i = 0; i < r.unmatchedB.size(); ++i) {
		if (i) os << ',';
		appendEscaped(os, r.unmatchedB[i]);
	}
	os << "]}";
	return os.str();
}

} // namespace obv
