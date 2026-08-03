#include "obv_core/board_json.h"
#include "obv_core/overlay_store.h"
#include "obv_core/pin_resolve.h"
#include "obv_core/parse.h"
#include "obv_core/filesystem_impl.h"
#include "obv_core/chip_store.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

void run_part_render_tests();
void run_pin_grid_tests();
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
	pin.show_name = "RST";
	pin.diode = "0.7";
	pin.note = "reset";
	pin.voltage = "3.3";
	pin.ohm = "10k";
	pin.ohm_black = "high";
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
	assert(loaded.partInfos["U1"].pins["1"].show_name == "RST");
	assert(loaded.partInfos["U1"].pins["1"].diode == "0.7");
	assert(loaded.partInfos["U1"].pins["1"].note == "reset");
	assert(loaded.partInfos["U1"].pins["1"].voltage == "3.3");
	assert(loaded.partInfos["U1"].pins["1"].ohm == "10k");
	assert(loaded.partInfos["U1"].pins["1"].ohm_black == "high");
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

static void test_operating_conditions_yaml_roundtrip() {
	const auto dir = filesystem::temp_directory_path() / "obv_core_oc_test";
	std::error_code ec;
	filesystem::create_directories(dir, ec);
	assert(!ec);
	const auto boardPath = dir / "board.brd";
	{
		std::ofstream touch(boardPath.string(), std::ios::trunc);
		assert(touch.good());
		touch << "x";
	}

	Annotations ann;
	ann.SetFilename(boardPath.string());
	auto &part = ann.NewPartInfo("U12");
	OperatingCondition oc;
	oc.id = "oc_01";
	oc.name = "UART0 TX path";
	oc.inputs = {"RXD0"};
	oc.outputs = {"TXD0"};
	oc.enables = {"UART_EN"};
	oc.note = "EN high, VCC 3.3";
	part.operating_conditions.push_back(oc);

	// Part with ONLY conditions (no pins/type) must still persist.
	auto &part2 = ann.NewPartInfo("U99");
	OperatingCondition oc2;
	oc2.id = "oc_a";
	oc2.outputs = {"Y"};
	part2.operating_conditions.push_back(oc2);

	std::string err;
	assert(obv::SavePartNetYaml(boardPath, ann, err));
	assert(err.empty());

	const auto yamlPath = boardPath.string() + ".yaml";
	{
		std::ifstream in(yamlPath);
		std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		assert(content.find("operating_conditions") != std::string::npos);
		assert(content.find("oc_01") != std::string::npos);
		assert(content.find("UART_EN") != std::string::npos);
		assert(content.find("U99") != std::string::npos);
	}

	Annotations loaded;
	assert(obv::LoadOverlayForBoard(boardPath, loaded, err));
	assert(loaded.partInfos.count("U12") == 1);
	assert(loaded.partInfos["U12"].operating_conditions.size() == 1);
	const auto &got = loaded.partInfos["U12"].operating_conditions[0];
	assert(got.id == "oc_01");
	assert(got.name == "UART0 TX path");
	assert(got.inputs.size() == 1 && got.inputs[0] == "RXD0");
	assert(got.outputs.size() == 1 && got.outputs[0] == "TXD0");
	assert(got.enables.size() == 1 && got.enables[0] == "UART_EN");
	assert(got.note == "EN high, VCC 3.3");
	assert(loaded.partInfos.count("U99") == 1);
	assert(loaded.partInfos["U99"].operating_conditions.size() == 1);
	assert(loaded.partInfos["U99"].operating_conditions[0].id == "oc_a");

	// Cleanup best-effort
	filesystem::remove_all(dir, ec);
	std::cout << "operating_conditions yaml ok\n";
}

