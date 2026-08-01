// Pin grid inference v2:
// - PCA axis alignment (never PartInfo.angle)
// - classify: single/row/column/grid/sparse/peripheral/unordered
// - peripheral: side + index; grid: 1D clustering on local axes

#include "obv_core/pin_grid.h"

#include "obv_core/part_render.h"
#include "obv_core/pin_resolve.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace obv {
namespace {

constexpr double kEps = 1e-9;
// Must be < 0.5 so half-pitch thermal pads form own row/col.
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

	const double medGap = medianSorted(gaps);
	const double binW = std::max(medGap * 0.05, kEps * 10);
	std::map<long long, std::pair<int, double>> bins;
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
			pitch = kv.second.second / static_cast<double>(kv.second.first);
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
	double bx = 0;
	double by = 0;
	double lx = 0;
	double ly = 0;
};

// PCA principal angle in radians. Never uses part.angle.
// Returns 0 if &lt;3 pins or nearly isotropic.
double computePcaAngle(const std::vector<PinRef> &pins, double &cx, double &cy) {
	cx = 0;
	cy = 0;
	if (pins.empty()) return 0;
	for (const auto &p : pins) {
		cx += p.bx;
		cy += p.by;
	}
	cx /= static_cast<double>(pins.size());
	cy /= static_cast<double>(pins.size());
	if (pins.size() < 3) return 0;

	double sxx = 0, syy = 0, sxy = 0;
	for (const auto &p : pins) {
		const double dx = p.bx - cx;
		const double dy = p.by - cy;
		sxx += dx * dx;
		syy += dy * dy;
		sxy += dx * dy;
	}
	const double n = static_cast<double>(pins.size());
	sxx /= n;
	syy /= n;
	sxy /= n;

	// Eigenvector of larger eigenvalue for [[sxx,sxy],[sxy,syy]]
	const double trace = sxx + syy;
	const double det = sxx * syy - sxy * sxy;
	const double disc = std::max(0.0, trace * trace * 0.25 - det);
	const double l1 = trace * 0.5 + std::sqrt(disc);
	const double l2 = trace * 0.5 - std::sqrt(disc);
	if (l1 < kEps) return 0;
	// Anisotropy: if nearly circular, skip rotation
	if (l2 > 0 && (l1 / std::max(l2, kEps)) < 1.15) return 0;

	double angle = 0;
	if (std::fabs(sxy) > kEps || std::fabs(sxx - l1) > kEps) {
		// (sxx-l1) * vx + sxy * vy = 0
		angle = std::atan2(l1 - sxx, sxy);
		// Prefer angle of principal axis as orientation of "x"
		// atan2(vy, vx) with vx=sxy, vy=l1-sxx
		angle = std::atan2(l1 - sxx, sxy);
		if (!std::isfinite(angle)) angle = 0;
	}

	// Snap near axis-aligned to 0 / ±2 for stability
	const double deg = angle * 180.0 / M_PI;
	const double ad = std::fmod(std::fabs(deg), 90.0);
	const double dist = std::min(ad, 90.0 - ad);
	if (dist < 15.0) {
		// snap to nearest multiple of 90°
		const double snapped = std::round(deg / 90.0) * 90.0;
		return snapped * M_PI / 180.0;
	}
	return angle;
}

void applyLocalFrame(std::vector<PinRef> &pins, double cx, double cy, double theta) {
	const double c = std::cos(-theta);
	const double s = std::sin(-theta);
	for (auto &p : pins) {
		const double dx = p.bx - cx;
		const double dy = p.by - cy;
		p.lx = c * dx - s * dy;
		p.ly = s * dx + c * dy;
	}
}

struct GridAssign {
	int rows = 0;
	int cols = 0;
	double pitchX = 0;
	double pitchY = 0;
	double originX = 0; // local
	double originY = 0;
	double fillRatio = 0;
	std::vector<int> row; // per pin
	std::vector<int> col;
	bool hasDup = false;
};

