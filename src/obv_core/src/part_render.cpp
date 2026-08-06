#include "obv_core/part_render.h"
#include "obv_core/pin_resolve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace obv {
namespace {

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

const char *shapeToStringLocal(EShapeType t) {
	switch (t) {
		case kShapeTypeRect: return "rect";
		case kShapeTypeFold: return "fold";
		case kShapeTypeCircle:
		default: return "circle";
	}
}

const char *pinTypeToStringLocal(Pin::EPinType t) {
	switch (t) {
		case Pin::kPinTypeNotConnected: return "not_connected";
		case Pin::kPinTypeComponent: return "component";
		case Pin::kPinTypeVia: return "via";
		case Pin::kPinTypeTestPad: return "test_pad";
		case Pin::kPinTypeUnkown:
		default: return "unknown";
	}
}

// --- Raster primitives ---

struct Rgba {
	std::vector<uint8_t> px;
	int w = 0, h = 0;
};

struct Color {
	uint8_t r, g, b, a;
};

void putPixel(Rgba &img, int x, int y, Color c) {
	if (x < 0 || y < 0 || x >= img.w || y >= img.h) return;
	const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(img.w) + static_cast<size_t>(x)) * 4;
	if (c.a == 255) {
		img.px[i + 0] = c.r;
		img.px[i + 1] = c.g;
		img.px[i + 2] = c.b;
		img.px[i + 3] = 255;
		return;
	}
	if (c.a == 0) return;
	const float sa = c.a / 255.f;
	const float da = img.px[i + 3] / 255.f;
	const float outA = sa + da * (1.f - sa);
	if (outA <= 0.f) return;
	auto blend = [&](uint8_t s, uint8_t d) -> uint8_t {
		const float v = (s * sa + d * da * (1.f - sa)) / outA;
		return static_cast<uint8_t>(std::min(255.f, std::max(0.f, v + 0.5f)));
	};
	img.px[i + 0] = blend(c.r, img.px[i + 0]);
	img.px[i + 1] = blend(c.g, img.px[i + 1]);
	img.px[i + 2] = blend(c.b, img.px[i + 2]);
	img.px[i + 3] = static_cast<uint8_t>(std::min(255.f, outA * 255.f + 0.5f));
}

void fillRect(Rgba &img, int x0, int y0, int x1, int y1, Color c) {
	if (x0 > x1) std::swap(x0, x1);
	if (y0 > y1) std::swap(y0, y1);
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(img.w - 1, x1);
	y1 = std::min(img.h - 1, y1);
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
			putPixel(img, x, y, c);
}

void fillSolid(Rgba &img, Color c) {
	for (int y = 0; y < img.h; ++y)
		for (int x = 0; x < img.w; ++x)
			putPixel(img, x, y, c);
}

void fillCircle(Rgba &img, double cx, double cy, double radius, Color c) {
	if (radius <= 0) radius = 0.5;
	const int x0 = static_cast<int>(std::floor(cx - radius));
	const int x1 = static_cast<int>(std::ceil(cx + radius));
	const int y0 = static_cast<int>(std::floor(cy - radius));
	const int y1 = static_cast<int>(std::ceil(cy + radius));
	const double r2 = radius * radius;
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			const double dx = (x + 0.5) - cx;
			const double dy = (y + 0.5) - cy;
			if (dx * dx + dy * dy <= r2) putPixel(img, x, y, c);
		}
	}
}

void fillRotatedRect(Rgba &img, double cx, double cy, double halfW, double halfH, double angleDeg,
                     Color c) {
	if (halfW <= 0) halfW = 0.5;
	if (halfH <= 0) halfH = 0.5;
	// angle in degrees (board pin.angle); flipY image space: rotate same way in screen
	const double rad = angleDeg * 3.14159265358979323846 / 180.0;
	const double cosA = std::cos(rad);
	const double sinA = std::sin(rad);
	const double extent = std::hypot(halfW, halfH) + 1.0;
	const int x0 = static_cast<int>(std::floor(cx - extent));
	const int x1 = static_cast<int>(std::ceil(cx + extent));
	const int y0 = static_cast<int>(std::floor(cy - extent));
	const int y1 = static_cast<int>(std::ceil(cy + extent));
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			const double dx = (x + 0.5) - cx;
			const double dy = (y + 0.5) - cy;
			// rotate by -angle into local
			const double lx = dx * cosA + dy * sinA;
			const double ly = -dx * sinA + dy * cosA;
			if (std::abs(lx) <= halfW && std::abs(ly) <= halfH) putPixel(img, x, y, c);
		}
	}
}

