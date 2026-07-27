#include "obv_core/board_json.h"
#include "obv_core/overlay_store.h"
#include "obv_core/parse.h"
#include "obv_core/filesystem_impl.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
// Minimal BRD-like fixture: prefer a real tiny sample under fixtures/
// If no binary fixture yet, test error path:
static void test_unrecognized_fails() {
	std::vector<char> buf = {'n', 'o', 'p', 'e'};
	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardBuffer(buf, "x.bin", keys);
	assert(!snap.ok());
	assert(!snap.error.empty());
}

// Optional: real board via env OBV_TEST_BOARD (no redistributable sample in repo).
static void test_parse_sample_ok_if_env() {
	if (const char *p = std::getenv("OBV_TEST_BOARD")) {
		obv::DecryptKeys keys;
		auto snap = obv::ParseBoardFile(p, keys);
		assert(snap.ok());
		assert(snap.error.empty());
		// Either pins or outline should be present for a real board.
		const bool has_pins = snap.board && !snap.board->Pins().empty();
		const bool has_outline =
		    snap.board &&
		    (!snap.board->OutlinePoints().empty() || !snap.board->OutlineSegments().empty());
		assert(has_pins || has_outline);
		std::cout << "OBV_TEST_BOARD ok: " << p << "\n";
	}
}

// Failed snapshots export empty JSON (documented choice: empty string, not error object).
static void test_export_failed_snap_empty() {
	std::vector<char> buf = {'n', 'o', 'p', 'e'};
	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardBuffer(buf, "x.bin", keys);
	assert(!snap.ok());
	assert(obv::ExportBoardJson(snap, "testid").empty());
	assert(obv::ExportMetaJson(snap, "testid").empty());
}

static void test_export_has_schema() {
	// Use env board or synthesize: if parse fails / no env, skip
	const char *p = std::getenv("OBV_TEST_BOARD");
	if (!p) {
		std::cout << "skip export\n";
		return;
	}
	obv::DecryptKeys keys;
	auto snap = obv::ParseBoardFile(p, keys);
	assert(snap.ok());
	auto js = obv::ExportBoardJson(snap, "testid");
	assert(js.find("\"boardSchemaVersion\":1") != std::string::npos);
	assert(js.find("\"pins\"") != std::string::npos);
	assert(js.find("\"boardId\":\"testid\"") != std::string::npos);

	auto meta = obv::ExportMetaJson(snap, "testid");
	assert(meta.find("\"boardSchemaVersion\":1") != std::string::npos);
	assert(meta.find("\"bounds\"") != std::string::npos);
	assert(meta.find("\"sides\"") != std::string::npos);
	// Meta must not dump full geometry arrays
	assert(meta.find("\"pins\"") == std::string::npos);
	assert(meta.find("\"tracks\"") == std::string::npos);
	std::cout << "export schema ok\n";
}