GridAssign assignGridLocal(const std::vector<PinRef> &pins) {
	GridAssign g;
	g.row.assign(pins.size(), 0);
	g.col.assign(pins.size(), 0);
	if (pins.empty()) return g;

	std::vector<double> xs, ys;
	xs.reserve(pins.size());
	ys.reserve(pins.size());
	for (const auto &p : pins) {
		xs.push_back(p.lx);
		ys.push_back(p.ly);
	}
	const auto colCenters = cluster1D(xs);
	const auto rowCenters = cluster1D(ys);
	g.cols = static_cast<int>(colCenters.size());
	g.rows = static_cast<int>(rowCenters.size());
	g.pitchX = medianCenterPitch(colCenters);
	g.pitchY = medianCenterPitch(rowCenters);
	g.originX = colCenters.empty() ? 0 : colCenters.front();
	g.originY = rowCenters.empty() ? 0 : rowCenters.front();

	std::map<std::pair<int, int>, int> cellCount;
	for (size_t i = 0; i < pins.size(); ++i) {
		g.row[i] = nearestIndex(rowCenters, pins[i].ly);
		g.col[i] = nearestIndex(colCenters, pins[i].lx);
		cellCount[{g.row[i], g.col[i]}]++;
	}
	for (const auto &kv : cellCount) {
		if (kv.second > 1) {
			g.hasDup = true;
			break;
		}
	}
	const int cells = std::max(1, g.rows * g.cols);
	g.fillRatio = static_cast<double>(pins.size()) / static_cast<double>(cells);
	return g;
}

struct BBox {
	double minX = 0, maxX = 0, minY = 0, maxY = 0;
	double w() const { return maxX - minX; }
	double h() const { return maxY - minY; }
};

BBox localBBox(const std::vector<PinRef> &pins) {
	BBox b;
	b.minX = b.maxX = pins[0].lx;
	b.minY = b.maxY = pins[0].ly;
	for (const auto &p : pins) {
		b.minX = std::min(b.minX, p.lx);
		b.maxX = std::max(b.maxX, p.lx);
		b.minY = std::min(b.minY, p.ly);
		b.maxY = std::max(b.maxY, p.ly);
	}
	return b;
}

void edgeStats(const std::vector<PinRef> &pins, const BBox &b, double margin,
               double &edgeFrac, double &interiorFrac) {
	if (pins.empty()) {
		edgeFrac = interiorFrac = 0;
		return;
	}
	int edge = 0, interior = 0;
	for (const auto &p : pins) {
		const double dt = std::fabs(p.ly - b.maxY);
		const double db = std::fabs(p.ly - b.minY);
		const double dl = std::fabs(p.lx - b.minX);
		const double dr = std::fabs(p.lx - b.maxX);
		const double dEdge = std::min(std::min(dt, db), std::min(dl, dr));
		if (dEdge <= margin) {
			++edge;
		} else {
			++interior;
		}
	}
	edgeFrac = static_cast<double>(edge) / static_cast<double>(pins.size());
	interiorFrac = static_cast<double>(interior) / static_cast<double>(pins.size());
}

// Classify layout. Does not use part.angle.
std::string classifyLayout(size_t n, const GridAssign &g, double edgeFrac, double interiorFrac) {
	if (n <= 1) return "single";
	if (g.rows == 1 && g.cols > 1) return "row";
	if (g.cols == 1 && g.rows > 1) return "column";
	// Peripheral QFP/SOP: most pins on bbox rim, few interior (thermal OK as ≤25%).
	if (n >= 8 && edgeFrac >= 0.75 && interiorFrac <= 0.25) return "peripheral";
	if (g.rows > 1 && g.cols > 1) {
		if (g.fillRatio >= 0.5) return "grid";
		// Hollow lattice: prefer peripheral when edge-dominated
		if (edgeFrac >= 0.6) return "peripheral";
		return "sparse";
	}
	if (g.rows == 1 && g.cols == 1) return "unordered";
	return "unordered";
}

struct PeriAssign {
	std::vector<std::string> side;
	std::vector<int> index;
};