// Scanline polygon fill (even-odd). pts are image-space.
void fillPolygon(Rgba &img, const std::vector<std::pair<double, double>> &pts, Color c) {
	if (pts.size() < 3) return;
	double minY = pts[0].second, maxY = pts[0].second;
	for (const auto &p : pts) {
		minY = std::min(minY, p.second);
		maxY = std::max(maxY, p.second);
	}
	const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
	const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(maxY)));
	const size_t n = pts.size();
	for (int y = y0; y <= y1; ++y) {
		const double yscan = y + 0.5;
		std::vector<double> xs;
		xs.reserve(n);
		for (size_t i = 0; i < n; ++i) {
			const auto &a = pts[i];
			const auto &b = pts[(i + 1) % n];
			if ((a.second <= yscan && b.second > yscan) || (b.second <= yscan && a.second > yscan)) {
				const double t = (yscan - a.second) / (b.second - a.second);
				xs.push_back(a.first + t * (b.first - a.first));
			}
		}
		std::sort(xs.begin(), xs.end());
		for (size_t i = 0; i + 1 < xs.size(); i += 2) {
			const int xa = static_cast<int>(std::floor(xs[i]));
			const int xb = static_cast<int>(std::ceil(xs[i + 1]));
			for (int x = xa; x <= xb; ++x) putPixel(img, x, y, c);
		}
	}
}

void strokePolygon(Rgba &img, const std::vector<std::pair<double, double>> &pts, Color c,
                   double thickness) {
	if (pts.size() < 2) return;
	const double half = std::max(0.5, thickness * 0.5);
	const size_t n = pts.size();
	for (size_t i = 0; i < n; ++i) {
		const auto &a = pts[i];
		const auto &b = pts[(i + 1) % n];
		const double dx = b.first - a.first;
		const double dy = b.second - a.second;
		const double len = std::hypot(dx, dy);
		if (len < 1e-9) continue;
		const double nx = -dy / len * half;
		const double ny = dx / len * half;
		std::vector<std::pair<double, double>> quad = {
		    {a.first + nx, a.second + ny},
		    {a.first - nx, a.second - ny},
		    {b.first - nx, b.second - ny},
		    {b.first + nx, b.second + ny},
		};
		fillPolygon(img, quad, c);
		fillCircle(img, a.first, a.second, half, c);
	}
}

// 5x7 bitmap font (ASCII 32-126); bits top-to-bottom, LSB = top.
// Each glyph: 5 columns of 7 bits.
static const uint8_t kFont5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x04, 0x08, 0x10, 0x08}, // ~
};

void drawGlyph(Rgba &img, int x, int y, char ch, Color c, int scale) {
	if (ch < 32 || ch > 126) ch = '?';
	const uint8_t *cols = kFont5x7[ch - 32];
	for (int col = 0; col < 5; ++col) {
		const uint8_t bits = cols[col];
		for (int row = 0; row < 7; ++row) {
			if (bits & (1u << row)) {
				for (int dy = 0; dy < scale; ++dy)
					for (int dx = 0; dx < scale; ++dx)
						putPixel(img, x + col * scale + dx, y + row * scale + dy, c);
			}
		}
	}
}

void drawText5x7(Rgba &img, double cx, double cy, const std::string &text, Color fg, Color halo,
                 int scale) {
	if (text.empty() || scale < 1) return;
	const int gw = 6 * scale; // 5 + 1 spacing
	const int gh = 7 * scale;
	const int totalW = static_cast<int>(text.size()) * gw - scale;
	int x0 = static_cast<int>(std::lround(cx - totalW * 0.5));
	int y0 = static_cast<int>(std::lround(cy - gh * 0.5));
	// Halo offsets
	static const int ox[] = {-1, 0, 1, -1, 1, -1, 0, 1};
	static const int oy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
	for (size_t i = 0; i < text.size(); ++i) {
		const int gx = x0 + static_cast<int>(i) * gw;
		for (int k = 0; k < 8; ++k)
			drawGlyph(img, gx + ox[k] * scale, y0 + oy[k] * scale, text[i], halo, scale);
		drawGlyph(img, gx, y0, text[i], fg, scale);
	}
}

