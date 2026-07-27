#include "obv_core/board_json.h"
#include "obv_core/parse.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
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

int main() {
	test_unrecognized_fails();
	test_parse_sample_ok_if_env();
	test_export_failed_snap_empty();
	test_export_has_schema();
	std::cout << "ok\n";
	return 0;
}
