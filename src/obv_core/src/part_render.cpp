#include "obv_core/part_render.h"
#include "obv_core/pin_resolve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace obv {
namespace {

std::string trim(const std::string &s) {
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
		++b;
	size_t e = s.size();
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
		--e;
	return s.substr(b, e - b);
}

void expandPoint(double x, double y, double &minX, double &minY, double &maxX, double &maxY,
                 bool &any) {
	if (!any) {
		minX = maxX = x;
		minY = maxY = y;
		any = true;
		return;
	}
	minX = std::min(minX, x);
	minY = std::min(minY, y);
	maxX = std::max(maxX, x);
	maxY = std::max(maxY, y);
}

void expandPin(const Pin &pin, double &minX, double &minY, double &maxX, double &maxY, bool &any) {
	const double cx = static_cast<double>(pin.position.x);
	const double cy = static_cast<double>(pin.position.y);
	const double r = std::max(0.0, static_cast<double>(pin.diameter) * 0.5);
	const double hx = std::max(r, std::abs(static_cast<double>(pin.size.x)) * 0.5);
	const double hy = std::max(r, std::abs(static_cast<double>(pin.size.y)) * 0.5);
	// Also consider top/bottom rect sizes when present.
	const double thx = std::abs(static_cast<double>(pin.top_size.x)) * 0.5;
	const double thy = std::abs(static_cast<double>(pin.top_size.y)) * 0.5;
	const double bhx = std::abs(static_cast<double>(pin.bottom_size.x)) * 0.5;
	const double bhy = std::abs(static_cast<double>(pin.bottom_size.y)) * 0.5;
	const double halfX = std::max({hx, thx, bhx, 0.0});
	const double halfY = std::max({hy, thy, bhy, 0.0});
	expandPoint(cx - halfX, cy - halfY, minX, minY, maxX, maxY, any);
	expandPoint(cx + halfX, cy + halfY, minX, minY, maxX, maxY, any);
}

void expandOutline(const Component &c, double &minX, double &minY, double &maxX, double &maxY,
                   bool &any) {
	if (c.is_special_outline) {
		for (const auto &p : c.special_outline)
			expandPoint(p.x, p.y, minX, minY, maxX, maxY, any);
		return;
	}
	if (c.outline_done) {
		for (const auto &p : c.outline)
			expandPoint(p.x, p.y, minX, minY, maxX, maxY, any);
		return;
	}
	if (!c.hull.empty()) {
		for (const auto &p : c.hull)
			expandPoint(p.x, p.y, minX, minY, maxX, maxY, any);
	}
}

} // namespace

std::string PinDisplayLabel(const Pin &pin, const Annotations *ann) {
	if (ann && pin.component) {
		const std::string part = pin.component->name;
		const std::string key = PinOverlayKey(pin);
		auto pit = ann->partInfos.find(part);
		if (pit != ann->partInfos.end()) {
			auto kit = pit->second.pins.find(key);
			if (kit != pit->second.pins.end()) {
				const std::string ov = trim(kit->second.show_name);
				if (!ov.empty()) return ov;
			}
		}
	}
	if (!trim(pin.show_name).empty()) return trim(pin.show_name);
	if (!trim(pin.name).empty()) return trim(pin.name);
	return trim(pin.number);
}

bool ComputePartBounds(const Board &board, const std::string &part, double paddingOrAuto,
                       PartRenderBounds &out, std::string &errCode) {
	const Component *comp = FindComponent(board, part);
	if (!comp) {
		errCode = "PART_NOT_FOUND";
		return false;
	}

	double minX = 0, minY = 0, maxX = 0, maxY = 0;
	bool any = false;
	expandOutline(*comp, minX, minY, maxX, maxY, any);
	for (const auto &pp : comp->pins) {
		if (pp) expandPin(*pp, minX, minY, maxX, maxY, any);
	}

	if (!any) {
		errCode = "PART_NO_GEOMETRY";
		return false;
	}

	const double w = maxX - minX;
	const double h = maxY - minY;
	double pad = paddingOrAuto;
	if (pad < 0) {
		pad = 0.05 * std::max(w, h);
		if (pad < 1.0) pad = 1.0;
	}

	out.minX = minX - pad;
	out.minY = minY - pad;
	out.maxX = maxX + pad;
	out.maxY = maxY + pad;
	out.padding = pad;
	errCode.clear();
	return true;
}

bool BuildBoardToImage(const PartRenderBounds &b, const PartRenderOpts &opts, BoardToImage &out,
                       std::string &err) {
	const double w = b.maxX - b.minX;
	const double h = b.maxY - b.minY;
	if (!(w > 0) || !(h > 0)) {
		err = "empty bounds";
		return false;
	}
	int maxEdge = opts.maxEdge;
	if (maxEdge < 64) maxEdge = 64;
	if (maxEdge > 2048) maxEdge = 2048;
	double scale = opts.scale;
	if (scale <= 0) {
		scale = static_cast<double>(maxEdge) / std::max(w, h);
	}
	if (scale <= 0) {
		err = "bad scale";
		return false;
	}
	int iw = static_cast<int>(std::ceil(w * scale));
	int ih = static_cast<int>(std::ceil(h * scale));
	if (iw < 1) iw = 1;
	if (ih < 1) ih = 1;
	// clamp longest edge
	const int longEdge = std::max(iw, ih);
	if (longEdge > maxEdge) {
		const double f = static_cast<double>(maxEdge) / longEdge;
		scale *= f;
		iw = std::max(1, static_cast<int>(std::ceil(w * scale)));
		ih = std::max(1, static_cast<int>(std::ceil(h * scale)));
	}
	if (std::max(iw, ih) > 2048) {
		err = "image too large";
		return false;
	}
	out.originBoardX = b.minX;
	out.originBoardY = b.maxY; // top
	out.scale = scale;
	out.flipY = true;
	out.width = iw;
	out.height = ih;
	err.clear();
	return true;
}

void BoardToImagePoint(const BoardToImage &t, double boardX, double boardY, double &imageX,
                       double &imageY) {
	imageX = (boardX - t.originBoardX) * t.scale;
	if (t.flipY) {
		imageY = (t.originBoardY - boardY) * t.scale;
	} else {
		// originBoardY holds top when flipY; without flip treat as minY origin.
		imageY = (boardY - t.originBoardY) * t.scale;
	}
}

} // namespace obv