PeriAssign assignPeripheral(const std::vector<PinRef> &pins, const BBox &b, double margin,
                            std::vector<std::string> &warnings) {
	PeriAssign a;
	a.side.assign(pins.size(), "");
	a.index.assign(pins.size(), -1);

	const double cx = 0.5 * (b.minX + b.maxX);
	const double cy = 0.5 * (b.minY + b.maxY);
	const double thermalR = 0.25 * std::min(std::max(b.w(), kEps), std::max(b.h(), kEps));

	enum Edge { Top, Bottom, Left, Right, Thermal };
	std::vector<Edge> edges(pins.size(), Top);

	for (size_t i = 0; i < pins.size(); ++i) {
		const auto &p = pins[i];
		const double dt = std::fabs(p.ly - b.maxY);
		const double db = std::fabs(p.ly - b.minY);
		const double dl = std::fabs(p.lx - b.minX);
		const double dr = std::fabs(p.lx - b.maxX);
		const double dEdge = std::min(std::min(dt, db), std::min(dl, dr));
		const double dCen = std::hypot(p.lx - cx, p.ly - cy);

		if (dEdge > margin && dCen < thermalR) {
			edges[i] = Thermal;
			a.side[i] = "thermal";
			continue;
		}
		// nearest edge
		Edge e = Top;
		double best = dt;
		if (db < best) {
			best = db;
			e = Bottom;
		}
		if (dl < best) {
			best = dl;
			e = Left;
		}
		if (dr < best) {
			best = dr;
			e = Right;
		}
		if (dEdge > margin) {
			warnings.push_back("ambiguous_side");
		}
		edges[i] = e;
		switch (e) {
			case Top: a.side[i] = "top"; break;
			case Bottom: a.side[i] = "bottom"; break;
			case Left: a.side[i] = "left"; break;
			case Right: a.side[i] = "right"; break;
			default: break;
		}
	}

	// Dedupe ambiguous_side warning
	{
		bool amb = false;
		std::vector<std::string> w2;
		for (const auto &w : warnings) {
			if (w == "ambiguous_side") {
				if (!amb) {
					w2.push_back(w);
					amb = true;
				}
			} else {
				w2.push_back(w);
			}
		}
		warnings.swap(w2);
	}

	auto sortSide = [&](Edge e, bool byX) {
		std::vector<size_t> idx;
		for (size_t i = 0; i < pins.size(); ++i) {
			if (edges[i] == e) idx.push_back(i);
		}
		std::sort(idx.begin(), idx.end(), [&](size_t i, size_t j) {
			if (byX) {
				if (std::fabs(pins[i].lx - pins[j].lx) > kEps) return pins[i].lx < pins[j].lx;
				return pins[i].ly < pins[j].ly;
			}
			if (std::fabs(pins[i].ly - pins[j].ly) > kEps) return pins[i].ly < pins[j].ly;
			return pins[i].lx < pins[j].lx;
		});
		for (size_t k = 0; k < idx.size(); ++k) {
			a.index[idx[k]] = static_cast<int>(k);
		}
	};

	sortSide(Top, true);
	sortSide(Bottom, true);
	sortSide(Left, false);
	sortSide(Right, false);
	// thermal: index by distance to centroid then angle
	{
		std::vector<size_t> idx;
		for (size_t i = 0; i < pins.size(); ++i) {
			if (edges[i] == Thermal) idx.push_back(i);
		}
		std::sort(idx.begin(), idx.end(), [&](size_t i, size_t j) {
			const double di = std::hypot(pins[i].lx - cx, pins[i].ly - cy);
			const double dj = std::hypot(pins[j].lx - cx, pins[j].ly - cy);
			if (std::fabs(di - dj) > kEps) return di < dj;
			return pins[i].lx < pins[j].lx;
		});
		for (size_t k = 0; k < idx.size(); ++k) {
			a.index[idx[k]] = static_cast<int>(k);
		}
	}

	return a;
}

PinGridPin makePin(const PinRef &pr, const Annotations *ann, int row, int col,
                   const std::string &side, int index) {
	PinGridPin gp;
	gp.key = PinOverlayKey(*pr.pin);
	gp.id = ExportPinId(*pr.pin, pr.globalIndex);
	gp.number = pr.pin->number;
	gp.name = pr.pin->name;
	gp.displayLabel = PinDisplayLabel(*pr.pin, ann);
	gp.boardX = pr.bx;
	gp.boardY = pr.by;
	gp.localX = pr.lx;
	gp.localY = pr.ly;
	gp.row = row;
	gp.col = col;
	gp.side = side;
	gp.index = index;
	return gp;
}

} // namespace

