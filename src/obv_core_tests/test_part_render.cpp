#include "obv_core/part_render.h"
#include "annotations.h"
#include "Board.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

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

void run_part_render_tests() {
	test_pin_display_label_priority();
	test_board_to_image_flip_y();
	// ComputePartBounds needs a real Board — covered in Task 2 with OBV_TEST_BOARD or synthetic if available
	std::cout << "part_render unit ok\n";
}
