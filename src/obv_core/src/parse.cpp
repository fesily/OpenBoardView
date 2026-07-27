#include "obv_core/parse.h"

#include "obv_core/core_utils.h"

#include "FileFormats/ADFile.h"
#include "FileFormats/ASCFile.h"
#include "FileFormats/BDVFile.h"
#include "FileFormats/BRD2File.h"
#include "FileFormats/BRDAllegroFile.h"
#include "FileFormats/BRDFile.h"
#include "FileFormats/BVR3File.h"
#include "FileFormats/BVRFile.h"
#include "FileFormats/CADFile.h"
#include "FileFormats/CAEFile.h"
#include "FileFormats/CSTFile.h"
#include "FileFormats/FZFile.h"
#include "FileFormats/GenCADFile.h"
#include "FileFormats/XJsonFile.h"
#include "FileFormats/XZZPCBFile.h"

#include <climits>
#include <utility>

namespace obv {
namespace {

void EnsureOutlineOrFallback(BRDFileBase &file) {
	// Port of BoardView::LoadBoard empty-outline margin rectangle.
	if (file.outline_segments.size() < 3 && file.format.size() < 3) {
		auto pins = file.pins;
		int minx, maxx, miny, maxy;
		int margin = 200;

		minx = miny = INT_MAX;
		maxx = maxy = INT_MIN;

		for (auto a : pins) {
			if (a.pos.x > maxx) maxx = a.pos.x;
			if (a.pos.y > maxy) maxy = a.pos.y;
			if (a.pos.x < minx) minx = a.pos.x;
			if (a.pos.y < miny) miny = a.pos.y;
		}

		// No pins either - leave outline empty; bounds stay default.
		if (pins.empty()) {
			return;
		}

		maxx += margin;
		maxy += margin;
		minx -= margin;
		miny -= margin;

		file.format.push_back({minx, miny});
		file.format.push_back({maxx, miny});
		file.format.push_back({maxx, maxy});
		file.format.push_back({minx, maxy});
		file.format.push_back({minx, miny});
	}
}

BoardBounds ComputeBounds(BRDBoard &board) {
	int min_x = INT_MAX, max_x = INT_MIN, min_y = INT_MAX, max_y = INT_MIN;
	bool any = false;

	for (auto &pa : board.OutlinePoints()) {
		any = true;
		if (pa->x < min_x) min_x = static_cast<int>(pa->x);
		if (pa->y < min_y) min_y = static_cast<int>(pa->y);
		if (pa->x > max_x) max_x = static_cast<int>(pa->x);
		if (pa->y > max_y) max_y = static_cast<int>(pa->y);
	}
	for (auto &s : board.OutlineSegments()) {
		any = true;
		if (s.first.x < min_x) min_x = static_cast<int>(s.first.x);
		if (s.second.x < min_x) min_x = static_cast<int>(s.second.x);
		if (s.first.y < min_y) min_y = static_cast<int>(s.first.y);
		if (s.second.y < min_y) min_y = static_cast<int>(s.second.y);
		if (s.first.x > max_x) max_x = static_cast<int>(s.first.x);
		if (s.second.x > max_x) max_x = static_cast<int>(s.second.x);
		if (s.first.y > max_y) max_y = static_cast<int>(s.first.y);
		if (s.second.y > max_y) max_y = static_cast<int>(s.second.y);
	}

	BoardBounds b{};
	if (any) {
		b.minX = static_cast<float>(min_x);
		b.minY = static_cast<float>(min_y);
		b.maxX = static_cast<float>(max_x);
		b.maxY = static_cast<float>(max_y);
	}
	return b;
}

std::unique_ptr<BRDFileBase> DetectAndParse(std::vector<char> &buffer,
                                            const filesystem::path &filepath,
                                            const DecryptKeys &keys,
                                            std::string &error) {
	// Port of BoardView::LoadFile format chain (~253-301).
	if (check_fileext(filepath, ".fz")) {
		auto fzfile = std::make_unique<FZFile>();
		fzfile->parse(buffer, keys.fzKey);
		return fzfile;
	}
	if (check_fileext(filepath, ".cae")) {
		auto caefile = std::make_unique<CAEFile>();
		caefile->parse(buffer, keys.caeKey);
		return caefile;
	}
	if (check_fileext(filepath, ".bom") || check_fileext(filepath, ".asc")) {
		return std::make_unique<ASCFile>(buffer, filepath);
	}
	if (GenCADFile::verifyFormat(buffer)) {
		return std::make_unique<GenCADFile>(buffer);
	}
	if (ADFile::verifyFormat(buffer)) {
		return std::make_unique<ADFile>(buffer);
	}
	if (CADFile::verifyFormat(buffer)) {
		return std::make_unique<CADFile>(buffer);
	}
	if (check_fileext(filepath, ".cst")) {
		return std::make_unique<CSTFile>(buffer);
	}
	if (BRDFile::verifyFormat(buffer)) {
		return std::make_unique<BRDFile>(buffer);
	}
	if (BRD2File::verifyFormat(buffer)) {
		return std::make_unique<BRD2File>(buffer);
	}
	if (BDVFile::verifyFormat(buffer)) {
		return std::make_unique<BDVFile>(buffer);
	}
	if (BVRFile::verifyFormat(buffer)) {
		return std::make_unique<BVRFile>(buffer);
	}
	if (BVR3File::verifyFormat(buffer)) {
		return std::make_unique<BVR3File>(buffer);
	}
	if (BRDAllegroFile::verifyFormat(buffer)) {
		return std::make_unique<BRDAllegroFile>(buffer);
	}
	if (XZZPCBFile::verifyFormat(buffer)) {
		return std::make_unique<XZZPCBFile>(buffer, keys.xzzKey);
	}
	if (filepath.filename().extension() == ".json") {
		return std::make_unique<XJsonFile>(buffer);
	}

	error = "Unrecognized file format.";
	return nullptr;
}

} // namespace

BoardSnapshot ParseBoardBuffer(std::vector<char> buffer,
                               const filesystem::path &filepath,
                               const DecryptKeys &keys) {
	BoardSnapshot snap;
	snap.sourceName = filepath.string();

	if (buffer.empty()) {
		snap.error = "Empty board buffer.";
		return snap;
	}

	std::string detect_error;
	snap.file = DetectAndParse(buffer, filepath, keys, detect_error);

	if (!snap.file) {
		snap.error = detect_error.empty() ? "Unrecognized file format." : detect_error;
		return snap;
	}

	if (!snap.file->valid) {
		snap.error = snap.file->error_msg.empty() ? "Invalid board file." : snap.file->error_msg;
		return snap;
	}

	EnsureOutlineOrFallback(*snap.file);
	snap.board = std::make_unique<BRDBoard>(snap.file.get());
	snap.bounds = ComputeBounds(*snap.board);
	return snap;
}

BoardSnapshot ParseBoardFile(const filesystem::path &filepath, const DecryptKeys &keys) {
	BoardSnapshot snap;
	snap.sourceName = filepath.string();

	std::string io_error;
	std::vector<char> buffer = file_as_buffer(filepath, io_error);
	if (buffer.empty()) {
		snap.error = io_error.empty() ? "Failed to read board file." : io_error;
		return snap;
	}

	snap = ParseBoardBuffer(std::move(buffer), filepath, keys);
	if (snap.sourceName.empty()) {
		snap.sourceName = filepath.string();
	}
	return snap;
}

} // namespace obv
