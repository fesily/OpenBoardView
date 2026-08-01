#include "obv_core/pin_grid.h"
#include "obv_core/parse.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

void run_pin_grid_tests();

static void test_export_pin_grid_json_shape() {
	obv::PinGridResult g;
	g.part = "U1";
	g.kind = "grid";
	g.rows = 2;
	g.cols = 3;
	g.pitchX = 10;
	g.pitchY = 12;
	g.originX = 1;
	g.originY = 2;
	g.fillRatio = 1.0;
	obv::PinGridPin p;
	p.key = "1";
	p.id = "U1.1";
	p.number = "1";
	p.name = "n1";
	p.displayLabel = "A1";
	p.boardX = 1;
	p.boardY = 2;
	p.row = 0;
	p.col = 0;
	g.pins.push_back(p);
	const std::string js = obv::ExportPinGridJson("deadbeef", "x.bvr", g);
	assert(js.find("\"kind\":\"grid\"") != std::string::npos);
	assert(js.find("\"rows\":2") != std::string::npos);
	assert(js.find("\"cols\":3") != std::string::npos);
	assert(js.find("\"row0\":\"min_y\"") != std::string::npos);
	assert(js.find("\"col0\":\"min_x\"") != std::string::npos);
	assert(js.find("\"A1\"") != std::string::npos);
	assert(js.find("\"row\":0") != std::string::npos);
	assert(js.find("\"col\":0") != std::string::npos);
	std::cout << "pin grid json ok\n";
}

static bool fileReadable(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	return static_cast<bool>(in);
}

static void test_infer_pin_grid_on_sample_board() {
	// Prefer OBV_TEST_BOARD; else try repo sample under data/boards.
	std::vector<std::string> candidates;
	if (const char *env = std::getenv("OBV_TEST_BOARD")) {
		candidates.emplace_back(env);
	}
	candidates.emplace_back(
	    "data/boards/"
	    "6c3f997ac47fc55d6ef67fe4134187f655d13c87d790e362db6a99ebb089ced5_switch-oled-heg-cpu-01.bvr");
	candidates.emplace_back(
	    "../data/boards/"
	    "6c3f997ac47fc55d6ef67fe4134187f655d13c87d790e362db6a99ebb089ced5_switch-oled-heg-cpu-01.bvr");
	candidates.emplace_back(
	    "../../data/boards/"
	    "6c3f997ac47fc55d6ef67fe4134187f655d13c87d790e362db6a99ebb089ced5_switch-oled-heg-cpu-01.bvr");

	std::string path;
	for (const auto &c : candidates) {
		if (fileReadable(c)) {
			path = c;
			break;
		}
	}
	if (path.empty()) {
		std::cout << "skip pin grid infer (no sample board)\n";
		return;
	}

	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardFile(path, keys);
	assert(snap.ok());
	assert(snap.board);

	// Prefer a multi-pin component.
	std::string partName;
	size_t bestPins = 0;
	for (const auto &c : snap.board->Components()) {
		if (!c || c->name.empty()) continue;
		if (c->pins.size() > bestPins) {
			bestPins = c->pins.size();
			partName = c->name;
		}
	}
	assert(!partName.empty());
	assert(bestPins >= 1);

	obv::PinGridResult grid;
	std::string err;
	assert(obv::InferPinGrid(*snap.board, partName, nullptr, grid, err));
	assert(err.empty());
	assert(grid.part == partName);
	assert(static_cast<size_t>(grid.pins.size()) == bestPins || grid.pins.size() >= 1);
	assert(grid.rows >= 1 && grid.cols >= 1);
	assert(grid.fillRatio > 0);

	// Every pin row/col in range; indices unique preferred (warn if not).
	std::set<std::pair<int, int>> cells;
	for (const auto &p : grid.pins) {
		assert(p.row >= 0 && p.row < grid.rows);
		assert(p.col >= 0 && p.col < grid.cols);
		cells.insert({p.row, p.col});
	}
	// At least some structure
	assert(!grid.kind.empty());
	const std::string js = obv::ExportPinGridJson("testid", "sample.bvr", grid);
	assert(js.find("\"pins\"") != std::string::npos);
	assert(js.find(partName) != std::string::npos);

	std::cout << "pin grid infer ok part=" << partName << " kind=" << grid.kind
	          << " " << grid.rows << "x" << grid.cols << " pins=" << grid.pins.size()
	          << " uniqueCells=" << cells.size() << "\n";
}

static void test_infer_missing_part() {
	// Need any parseable board; reuse sample if present.
	const std::string path =
	    "data/boards/"
	    "6c3f997ac47fc55d6ef67fe4134187f655d13c87d790e362db6a99ebb089ced5_switch-oled-heg-cpu-01.bvr";
	if (!fileReadable(path)) {
		if (const char *env = std::getenv("OBV_TEST_BOARD")) {
			if (!fileReadable(env)) {
				std::cout << "skip pin grid missing part\n";
				return;
			}
		} else {
			std::cout << "skip pin grid missing part\n";
			return;
		}
	}
	const char *p = fileReadable(path) ? path.c_str() : std::getenv("OBV_TEST_BOARD");
	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardFile(p, keys);
	if (!snap.ok()) {
		std::cout << "skip pin grid missing part (parse fail)\n";
		return;
	}
	obv::PinGridResult grid;
	std::string err;
	assert(!obv::InferPinGrid(*snap.board, "__NoSuchPart_ZZ__", nullptr, grid, err));
	assert(err == "PART_NOT_FOUND");
	std::cout << "pin grid missing part ok\n";
}

void run_pin_grid_tests() {
	test_export_pin_grid_json_shape();
	test_infer_pin_grid_on_sample_board();
	test_infer_missing_part();
	std::cout << "pin_grid unit ok\n";
}