std::vector<std::pair<double, double>> componentOutlineImagePts(const Component &c,
                                                                const BoardToImage &t) {
	std::vector<std::pair<double, double>> pts;
	auto add = [&](double bx, double by) {
		double ix, iy;
		BoardToImagePoint(t, bx, by, ix, iy);
		pts.emplace_back(ix, iy);
	};
	if (c.is_special_outline) {
		for (const auto &p : c.special_outline) add(p.x, p.y);
	} else if (c.outline_done) {
		for (const auto &p : c.outline) add(p.x, p.y);
	} else if (!c.hull.empty()) {
		for (const auto &p : c.hull) add(p.x, p.y);
	}
	return pts;
}

bool isOrientationPin(const Pin &pin, size_t pinCount, const Annotations *ann) {
	if (PinDisplayLabel(pin, ann) == "A1") return true;
	if (pin.number == "1" && pinCount >= 3) return true;
	return false;
}

std::string overlayShowNameFor(const Pin &pin, const Annotations &ann) {
	if (!pin.component) return {};
	const std::string part = pin.component->name;
	const std::string key = PinOverlayKey(pin);
	auto pit = ann.partInfos.find(part);
	if (pit == ann.partInfos.end()) return {};
	auto kit = pit->second.pins.find(key);
	if (kit == pit->second.pins.end()) return {};
	return trim(kit->second.show_name);
}

size_t pinGlobalIndex(const Board &board, const Pin *pin) {
	const auto &pins = const_cast<Board &>(board).Pins();
	for (size_t i = 0; i < pins.size(); ++i) {
		if (pins[i].get() == pin) return i;
	}
	return 0;
}

void stbiWriteAppend(void *context, void *data, int size) {
	auto *out = static_cast<std::string *>(context);
	const char *bytes = static_cast<const char *>(data);
	out->append(bytes, bytes + size);
}

// Colors
constexpr Color kBg{0x1a, 0x1d, 0x24, 0xff};
constexpr Color kOutlineFill{80, 90, 110, 89}; // ~0.35 alpha
constexpr Color kOutlineStroke{0x8b, 0x93, 0xa7, 0xff};
constexpr Color kPad{0x6e, 0xc6, 0xff, 0xff};
constexpr Color kPadA1{0xdd, 0x00, 0x00, 0xff};
constexpr Color kLabel{0xe8, 0xea, 0xed, 0xff};
constexpr Color kLabelHalo{0x00, 0x00, 0x00, 0xff};
constexpr Color kPartName{0xc5, 0xca, 0xd3, 0xff};

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

bool EncodePng(int width, int height, const unsigned char *rgba, std::string &outPng) {
	outPng.clear();
	if (width <= 0 || height <= 0 || !rgba) return false;
	const int ok = stbi_write_png_to_func(stbiWriteAppend, &outPng, width, height, 4, rgba, width * 4);
	return ok != 0 && outPng.size() >= 8;
}