static void test_operating_conditions_json_roundtrip() {
	Annotations ann;
	auto &part = ann.NewPartInfo("U12");
	OperatingCondition oc;
	oc.id = "oc_01";
	oc.name = "g1";
	oc.inputs = {"A"};
	oc.outputs = {"B", "C"};
	oc.enables = {"EN"};
	oc.note = "n";
	part.operating_conditions.push_back(oc);

	std::string js = obv::ExportOverlayJson(ann);
	assert(js.find("\"operating_conditions\"") != std::string::npos);
	assert(js.find("\"oc_01\"") != std::string::npos);
	assert(js.find("\"EN\"") != std::string::npos);

	Annotations applied;
	std::string err;
	assert(obv::ApplyOverlayJson(applied, js, err));
	assert(err.empty());
	assert(applied.partInfos["U12"].operating_conditions.size() == 1);
	assert(applied.partInfos["U12"].operating_conditions[0].outputs.size() == 2);
	assert(applied.partInfos["U12"].operating_conditions[0].outputs[1] == "C");
	std::cout << "operating_conditions json ok\n";
}

static void test_resolve_one_field_priority() {
	auto m = obv::ResolveOneField("ov", "bd", "prop", "R1", "2", "id2");
	assert(m.overlay == "ov");
	assert(m.board == "bd");
	assert(m.localValue == "ov");
	assert(m.localSource == obv::MeasureSource::Overlay);
	assert(m.effectiveValue == "ov");
	assert(m.effectiveSource == obv::MeasureSource::Overlay);

	m = obv::ResolveOneField("", "bd", "prop", "R1", "2", "id2");
	assert(m.localValue == "bd");
	assert(m.effectiveSource == obv::MeasureSource::Board);

	m = obv::ResolveOneField("", "", "prop", "R1", "2", "id2");
	assert(m.localSource == obv::MeasureSource::None);
	assert(m.effectiveValue == "prop");
	assert(m.effectiveSource == obv::MeasureSource::Propagated);
	assert(m.fromComponent == "R1");
	assert(m.fromPinKey == "2");
	assert(m.fromPinId == "id2");

	m = obv::ResolveOneField("", "", "", "R1", "2", "id2");
	assert(m.localSource == obv::MeasureSource::None);
	assert(m.effectiveSource == obv::MeasureSource::None);
	assert(m.effectiveValue.empty());
	std::cout << "resolve field priority ok\n";
}

static void test_normalize_condition() {
	OperatingCondition oc;
	oc.id = "  oc_1  ";
	oc.inputs = {" A ", "", "B"};
	oc.outputs = {"Y"};
	std::string err;
	assert(obv::NormalizeOperatingCondition(oc, err));
	assert(oc.id == "oc_1");
	assert(oc.inputs.size() == 2);
	assert(oc.inputs[0] == "A");
	assert(oc.inputs[1] == "B");
	assert(oc.outputs.size() == 1);
	assert(oc.outputs[0] == "Y");

	OperatingCondition bad;
	bad.id = std::string(65, 'a');
	assert(!obv::NormalizeOperatingCondition(bad, err));
	assert(!err.empty());

	OperatingCondition longLabel;
	longLabel.id = "ok";
	longLabel.inputs = {std::string(129, 'x')};
	assert(!obv::NormalizeOperatingCondition(longLabel, err));

	OperatingCondition tooMany;
	tooMany.id = "ok";
	tooMany.inputs.assign(257, "a");
	assert(!obv::NormalizeOperatingCondition(tooMany, err));
	std::cout << "normalize condition ok\n";
}

static void test_allocate_condition_id() {
	PartInfo p;
	OperatingCondition a;
	a.id = "oc_0001";
	p.operating_conditions.push_back(a);
	std::string id = obv::AllocateConditionId(p);
	assert(id != "oc_0001");
	assert(id.rfind("oc_", 0) == 0);

	// Deterministic next free oc_NNNN style
	assert(id == "oc_0002");

	OperatingCondition b;
	b.id = "oc_0002";
	p.operating_conditions.push_back(b);
	assert(obv::AllocateConditionId(p) == "oc_0003");

	PartInfo empty;
	assert(obv::AllocateConditionId(empty) == "oc_0001");
	std::cout << "allocate condition id ok\n";
}

static void test_pin_overlay_key() {
	Pin pin;
	pin.name = "A1";
	pin.number = "7";
	assert(obv::PinOverlayKey(pin) == "A1");
	pin.name.clear();
	assert(obv::PinOverlayKey(pin) == "7");
	pin.number.clear();
	assert(obv::PinOverlayKey(pin) == pin.UniqueId());
	std::cout << "pin overlay key ok\n";
}

