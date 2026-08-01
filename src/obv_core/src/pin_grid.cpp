// Pin grid inference: map part pin (x,y) → (row,col) via 1D projection clustering.

#include "obv_core/pin_grid.h"

#include "obv_core/part_render.h"
#include "obv_core/pin_resolve.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace obv {
namespace {

constexpr double kEps = 1e-9;
// Must be < 0.5 so half-pitch thermal pads (common on QFP) form their own row/col
// instead of merging into neighbors and causing duplicate (row,col) assignments.
constexpr double kGapFactor = 0.4;

void appendEscaped(std::ostringstream &os, const std::string &s) {
	os << '"';
	for (unsigned char c : s) {
		switch (c) {
			case '"': os << "\\\""; break;
			case '\\': os << "\\\\"; break;
			case '\b': os << "\\b"; break;
			case '\f': os << "\\f"; break;
			case '\n': os << "\\n"; break;
			case '\r': os << "\\r"; break;
			case '\t': os << "\\t"; break;
			default:
				if (c < 0x20) {
					os << "\\u" << std::hex << std::setw(4) << std::setfill('0')
					   << static_cast<int>(c) << std::dec << std::setfill(' ');
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
	os << v;
}

double medianSorted(std::vector<double> v) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	const size_t n = v.size();
	if (n % 2 == 1) return v[n / 2];
	return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Cluster 1D values; returns centers sorted ascending.
// Pitch is estimated from the mode of adjacent gaps (histogram peak), not the
// median: half-pitch thermal pads create smaller gaps that pull the median
// down and can still leave thr > half-pitch when using 0.6*median.
std::vector<double> cluster1D(const std::vector<double> &values) {
	if (values.empty()) return {};
	std::vector<double> s = values;
	std::sort(s.begin(), s.end());

	std::vector<double> gaps;
	for (size_t i = 1; i < s.size(); ++i) {
		const double g = s[i] - s[i - 1];
		if (g > kEps) gaps.push_back(g);
	}
	if (gaps.empty()) {
		return {medianSorted(s)};
	}

	// Mode via relative bins (~5% of median gap) — peak count wins.
	const double medGap = medianSorted(gaps);
	const double binW = std::max(medGap * 0.05, kEps * 10);
	std::map<long long, std::pair<int, double>> bins; // bin -> (count, sum)
	for (double g : gaps) {
		const long long b = static_cast<long long>(std::llround(g / binW));
		auto &e = bins[b];
		e.first += 1;
		e.second += g;
	}
	int bestCount = 0;
	double pitch = medGap;
	for (const auto &kv : bins) {
		if (kv.second.first > bestCount) {
			bestCount = kv.second.first;
			pitch = kv.second.second / kv.second.first;
		}
	}
	if (pitch < kEps) pitch = medGap;
	if (pitch < kEps) {
		return {medianSorted(s)};
	}

	const double thr = std::max(kGapFactor * pitch, kEps);

	std::vector<std::vector<double>> clusters;
	clusters.push_back({s[0]});
	for (size_t i = 1; i < s.size(); ++i) {
		const double g = s[i] - s[i - 1];
		if (g > thr) {
			clusters.push_back({});
		}
		clusters.back().push_back(s[i]);
	}

	std::vector<double> centers;
	centers.reserve(clusters.size());
	for (auto &c : clusters) {
		centers.push_back(medianSorted(std::move(c)));
	}
	return centers;
}

int nearestIndex(const std::vector<double> &centers, double v) {
	int best = 0;
	double bestD = std::numeric_limits<double>::infinity();
	for (size_t i = 0; i < centers.size(); ++i) {
		const double d = std::fabs(v - centers[i]);
		if (d < bestD) {
			bestD = d;
			best = static_cast<int>(i);
		}
	}
	return best;
}

double medianCenterPitch(const std::vector<double> &centers) {
	if (centers.size() < 2) return 0;
	std::vector<double> gaps;
	for (size_t i = 1; i < centers.size(); ++i) {
		gaps.push_back(centers[i] - centers[i - 1]);
	}
	return medianSorted(std::move(gaps));
}

struct PinRef {
	const Pin *pin = nullptr;
	size_t globalIndex = 0;
	double x = 0;
	double y = 0;
};

} // namespace

bool InferPinGrid(const Board &board, const std::string &part, const Annotations *ann,
                  PinGridResult &out, std::string &errCode) {
	out = PinGridResult{};
	out.part = part;
	out.row0 = "min_y";
	out.col0 = "min_x";

	if (!FindComponent(board, part)) {
		errCode = "PART_NOT_FOUND";
		return false;
	}

	// Board::Pins() is non-const in this codebase — const_cast for read-only walk.
	auto &pinsAll = const_cast<Board &>(board).Pins();
	std::vector<PinRef> pins;
	pins.reserve(pinsAll.size());
	for (size_t i = 0; i < pinsAll.size(); ++i) {
		const auto &sp = pinsAll[i];
		if (!sp || !sp->component) continue;
		if (sp->component->name != part) continue;
		PinRef pr;
		pr.pin = sp.get();
		pr.globalIndex = i;
		pr.x = sp->position.x;
		pr.y = sp->position.y;
		pins.push_back(pr);
	}

	if (pins.empty()) {
		errCode = "PART_NO_PINS";
		return false;
	}

	errCode.clear();

	if (pins.size() == 1) {
		out.kind = "single";
		out.rows = 1;
		out.cols = 1;
		out.pitchX = 0;
		out.pitchY = 0;
		out.originX = pins[0].x;
		out.originY = pins[0].y;
		out.fillRatio = 1.0;
		PinGridPin gp;
		gp.key = PinOverlayKey(*pins[0].pin);
		gp.id = ExportPinId(*pins[0].pin, pins[0].globalIndex);
		gp.number = pins[0].pin->number;
		gp.name = pins[0].pin->name;
		gp.displayLabel = PinDisplayLabel(*pins[0].pin, ann);
		gp.boardX = pins[0].x;
		gp.boardY = pins[0].y;
		gp.row = 0;
		gp.col = 0;
		out.pins.push_back(std::move(gp));
		return true;
	}

	std::vector<double> xs, ys;
	xs.reserve(pins.size());
	ys.reserve(pins.size());
	for (const auto &p : pins) {
		xs.push_back(p.x);
		ys.push_back(p.y);
	}

	const std::vector<double> colCenters = cluster1D(xs);
	const std::vector<double> rowCenters = cluster1D(ys);
	out.cols = static_cast<int>(colCenters.size());
	out.rows = static_cast<int>(rowCenters.size());
	out.pitchX = medianCenterPitch(colCenters);
	out.pitchY = medianCenterPitch(rowCenters);
	out.originX = colCenters.empty() ? 0 : colCenters.front();
	out.originY = rowCenters.empty() ? 0 : rowCenters.front();

	std::map<std::pair<int, int>, int> cellCount;
	out.pins.reserve(pins.size());
	for (const auto &p : pins) {
		const int r = nearestIndex(rowCenters, p.y);
		const int c = nearestIndex(colCenters, p.x);
		cellCount[{r, c}]++;
		PinGridPin gp;
		gp.key = PinOverlayKey(*p.pin);
		gp.id = ExportPinId(*p.pin, p.globalIndex);
		gp.number = p.pin->number;
		gp.name = p.pin->name;
		gp.displayLabel = PinDisplayLabel(*p.pin, ann);
		gp.boardX = p.x;
		gp.boardY = p.y;
		gp.row = r;
		gp.col = c;
		out.pins.push_back(std::move(gp));
	}

	const int cells = std::max(1, out.rows * out.cols);
	out.fillRatio = static_cast<double>(pins.size()) / static_cast<double>(cells);

	if (out.rows == 1 && out.cols > 1) {
		out.kind = "row";
	} else if (out.cols == 1 && out.rows > 1) {
		out.kind = "column";
	} else if (out.rows > 1 && out.cols > 1) {
		if (out.fillRatio < 0.5) {
			out.kind = "sparse";
			out.warnings.push_back(
			    "fillRatio < 0.5; possible peripheral package (QFP/SOP), not a full matrix");
		} else {
			out.kind = "grid";
		}
	} else {
		// rows==1 && cols==1 but multiple pins stacked — treat as single cell grid
		out.kind = "grid";
		out.warnings.push_back("all pins collapsed to one cell");
	}

	for (const auto &kv : cellCount) {
		if (kv.second > 1) {
			out.warnings.push_back("duplicate_cells");
			break;
		}
	}

	return true;
}

std::string ExportPinGridJson(const std::string &boardId, const std::string &sourceName,
                              const PinGridResult &g) {
	std::ostringstream os;
	os << "{\"boardId\":";
	appendEscaped(os, boardId);
	os << ",\"sourceName\":";
	appendEscaped(os, sourceName);
	os << ",\"part\":";
	appendEscaped(os, g.part);
	os << ",\"kind\":";
	appendEscaped(os, g.kind);
	os << ",\"rows\":" << g.rows;
	os << ",\"cols\":" << g.cols;
	os << ",\"pitchX\":";
	appendNumber(os, g.pitchX);
	os << ",\"pitchY\":";
	appendNumber(os, g.pitchY);
	os << ",\"origin\":{\"x\":";
	appendNumber(os, g.originX);
	os << ",\"y\":";
	appendNumber(os, g.originY);
	os << "},\"row0\":";
	appendEscaped(os, g.row0);
	os << ",\"col0\":";
	appendEscaped(os, g.col0);
	os << ",\"fillRatio\":";
	appendNumber(os, g.fillRatio);
	os << ",\"warnings\":[";
	for (size_t i = 0; i < g.warnings.size(); ++i) {
		if (i) os << ',';
		appendEscaped(os, g.warnings[i]);
	}
	os << "],\"pins\":[";
	for (size_t i = 0; i < g.pins.size(); ++i) {
		if (i) os << ',';
		const auto &p = g.pins[i];
		os << "{\"key\":";
		appendEscaped(os, p.key);
		os << ",\"id\":";
		appendEscaped(os, p.id);
		os << ",\"number\":";
		appendEscaped(os, p.number);
		os << ",\"name\":";
		appendEscaped(os, p.name);
		os << ",\"displayLabel\":";
		appendEscaped(os, p.displayLabel);
		os << ",\"board\":{\"x\":";
		appendNumber(os, p.boardX);
		os << ",\"y\":";
		appendNumber(os, p.boardY);
		os << "},\"row\":" << p.row << ",\"col\":" << p.col << '}';
	}
	os << "]}";
	return os.str();
}

} // namespace obv