// YAML PartInfos/NetInfos round-trip via absolute temp paths (no flaky relative cwd).
static void test_overlay_yaml_roundtrip() {
	const auto dir = filesystem::temp_directory_path() / "obv_core_overlay_test";
	std::error_code ec;
	filesystem::create_directories(dir, ec);
	assert(!ec);

	const auto boardPath = dir / "board.brd";
	// Overlay sidecars: boardPath + ".yaml" / derived sqlite name.
	{
		std::ofstream touch(boardPath.string(), std::ios::trunc);
		assert(touch.good());
		touch << "x";
	}

	Annotations ann;
	ann.SetFilename(boardPath.string());
	auto &part = ann.NewPartInfo("U1");
	part.part_type = "ic";
	part.angle = PartAngle::_90;
	auto &pin = ann.NewPinInfo("U1", "1");
	pin.note = "reset";
	pin.voltage = "3.3";
	pin.voltage_flag = PinVoltageFlag::input;
	auto &net = ann.NewNetInfo("GND");
	net.showname = "Ground";
	net.note = "common";

	std::string err;
	assert(obv::SavePartNetYaml(boardPath, ann, err));
	assert(err.empty());

	const auto yamlPath = boardPath.string() + ".yaml";
	assert(filesystem::exists(yamlPath));
	{
		std::ifstream in(yamlPath);
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("0.0.2") != std::string::npos);
		assert(content.find("PartInfos") != std::string::npos);
		assert(content.find("NetInfos") != std::string::npos);
		assert(content.find("U1") != std::string::npos);
		assert(content.find("GND") != std::string::npos);
	}

	Annotations loaded;
	assert(obv::LoadOverlayForBoard(boardPath, loaded, err));
	assert(err.empty());
	assert(loaded.partInfos.count("U1") == 1);
	assert(loaded.partInfos["U1"].part_type == "ic");
	assert(loaded.partInfos["U1"].angle == PartAngle::_90);
	assert(loaded.partInfos["U1"].pins.count("1") == 1);
	assert(loaded.partInfos["U1"].pins["1"].note == "reset");
	assert(loaded.partInfos["U1"].pins["1"].voltage == "3.3");
	assert(loaded.partInfos["U1"].pins["1"].voltage_flag == PinVoltageFlag::input);
	assert(loaded.netInfos.count("GND") == 1);
	assert(loaded.netInfos["GND"].showname == "Ground");
	assert(loaded.netInfos["GND"].note == "common");

	// Empty save/load must not crash.
	Annotations empty;
	assert(obv::SavePartNetYaml(boardPath, empty, err));
	Annotations emptyLoaded;
	assert(obv::LoadOverlayForBoard(boardPath, emptyLoaded, err));
	assert(emptyLoaded.partInfos.empty());
	assert(emptyLoaded.netInfos.empty());

	// Restore rich maps for JSON export / ApplyOverlayJson (PUT parts/nets).
	assert(obv::SavePartNetYaml(boardPath, ann, err));
	Annotations forJson;
	assert(obv::LoadOverlayForBoard(boardPath, forJson, err));

	std::string js = obv::ExportOverlayJson(forJson);
	assert(js.find("\"partInfos\"") != std::string::npos);
	assert(js.find("\"netInfos\"") != std::string::npos);
	assert(js.find("\"annotations\"") != std::string::npos);
	assert(js.find("\"U1\"") != std::string::npos);
	assert(js.find("\"GND\"") != std::string::npos);
	assert(js.find("\"reset\"") != std::string::npos);

	Annotations applied;
	auto &old = applied.NewPartInfo("OLD");
	old.part_type = "gone";
	assert(obv::ApplyOverlayJson(applied, js, err));
	assert(err.empty());
	assert(applied.partInfos.count("OLD") == 0);
	assert(applied.partInfos.count("U1") == 1);
	assert(applied.partInfos["U1"].pins["1"].note == "reset");
	assert(applied.partInfos["U1"].pins["1"].voltage_flag == PinVoltageFlag::input);
	assert(applied.netInfos["GND"].showname == "Ground");

	// Apply with only netInfos leaves partInfos alone when key omitted.
	Annotations partial;
	partial.NewPartInfo("KEEP").part_type = "keep";
	assert(obv::ApplyOverlayJson(partial, std::string("{\"netInfos\":{\"N1\":{\"note\":\"n\"}}}"), err));
	assert(partial.partInfos.count("KEEP") == 1);
	assert(partial.netInfos.count("N1") == 1);
	assert(partial.netInfos["N1"].note == "n");

#ifdef HAVE_SQLITE3
	{
		Annotations sqlAnn;
		assert(obv::LoadOverlayForBoard(boardPath, sqlAnn, err));
		sqlAnn.Add(0, 10.0, 20.0, "NET1", "U1", "1", "hello sqlite");
		sqlAnn.GenerateList();
		assert(sqlAnn.annotations.size() == 1);
		assert(sqlAnn.annotations[0].note == "hello sqlite");
		assert(sqlAnn.annotations[0].x == 10.0);
		assert(sqlAnn.annotations[0].y == 20.0);

		Annotations reloaded;
		assert(obv::LoadOverlayForBoard(boardPath, reloaded, err));
		assert(reloaded.annotations.size() == 1);
		assert(reloaded.annotations[0].note == "hello sqlite");
		std::string js2 = obv::ExportOverlayJson(reloaded);
		assert(js2.find("hello sqlite") != std::string::npos);
		assert(js2.find("\"visible\":true") != std::string::npos);
		sqlAnn.Close();
		reloaded.Close();
	}
#endif

	// Cleanup temp files (best-effort)
	filesystem::remove_all(dir, ec);
	std::cout << "overlay yaml ok\n";
}

int main() {
	test_unrecognized_fails();
	test_parse_sample_ok_if_env();
	test_export_failed_snap_empty();
	test_export_has_schema();
	test_overlay_yaml_roundtrip();
	std::cout << "ok\n";
	return 0;
}