bool RenderPartScreenshot(const Board &board, const Annotations &ann, const std::string &part,
                          const PartRenderOpts &opts, PartScreenshotResult &out, std::string &errCode,
                          std::string &errMessage) {
	out = PartScreenshotResult{};
	errCode.clear();
	errMessage.clear();

	PartRenderBounds bounds;
	if (!ComputePartBounds(board, part, opts.padding, bounds, errCode)) {
		errMessage = errCode == "PART_NOT_FOUND" ? "part not found" : "part has no geometry";
		return false;
	}

	BoardToImage t;
	std::string buildErr;
	if (!BuildBoardToImage(bounds, opts, t, buildErr)) {
		errCode = "BAD_REQUEST";
		errMessage = buildErr.empty() ? "invalid render options" : buildErr;
		return false;
	}

	const Component *comp = FindComponent(board, part);
	if (!comp) {
		errCode = "PART_NOT_FOUND";
		errMessage = "part not found";
		return false;
	}

	Rgba img;
	img.w = t.width;
	img.h = t.height;
	try {
		img.px.assign(static_cast<size_t>(img.w) * static_cast<size_t>(img.h) * 4u, 0);
	} catch (...) {
		errCode = "RENDER_FAILED";
		errMessage = "out of memory";
		return false;
	}

	// 1. Background
	fillSolid(img, kBg);

	// 2. Outline fill + stroke
	auto outlinePts = componentOutlineImagePts(*comp, t);
	if (outlinePts.size() >= 3) {
		fillPolygon(img, outlinePts, kOutlineFill);
		strokePolygon(img, outlinePts, kOutlineStroke, 1.5);
	}

	// 3. Pads + pin meta
	const size_t pinCount = comp->pins.size();
	out.meta.pins.reserve(pinCount);
	for (const auto &pp : comp->pins) {
		if (!pp) continue;
		const Pin &pin = *pp;
		double ix, iy;
		BoardToImagePoint(t, pin.position.x, pin.position.y, ix, iy);

		const Color padColor = isOrientationPin(pin, pinCount, &ann) ? kPadA1 : kPad;
		const double diamBoard = std::max(0.0, static_cast<double>(pin.diameter));
		double halfW = diamBoard * 0.5 * t.scale;
		double halfH = halfW;
		if (pin.shape == kShapeTypeRect) {
			halfW = std::max(0.5, std::abs(static_cast<double>(pin.size.x)) * 0.5 * t.scale);
			halfH = std::max(0.5, std::abs(static_cast<double>(pin.size.y)) * 0.5 * t.scale);
			if (halfW < 0.5 && diamBoard > 0) halfW = diamBoard * 0.5 * t.scale;
			if (halfH < 0.5 && diamBoard > 0) halfH = diamBoard * 0.5 * t.scale;
			// board pin.angle is degrees; 90/270 swap handled by rotation
			fillRotatedRect(img, ix, iy, halfW, halfH, static_cast<double>(pin.angle), padColor);
		} else {
			// circle / fold / default
			double r = halfW;
			if (r < 0.5) {
				// fallback from size
				r = std::max(0.5, std::max(std::abs(static_cast<double>(pin.size.x)),
				                           std::abs(static_cast<double>(pin.size.y))) *
				                      0.5 * t.scale);
			}
			if (r < 1.0) r = 1.0; // visibility floor
			fillCircle(img, ix, iy, r, padColor);
		}

		PartPinMeta pm;
		pm.key = PinOverlayKey(pin);
		pm.id = ExportPinId(pin, pinGlobalIndex(board, &pin));
		pm.number = pin.number;
		pm.name = pin.name;
		pm.boardShowName = pin.show_name;
		pm.overlayShowName = overlayShowNameFor(pin, ann);
		pm.displayLabel = PinDisplayLabel(pin, &ann);
		pm.boardX = pin.position.x;
		pm.boardY = pin.position.y;
		pm.imageX = ix;
		pm.imageY = iy;
		pm.type = pinTypeToStringLocal(pin.type);
		pm.shape = shapeToStringLocal(pin.shape);
		pm.diameter = diamBoard;
		pm.netName = pin.net ? pin.net->name : std::string{};
		out.meta.pins.push_back(std::move(pm));
	}

	// 4. Labels
	if (opts.labels) {
		const int fontScale = t.width >= 400 ? 2 : 1;
		for (const auto &pm : out.meta.pins) {
			if (pm.displayLabel.empty()) continue;
			drawText5x7(img, pm.imageX, pm.imageY, pm.displayLabel, kLabel, kLabelHalo, fontScale);
		}
	}

	// 5. Part name near top of image
	if (opts.partName && !part.empty()) {
		const int fontScale = t.width >= 400 ? 2 : 1;
		const double ty = 4.0 + 3.5 * fontScale;
		drawText5x7(img, t.width * 0.5, ty, part, kPartName, kLabelHalo, fontScale);
	}

	// 6. Encode PNG
	if (!EncodePng(img.w, img.h, img.px.data(), out.png)) {
		errCode = "RENDER_FAILED";
		errMessage = "png encode failed";
		return false;
	}

	// 7. Meta
	out.meta.part = part;
	out.meta.transform = t;
	out.meta.bounds = bounds;
	out.meta.optsUsed = opts;
	out.meta.optsUsed.scale = t.scale;
	out.meta.optsUsed.padding = bounds.padding;
	out.meta.optsUsed.maxEdge = opts.maxEdge;
	if (out.meta.optsUsed.maxEdge < 64) out.meta.optsUsed.maxEdge = 64;
	if (out.meta.optsUsed.maxEdge > 2048) out.meta.optsUsed.maxEdge = 2048;
	return true;
}