static void test_sanitize_part_type() {
	std::string stem, err;
	assert(obv::SanitizePartTypeFilename("MP3398E", stem, err));
	assert(stem == "MP3398E");
	assert(obv::SanitizePartTypeFilename("  A/B\\C  ", stem, err));
	assert(stem == "A_B_C");
	assert(!obv::SanitizePartTypeFilename("   ", stem, err));
	// "..." keeps dots (allowed); only empty / "." / ".." after sanitize are rejected:
	assert(obv::SanitizePartTypeFilename("...", stem, err));
	assert(stem == "...");
	assert(!obv::SanitizePartTypeFilename(".", stem, err));
	assert(!obv::SanitizePartTypeFilename("..", stem, err));
	std::cout << "sanitize ok\n";
}

static void test_merge_conditions() {
	OperatingCondition b; b.id = "oc_b"; b.outputs = {"Y"};
	OperatingCondition c; c.id = "oc_c"; c.inputs = {"A"};
	std::vector<OperatingCondition> board{b}, chip{c};

	auto m1 = obv::MergeOperatingConditions(&board, &chip);
	assert(m1.source == obv::ConditionSource::Board);
	assert(m1.effective.size() == 1 && m1.effective[0].id == "oc_b");
	assert(m1.board.size() == 1 && m1.chip.size() == 1);

	std::vector<OperatingCondition> empty;
	auto m2 = obv::MergeOperatingConditions(&empty, &chip);
	assert(m2.source == obv::ConditionSource::Chip);
	assert(m2.effective[0].id == "oc_c");

	auto m3 = obv::MergeOperatingConditions(nullptr, nullptr);
	assert(m3.source == obv::ConditionSource::None);
	assert(m3.effective.empty());
	std::cout << "merge ok\n";
}

static void test_chip_yaml_roundtrip() {
	const auto dir = filesystem::temp_directory_path() / "obv_chip_store_test";
	std::error_code ec;
	filesystem::remove_all(dir, ec);
	filesystem::create_directories(dir, ec);
	assert(!ec);

	obv::ChipStore store(dir);
	obv::ChipRecord rec;
	rec.part_type = "MP3398E";
	rec.note = "led driver";
	OperatingCondition oc;
	oc.id = "oc_0001";
	oc.name = "pwm";
	oc.inputs = {"PWM"};
	oc.outputs = {"CH1"};
	oc.enables = {"EN"};
	oc.note = "en high";
	rec.operating_conditions.push_back(oc);

	std::string code, msg;
	assert(store.Put(rec, true, true, code, msg));

	obv::ChipRecord got;
	assert(store.Get("MP3398E", got, code, msg));
	assert(got.part_type == "MP3398E");
	assert(got.note == "led driver");
	assert(got.operating_conditions.size() == 1);
	assert(got.operating_conditions[0].inputs[0] == "PWM");

	std::vector<obv::ChipRecord> list;
	assert(store.List(list, code, msg));
	assert(list.size() == 1);

	// restart store instance
	obv::ChipStore store2(dir);
	assert(store2.Get("MP3398E", got, code, msg));
	assert(got.operating_conditions[0].id == "oc_0001");

	assert(store2.Delete("MP3398E", code, msg));
	assert(!store2.Get("MP3398E", got, code, msg));
	assert(code == "CHIP_NOT_FOUND");

	filesystem::remove_all(dir, ec);
	std::cout << "chip yaml ok\n";
}

static void test_allocate_from_vector() {
	std::vector<OperatingCondition> ocs;
	OperatingCondition a; a.id = "oc_0001";
	ocs.push_back(a);
	assert(obv::AllocateConditionId(ocs) == "oc_0002");
	std::cout << "allocate vector ok\n";
}