bool InferPinGrid(const Board &board, const std::string &part, const Annotations *ann,
                  PinGridResult &out, std::string &errCode) {
	out = PinGridResult{};
	out.part = part;
	out.row0 = "min_local_y";
	out.col0 = "min_local_x";

	if (!FindComponent(board, part)) {
		errCode = "PART_NOT_FOUND";
		return false;
	}

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
		pr.bx = sp->position.x;
		pr.by = sp->position.y;
		pins.push_back(pr);
	}

	if (pins.empty()) {
		errCode = "PART_NO_PINS";
		return false;
	}
	errCode.clear();

	double cx = 0, cy = 0;
	const double theta = computePcaAngle(pins, cx, cy);
	out.centroidX = cx;
	out.centroidY = cy;
	out.rotationDeg = theta * 180.0 / M_PI;
	applyLocalFrame(pins, cx, cy, theta);

	if (pins.size() == 1) {
		out.kind = out.layout = "single";
		out.rows = out.cols = 1;
		out.fillRatio = 1;
		out.originX = pins[0].bx;
		out.originY = pins[0].by;
		out.pins.push_back(makePin(pins[0], ann, 0, 0, "", -1));
		return true;
	}

	const GridAssign grid = assignGridLocal(pins);
	const BBox bb = localBBox(pins);
	const double minPitch =
	    (grid.pitchX > kEps && grid.pitchY > kEps) ? std::min(grid.pitchX, grid.pitchY)
	    : (grid.pitchX > kEps)                       ? grid.pitchX
	    : (grid.pitchY > kEps)                       ? grid.pitchY
	                                                 : 0;
	const double minSide = std::max(std::min(bb.w(), bb.h()), kEps);
	double margin = 0.15 * minSide;
	if (minPitch > kEps) {
		margin = std::max(margin, 0.5 * minPitch);
	}

	double edgeFrac = 0, interiorFrac = 0;
	edgeStats(pins, bb, margin, edgeFrac, interiorFrac);
	const std::string layout =
	    classifyLayout(pins.size(), grid, edgeFrac, interiorFrac);

	out.kind = layout;
	out.layout = layout;
	out.rows = grid.rows;
	out.cols = grid.cols;
	out.pitchX = grid.pitchX;
	out.pitchY = grid.pitchY;
	// origin in board space: approx centroid for PCA frame; keep local col0/row0 as numbers in pitch
	out.originX = cx + grid.originX; // rough; clients use local for structure
	out.originY = cy + grid.originY;
	out.fillRatio = grid.fillRatio;

	if (grid.hasDup && layout != "peripheral") {
		out.warnings.push_back("duplicate_cells");
	}
	if (layout == "sparse") {
		out.warnings.push_back(
		    "fillRatio < 0.5; possible peripheral package (QFP/SOP), not a full matrix");
	}
	if (layout == "unordered") {
		out.warnings.push_back("could not classify pin layout");
	}
	if (std::fabs(out.rotationDeg) > 1e-6) {
		out.warnings.push_back("pca_rotation_applied");
	}

	PeriAssign peri;
	if (layout == "peripheral") {
		peri = assignPeripheral(pins, bb, margin, out.warnings);
	}

	out.pins.reserve(pins.size());
	for (size_t i = 0; i < pins.size(); ++i) {
		const std::string side = (layout == "peripheral") ? peri.side[i] : "";
		const int index = (layout == "peripheral") ? peri.index[i] : -1;
		out.pins.push_back(makePin(pins[i], ann, grid.row[i], grid.col[i], side, index));
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
	os << ",\"layout\":";
	appendEscaped(os, g.layout.empty() ? g.kind : g.layout);
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
	os << "},\"centroid\":{\"x\":";
	appendNumber(os, g.centroidX);
	os << ",\"y\":";
	appendNumber(os, g.centroidY);
	os << "},\"rotationDeg\":";
	appendNumber(os, g.rotationDeg);
	os << ",\"row0\":";
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
		os << "},\"local\":{\"x\":";
		appendNumber(os, p.localX);
		os << ",\"y\":";
		appendNumber(os, p.localY);
		os << "},\"row\":" << p.row << ",\"col\":" << p.col;
		os << ",\"side\":";
		appendEscaped(os, p.side);
		os << ",\"index\":" << p.index;
		os << '}';
	}
	os << "]}";
	return os.str();
}

} // namespace obv