std::string ExportPartScreenshotMetaJson(const std::string &boardId,
                                         const std::string &sourceName,
                                         const PartScreenshotMeta &meta) {
	std::ostringstream os;
	os << "{\"boardId\":";
	appendEscaped(os, boardId);
	os << ",\"sourceName\":";
	appendEscaped(os, sourceName);
	os << ",\"part\":";
	appendEscaped(os, meta.part);

	// image
	os << ",\"image\":{";
	os << "\"width\":" << meta.transform.width;
	os << ",\"height\":" << meta.transform.height;
	os << ",\"scale\":";
	appendNumber(os, meta.optsUsed.scale != 0 ? meta.optsUsed.scale : meta.transform.scale);
	os << ",\"padding\":";
	appendNumber(os, meta.optsUsed.padding >= 0 ? meta.optsUsed.padding : meta.bounds.padding);
	os << ",\"labels\":" << (meta.optsUsed.labels ? "true" : "false");
	os << ",\"partName\":" << (meta.optsUsed.partName ? "true" : "false");
	os << '}';

	// boardBounds
	os << ",\"boardBounds\":{";
	os << "\"minX\":";
	appendNumber(os, meta.bounds.minX);
	os << ",\"minY\":";
	appendNumber(os, meta.bounds.minY);
	os << ",\"maxX\":";
	appendNumber(os, meta.bounds.maxX);
	os << ",\"maxY\":";
	appendNumber(os, meta.bounds.maxY);
	os << '}';

	// transform.boardToImage
	os << ",\"transform\":{\"boardToImage\":{";
	os << "\"originBoardX\":";
	appendNumber(os, meta.transform.originBoardX);
	os << ",\"originBoardY\":";
	appendNumber(os, meta.transform.originBoardY);
	os << ",\"scale\":";
	appendNumber(os, meta.transform.scale);
	os << ",\"flipY\":" << (meta.transform.flipY ? "true" : "false");
	os << "}}";

	// pins
	os << ",\"pins\":[";
	for (size_t i = 0; i < meta.pins.size(); ++i) {
		const PartPinMeta &p = meta.pins[i];
		if (i) os << ',';
		os << '{';
		os << "\"key\":";
		appendEscaped(os, p.key);
		os << ",\"id\":";
		appendEscaped(os, p.id);
		os << ",\"number\":";
		appendEscaped(os, p.number);
		os << ",\"name\":";
		appendEscaped(os, p.name);
		os << ",\"boardShowName\":";
		appendEscaped(os, p.boardShowName);
		os << ",\"overlayShowName\":";
		appendEscaped(os, p.overlayShowName);
		os << ",\"displayLabel\":";
		appendEscaped(os, p.displayLabel);
		os << ",\"board\":{\"x\":";
		appendNumber(os, p.boardX);
		os << ",\"y\":";
		appendNumber(os, p.boardY);
		os << "},\"image\":{\"x\":";
		appendNumber(os, p.imageX);
		os << ",\"y\":";
		appendNumber(os, p.imageY);
		os << "},\"type\":";
		appendEscaped(os, p.type);
		os << ",\"shape\":";
		appendEscaped(os, p.shape);
		os << ",\"diameter\":";
		appendNumber(os, p.diameter);
		os << ",\"netName\":";
		appendEscaped(os, p.netName);
		os << '}';
	}
	os << "]}";
	return os.str();
}


} // namespace obv