static void test_chip_pin_normalize_and_resolve() {
	obv::ChipPin p;
	p.id = "  B3  ";
	p.name = " VBAT ";
	p.aliases = {" BAT ", "", "VBAT"}; // VBAT dup with name → Validate fails later; normalize keeps
	p.dir = "power";
	std::string err;
	assert(obv::NormalizeChipPin(p, err));
	assert(p.id == "B3" && p.name == "VBAT");
	// aliases: empty dropped; "BAT" kept; "VBAT" still present until Validate
	p.aliases = {"BAT"};
	assert(obv::NormalizeChipPin(p, err));

	obv::ChipPin bad; bad.id = "X"; bad.dir = "nope";
	assert(!obv::NormalizeChipPin(bad, err));

	std::vector<obv::ChipPin> table;
	obv::ChipPin a; a.id = "B3"; a.name = "VBAT"; a.aliases = {"BAT"}; a.dir = "power";
	assert(obv::NormalizeChipPin(a, err));
	table.push_back(a);
	obv::ChipPin b; b.id = "H4"; b.name = "UART_TXD"; b.dir = "out";
	assert(obv::NormalizeChipPin(b, err));
	table.push_back(b);
	assert(obv::ValidateChipPinTable(table, err));

	obv::ChipPin c; c.id = "Z1"; c.name = "B3"; // name conflicts with a.id
	assert(obv::NormalizeChipPin(c, err));
	table.push_back(c);
	assert(!obv::ValidateChipPinTable(table, err));
	table.pop_back();

	obv::ChipRecord rec;
	rec.pins = table;
	auto r1 = obv::ResolveChipPin(rec, "B3");
	assert(r1.matched == obv::ChipPinMatch::Id && r1.pin && r1.pin->name == "VBAT");
	auto r2 = obv::ResolveChipPin(rec, "VBAT");
	assert(r2.matched == obv::ChipPinMatch::Name && r2.pin->id == "B3");
	auto r3 = obv::ResolveChipPin(rec, "BAT");
	assert(r3.matched == obv::ChipPinMatch::Alias);
	auto r4 = obv::ResolveChipPin(rec, "nope");
	assert(r4.matched == obv::ChipPinMatch::None && !r4.pin);
	std::cout << "chip pin resolve ok\n";
}

static void test_chip_pins_yaml_roundtrip() {
	const auto dir = filesystem::temp_directory_path() / "obv_chip_pins_test";
	std::error_code ec;
	filesystem::remove_all(dir, ec);
	filesystem::create_directories(dir, ec);
	obv::ChipStore store(dir);
	obv::ChipRecord rec;
	rec.part_type = "MP3398E";
	rec.note = "n";
	obv::ChipPin pin; pin.id = "B3"; pin.name = "VBAT"; pin.aliases = {"BAT"}; pin.dir = "power";
	rec.pins.push_back(pin);
	OperatingCondition oc; oc.id = "oc_0001"; oc.inputs = {"B3"};
	rec.operating_conditions.push_back(oc);
	std::string code, msg;
	assert(store.Put(rec, true, true, code, msg));

	obv::ChipRecord got;
	assert(store.Get("MP3398E", got, code, msg));
	assert(got.pins.size() == 1 && got.pins[0].name == "VBAT");
	assert(got.operating_conditions.size() == 1);

	// preserve pins when replacePins false
	got.note = "n2";
	got.pins.clear();
	assert(store.Put(got, false, false, code, msg));
	obv::ChipRecord got2;
	assert(store.Get("MP3398E", got2, code, msg));
	assert(got2.note == "n2");
	assert(got2.pins.size() == 1);
	assert(got2.operating_conditions.size() == 1);

	// clear pins
	got2.pins.clear();
	assert(store.Put(got2, false, true, code, msg));
	obv::ChipRecord got3;
	assert(store.Get("MP3398E", got3, code, msg));
	assert(got3.pins.empty());
	assert(got3.operating_conditions.size() == 1);

	filesystem::remove_all(dir, ec);
	std::cout << "chip pins yaml ok\n";
}

int main() {
	test_unrecognized_fails();
	test_parse_sample_ok_if_env();
	test_export_failed_snap_empty();
	test_export_has_schema();
	test_overlay_yaml_roundtrip();
	test_operating_conditions_yaml_roundtrip();
	test_operating_conditions_json_roundtrip();
	test_resolve_one_field_priority();
	test_normalize_condition();
	test_allocate_condition_id();
	test_pin_overlay_key();
	test_sanitize_part_type();
	test_merge_conditions();
	test_chip_yaml_roundtrip();
	test_allocate_from_vector();
	test_chip_pin_normalize_and_resolve();
	test_chip_pins_yaml_roundtrip();
	run_part_render_tests();
	run_pin_grid_tests();
	std::cout << "ok\n";
	return 0;
}
