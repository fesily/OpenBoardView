#include "obv_core/part_render.h"
#include "obv_core/parse.h"
#include "annotations.h"
#include "Board.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static void test_pin_display_label_priority() {
	Pin pin;
	pin.name = "netish";
	pin.number = "3";
	pin.show_name = "BOARD_SN";

	// no overlay → board show_name
	assert(obv::PinDisplayLabel(pin, nullptr) == "BOARD_SN");

	pin.show_name.clear();
	assert(obv::PinDisplayLabel(pin, nullptr) == "netish");

	pin.name.clear();
	assert(obv::PinDisplayLabel(pin, nullptr) == "3");

	// overlay wins
	pin.name = "netish";
	pin.show_name = "BOARD_SN";
	pin.component = std::make_shared<Component>();
	pin.component->name = "U1";
	Annotations ann;
	auto &pi = ann.NewPinInfo("U1", "netish"); // key = name
	pi.show_name = "OV_SN";
	assert(obv::PinDisplayLabel(pin, &ann) == "OV_SN");

	// empty overlay falls through
	pi.show_name = "  ";
	assert(obv::PinDisplayLabel(pin, &ann) == "BOARD_SN");
	std::cout << "pin display label ok\n";
}

static void test_board_to_image_flip_y() {
	obv::PartRenderBounds b;
	b.minX = 0;
	b.minY = 0;
	b.maxX = 100;
	b.maxY = 50;
	b.padding = 0;
	obv::PartRenderOpts opts;
	opts.scale = 2.0;
	opts.maxEdge = 2048;
	obv::BoardToImage t;
	std::string err;
	assert(obv::BuildBoardToImage(b, opts, t, err));
	assert(t.width == 200);
	assert(t.height == 100);
	assert(t.flipY);
	double ix, iy;
	obv::BoardToImagePoint(t, 0, 50, ix, iy); // top of board box → y=0
	assert(std::fabs(ix - 0) < 1e-6);
	assert(std::fabs(iy - 0) < 1e-6);
	obv::BoardToImagePoint(t, 100, 0, ix, iy); // bottom-right
	assert(std::fabs(ix - 200) < 1e-6);
	assert(std::fabs(iy - 100) < 1e-6);
	std::cout << "board to image ok\n";
}

static void test_encode_png_tiny_buffer() {
	// Always-run unit path: synthetic 2x2 RGBA → valid PNG
	std::vector<unsigned char> rgba(2 * 2 * 4, 0);
	rgba[0] = 0x1a;
	rgba[1] = 0x1d;
	rgba[2] = 0x24;
	rgba[3] = 0xff;
	std::string png;
	assert(obv::EncodePng(2, 2, rgba.data(), png));
	assert(png.size() >= 8);
	assert(static_cast<unsigned char>(png[0]) == 0x89);
	assert(png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
	std::cout << "encode png tiny ok bytes=" << png.size() << "\n";
}

static void test_render_part_not_found() {
	const char *p = std::getenv("OBV_TEST_BOARD");
	if (!p) {
		std::cout << "skip render part not found (no OBV_TEST_BOARD)\n";
		return;
	}
	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardFile(p, keys);
	assert(snap.ok());
	Annotations ann;
	obv::PartRenderOpts opts;
	obv::PartScreenshotResult r;
	std::string code, msg;
	assert(!obv::RenderPartScreenshot(*snap.board, ann, "__no_such_part__", opts, r, code, msg));
	assert(code == "PART_NOT_FOUND");
	std::cout << "render part not found ok\n";
}

static void test_render_part_png_if_env() {
	const char *p = std::getenv("OBV_TEST_BOARD");
	if (!p) {
		std::cout << "skip part render png\n";
		return;
	}
	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardFile(p, keys);
	assert(snap.ok());
	std::string partName;
	for (const auto &c : snap.board->Components()) {
		if (c && !c->name.empty() && !c->pins.empty()) {
			partName = c->name;
			break;
		}
	}
	assert(!partName.empty());
	Annotations ann;
	obv::PartRenderOpts opts;
	opts.maxEdge = 256;
	obv::PartScreenshotResult r;
	std::string code, msg;
	assert(obv::RenderPartScreenshot(*snap.board, ann, partName, opts, r, code, msg));
	assert(r.png.size() >= 8);
	assert(static_cast<unsigned char>(r.png[0]) == 0x89);
	assert(r.png[1] == 'P' && r.png[2] == 'N' && r.png[3] == 'G');
	assert(r.meta.transform.width > 0 && r.meta.transform.height > 0);
	assert(!r.meta.pins.empty());
	for (const auto &pm : r.meta.pins) {
		assert(pm.imageX >= -1 && pm.imageX <= r.meta.transform.width + 1);
		assert(pm.imageY >= -1 && pm.imageY <= r.meta.transform.height + 1);
	}
	std::cout << "part render png ok part=" << partName << " bytes=" << r.png.size() << "\n";
}

void run_part_render_tests() {
	test_pin_display_label_priority();
	test_board_to_image_flip_y();
	test_encode_png_tiny_buffer();
	test_render_part_not_found();
	test_render_part_png_if_env();
	std::cout << "part_render unit ok\n";
}
