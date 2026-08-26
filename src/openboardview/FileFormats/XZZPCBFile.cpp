#include "XZZPCBFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Crypto/des.h"
#include "utils.h"

/*
 * Credit to @huertas for DES functions
 * Also credit to @inflex and @MuertoGB for help with cracking the encryption + decoding the format
 */


static inline uint32_t read_uint32_t(const std::vector<char> &buf, size_t start_pos, std::string &error_msg) {
	ENSURE_OR_FAIL(buf.size() > start_pos + 3, error_msg, return 0);
	return ((static_cast<uint32_t>(static_cast<unsigned char>(buf[start_pos + 3])) << 24) |
			(static_cast<uint32_t>(static_cast<unsigned char>(buf[start_pos + 2])) << 16) |
			(static_cast<uint32_t>(static_cast<unsigned char>(buf[start_pos + 1])) <<  8) |
			(static_cast<uint32_t>(static_cast<unsigned char>(buf[start_pos + 0])) <<  0));
}

namespace {
static bool read_named_string(const std::vector<char> &buf, uint32_t &ptr, std::string &out, std::string &error_msg) {
	if (ptr + 6 > buf.size()) {
		return false;
	}
	ptr += 2; // u16 unk
	uint32_t nlen = read_uint32_t(buf, ptr, error_msg);
	ptr += 4;
	if (!error_msg.empty() || ptr + nlen > buf.size()) {
		error_msg.clear();
		return false;
	}
	out.assign(buf.begin() + ptr, buf.begin() + ptr + nlen);
	ptr += nlen;
	return true;
}
} // namespace

std::vector<char> XZZPCBFile::des_decrypt(const std::vector<char> &inbuf) {
	std::vector<char> outbuf(inbuf.size());

	// Iterate over input and output buffer at the same time by chunks of 8 bytes
	auto inpos = inbuf.begin();
	for (auto outpos = outbuf.begin();
			inpos < inbuf.end() && outpos < outbuf.end();
			inpos += sizeof(uint64_t), outpos += sizeof(uint64_t)) {
		// Convert 8 bytes of the input buffer into a 64-bit unsigned int with byte order reversed
		uint64_t input = 0l;
		for (size_t i = 0; i < sizeof(uint64_t) && inpos + i < inbuf.end(); i++) {
			input |= static_cast<uint64_t>(static_cast<unsigned char>(inpos[sizeof(uint64_t) - 1 - i])) << (i * 8);
		}

		uint64_t output = des(input, key, 'd');

		// Convert the resulting 64-bit unsigned back into 8 bytes in the output buffer with byte order reversed
		for (size_t i = 0; i < sizeof(uint64_t) && outpos + i < outbuf.end(); i++) {
			outpos[sizeof(uint64_t) - 1 - i] = (output >> (i * 8)) & 0xff;
		}
	}

	return outbuf;
}

std::vector<std::pair<BRDPoint, BRDPoint>> XZZPCBFile::xzz_arc_to_segments(int startAngle, int endAngle, int r, BRDPoint pc) {
	const int numPoints = 10;
	std::vector<std::pair<BRDPoint, BRDPoint>> arc_segments{};

	if (startAngle > endAngle) {
		std::swap(startAngle, endAngle);
	}

	if (endAngle - startAngle > 180) {
		startAngle += 360;
	}

	double startAngleD = static_cast<double>(startAngle);
	double endAngleD   = static_cast<double>(endAngle);
	double rD          = static_cast<double>(r);
	double pc_xD       = static_cast<double>(pc.x);
	double pc_yD       = static_cast<double>(pc.y);

	const double degToRad = 3.14159265358979323846 / 180.0;
	startAngleD *= degToRad;
	endAngleD *= degToRad;

	double angleStep = (endAngleD - startAngleD) / (numPoints - 1);

	BRDPoint pold = {static_cast<int>(pc_xD + rD * std::cos(startAngleD)),
	                 static_cast<int>(pc_yD + rD * std::sin(startAngleD))};
	for (int i = 1; i < numPoints; ++i) {
		double angle = startAngleD + i * angleStep;
		BRDPoint p   = {static_cast<int>(pc_xD + rD * std::cos(angle)), static_cast<int>(pc_yD + rD * std::sin(angle))};
		arc_segments.push_back({pold, p});
		pold = p;
	}

	return arc_segments;
}

bool XZZPCBFile::checkKey(uint64_t key) const {
	auto key_parity = getKeyParity();
	bool valid_key = true;
	for (size_t i = 0; i < sizeof(uint64_t); i++) { // Compute parity for each byte of XZZ key
		uint8_t tmp = (key >> (i * 8)) & 0xff;
		tmp ^= tmp >> 4;
		tmp ^= tmp >> 2;
		tmp ^= tmp >> 1;
		tmp = (~tmp) & 1;
		valid_key = valid_key && (tmp == key_parity[i]);
	}
	return valid_key;
}

const std::array<uint8_t, 8> XZZPCBFile::getKeyParity() const {
	return {{1, 1, 1, 1, 1, 1, 1, 0}};
}

std::string XZZPCBFile::keyToString(uint64_t key) const {
	std::stringstream ss;
	ss << "0x" << std::setfill('0') << std::setw(sizeof(key) * 2)  << std::hex << key;
	return ss.str();
}

bool XZZPCBFile::verifyFormat(const std::vector<char> &buf) {
	if (buf.size() < 6) {
		return false;
	}

	if (std::string(buf.begin(), buf.begin() + 6) == "XZZPCB") {
		return true;
	}

	if (buf.size() > 0x10 && buf[0x10] != 0x00) {
		uint8_t xor_key = buf[0x10];
		std::vector<char> xor_buf(buf.begin(), buf.begin() + 6);
		for (size_t i = 0; i < 6; ++i) {
			xor_buf[i] ^= xor_key;
		}
		return std::string(xor_buf.begin(), xor_buf.end()) == "XZZPCB";
	}

	return false;
}

XZZPCBFile::XZZPCBFile(std::vector<char> &buf, uint64_t xzzkey) {

	if (checkKey(xzzkey)) {
		key = xzzkey;
	} else if (!checkKey(key)) { // Try to fallback to built-in key
		valid = false;
		error_msg = "Invalid XZZ PCB Key\nXZZ PCB key: " + keyToString(xzzkey);
		return;
	}

	std::string v6v6555v6v6{"v6v6555v6v6"};
	auto v6v6555v6v6_found = std::search(buf.begin(), buf.end(), v6v6555v6v6.begin(), v6v6555v6v6.end());

	// v6v6555v6v6_found is buf.end() if not found
	ENSURE_OR_FAIL(buf.size() >= 0x10, error_msg, return);
	if (buf[0x10] != 0x00) {
		uint8_t xor_key = buf[0x10];
		for (auto pos = buf.begin(); pos < v6v6555v6v6_found; pos++) {
			*pos ^= xor_key; // XOR the buffer with xor_key until v6v6555v6v6 is reached
		}
	}

	uint32_t main_data_offset = read_uint32_t(buf, 0x20, error_msg);
	uint32_t net_data_offset  = read_uint32_t(buf, 0x28, error_msg);

	uint32_t main_data_start = main_data_offset + 0x20;
	uint32_t net_data_start  = net_data_offset + 0x20;

	uint32_t main_data_blocks_size = read_uint32_t(buf, main_data_start, error_msg);
	uint32_t net_block_size        = read_uint32_t(buf, net_data_start, error_msg);

	if (!error_msg.empty()) { // Check if one of read_uint32_t() failed
		return;
	}

	ENSURE_OR_FAIL(buf.size() >= net_data_start + net_block_size + 4, error_msg, return);
	std::vector<char> net_block_buf(buf.begin() + net_data_start + 4, buf.begin() + net_data_start + net_block_size + 4);
	parse_net_block(net_block_buf);
	if (!error_msg.empty()) { // Check if parse_net_block() failed
		return;
	}

	process_blocks(buf, main_data_start, main_data_blocks_size);
	if (!error_msg.empty()) { // Check if process_blocks() failed
		return;
	}

	xjsonfile::LayerMapper::compactSequential(*this);

	scale = static_cast<float>(XZZ_GLOBAL_SCALE);
	boardSymmetry = true;
	valid = true;

	num_parts  = parts.size();
	num_pins   = pins.size();
	num_format = format.size();
	num_nails  = nails.size();
	SDL_Log("XZZPCBFile: parts=%u pins=%u tracks=%u vias=%u arcs=%u outline=%u nets=%u texts=%u",
	        num_parts, num_pins, (unsigned)tracks.size(), (unsigned)vias.size(), (unsigned)arcs.size(),
	        (unsigned)outline_segments.size(), (unsigned)nets.size(), (unsigned)texts.size());
}

void XZZPCBFile::process_block(std::vector<char> &block_buf, uint8_t block_type) {
	switch (block_type) {
		case 0x01: // ARC
			parse_arc_block(block_buf);
			break;
		case 0x02: // VIA
			parse_via_block(block_buf);
			break;
		case 0x05: // LINE SEGMENT
			parse_line_segment_block(block_buf);
			break;
		case 0x06: // TEXT
			parse_text_block(block_buf);
			break;
		case 0x07: // PART/PIN
			parse_part_block(block_buf);
			break;
		case 0x09: // TEST PADS/DRILL HOLES
			parse_test_pad_block(block_buf);
			break;
		default:
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: Unhandled block type: %x", block_type);
			break;
	}
}

void XZZPCBFile::process_blocks(const std::vector<char> &buf, uint32_t main_data_start, uint32_t main_data_blocks_size) {
	ENSURE_OR_FAIL(buf.size() >= main_data_start + 4 + main_data_blocks_size, error_msg, return);
	uint32_t current_pointer = main_data_start + 4;
	while (current_pointer < main_data_start + 4 + main_data_blocks_size) {
		uint8_t block_type = buf[current_pointer];
		current_pointer += 1;
		uint32_t block_size = read_uint32_t(buf, current_pointer, error_msg);
		current_pointer += 4;
		if (!error_msg.empty()) { // Check if read_uint32_t() failed
			return;
		}

		ENSURE_OR_FAIL(buf.size() >= current_pointer + block_size, error_msg, return);
		std::vector<char> block_buf(buf.begin() + current_pointer, buf.begin() + current_pointer + block_size);
		process_block(block_buf, block_type);
		current_pointer += block_size;
		if (!error_msg.empty()) { // Check if process_block() failed
			return;
		}
	}
}

// Layers:
// 1->16 Trace Layers (Used in order excluding last which always uses 16)
// 17 Silkscreen
// 18->27 Unknown
// 28 Board edges

void XZZPCBFile::parse_arc_block(const std::vector<char> &buf) {
	if (buf.size() < 8 * sizeof(uint32_t)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short arc block (%zu)", buf.size());
		return;
	}
	uint32_t layer = read_uint32_t(buf, 0 * sizeof(uint32_t), error_msg);
	uint32_t x     = read_uint32_t(buf, 1 * sizeof(uint32_t), error_msg);
	uint32_t y     = read_uint32_t(buf, 2 * sizeof(uint32_t), error_msg);
	uint32_t r     = read_uint32_t(buf, 3 * sizeof(uint32_t), error_msg);
	uint32_t as    = read_uint32_t(buf, 4 * sizeof(uint32_t), error_msg);
	uint32_t ae    = read_uint32_t(buf, 5 * sizeof(uint32_t), error_msg);
	uint32_t width = read_uint32_t(buf, 6 * sizeof(uint32_t), error_msg);
	uint32_t net_index = read_uint32_t(buf, 7 * sizeof(uint32_t), error_msg);
	if (!error_msg.empty()) {
		error_msg.clear();
		return;
	}

	const double deg_start = static_cast<double>(as) / XZZ_GLOBAL_SCALE;
	const double deg_end   = static_cast<double>(ae) / XZZ_GLOBAL_SCALE;
	BRDPoint centre{static_cast<int>(x), static_cast<int>(y)};

	if (layer == static_cast<uint32_t>(xjsonfile::Board)) {
		auto segments = xzz_arc_to_segments(static_cast<int>(deg_start), static_cast<int>(deg_end),
		                                    static_cast<int>(r), centre);
		std::move(segments.begin(), segments.end(), std::back_inserter(outline_segments));
		return;
	}

	BRDArc arc{};
	arc.pos = centre;
	arc.radius = static_cast<float>(r);
	arc.width = static_cast<float>(width);
	double start = deg_start;
	double end = deg_end;
	if (start > end) {
		start -= 360.0;
	}
	constexpr double degToRad = 3.14159265358979323846 / 180.0;
	arc.startAngle = static_cast<float>(start * degToRad);
	arc.endAngle = static_cast<float>(end * degToRad);
	arc.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	apply_net(arc.net, arc.netId, net_index);
	arcs.push_back(arc);
}

void XZZPCBFile::parse_via_block(const std::vector<char> &buf) {
	if (buf.size() < 8 * sizeof(uint32_t)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short via block (%zu)", buf.size());
		return;
	}
	uint32_t x     = read_uint32_t(buf, 0 * sizeof(uint32_t), error_msg);
	uint32_t y     = read_uint32_t(buf, 1 * sizeof(uint32_t), error_msg);
	uint32_t size  = read_uint32_t(buf, 2 * sizeof(uint32_t), error_msg);
	// u32[3] aperture ignored
	uint32_t layer = read_uint32_t(buf, 4 * sizeof(uint32_t), error_msg);
	uint32_t to    = read_uint32_t(buf, 5 * sizeof(uint32_t), error_msg);
	uint32_t net_index = read_uint32_t(buf, 6 * sizeof(uint32_t), error_msg);
	if (!error_msg.empty()) {
		error_msg.clear();
		return;
	}

	BRDVia via{};
	via.pos = {static_cast<int>(x), static_cast<int>(y)};
	via.size = static_cast<float>(size);
	via.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	via.target_side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(to));
	apply_net(via.net, via.netId, net_index);
	vias.push_back(via);
}

void XZZPCBFile::parse_line_segment_block(const std::vector<char> &buf) {
	if (buf.size() < 7 * sizeof(uint32_t)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short line block (%zu)", buf.size());
		return;
	}
	uint32_t layer = read_uint32_t(buf, 0 * sizeof(uint32_t), error_msg);
	uint32_t x1    = read_uint32_t(buf, 1 * sizeof(uint32_t), error_msg);
	uint32_t y1    = read_uint32_t(buf, 2 * sizeof(uint32_t), error_msg);
	uint32_t x2    = read_uint32_t(buf, 3 * sizeof(uint32_t), error_msg);
	uint32_t y2    = read_uint32_t(buf, 4 * sizeof(uint32_t), error_msg);
	uint32_t width = read_uint32_t(buf, 5 * sizeof(uint32_t), error_msg);
	uint32_t net_index = read_uint32_t(buf, 6 * sizeof(uint32_t), error_msg);
	if (!error_msg.empty()) {
		error_msg.clear();
		return;
	}

	BRDPoint p1{static_cast<int>(x1), static_cast<int>(y1)};
	BRDPoint p2{static_cast<int>(x2), static_cast<int>(y2)};

	if (layer == static_cast<uint32_t>(xjsonfile::Board)) {
		outline_segments.push_back({p1, p2});
		return;
	}

	BRDTrack track{};
	track.points = {p1, p2};
	track.width = static_cast<float>(width);
	track.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	apply_net(track.net, track.netId, net_index);
	tracks.push_back(track);
}

BRDPin XZZPCBFile::parse_pin_block(const std::vector<char> &buf, uint32_t &current_pointer) {
	BRDPin pin{};
	uint32_t pin_block_size = read_uint32_t(buf, current_pointer, error_msg);
	uint32_t pin_block_end  = current_pointer + pin_block_size + 4;
	current_pointer += 4;
	if (!error_msg.empty() || pin_block_end > buf.size()) {
		error_msg.clear();
		current_pointer = pin_block_end > buf.size() ? static_cast<uint32_t>(buf.size()) : pin_block_end;
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short pin block");
		return {};
	}

	uint32_t layer = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	uint32_t x     = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	uint32_t y     = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	current_pointer += 4; // drillSize.x unused
	uint32_t angle_raw = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	uint32_t pin_name_size = read_uint32_t(buf, current_pointer, error_msg); current_pointer += 4;
	if (!error_msg.empty() || current_pointer + pin_name_size + 32 + 4 > pin_block_end) {
		error_msg.clear();
		current_pointer = pin_block_end;
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short pin block");
		return {};
	}
	std::string pin_name(buf.begin() + current_pointer, buf.begin() + current_pointer + pin_name_size);
	current_pointer += pin_name_size;

	auto ru32 = [&](uint32_t &p) {
		uint32_t v = read_uint32_t(buf, p, error_msg);
		p += 4;
		return v;
	};
	uint32_t gp = current_pointer;
	uint32_t top_w = ru32(gp), top_h = ru32(gp);
	uint8_t top_shape = static_cast<uint8_t>(buf[gp]); gp += 1;
	uint32_t size_w = ru32(gp), size_h = ru32(gp);
	uint8_t shape = static_cast<uint8_t>(buf[gp]); gp += 1;
	uint32_t bot_w = ru32(gp), bot_h = ru32(gp);
	uint8_t bot_shape = static_cast<uint8_t>(buf[gp]); gp += 1;
	current_pointer += 32; // leftover 5 geometry bytes unused (not angle)
	uint32_t net_index = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer = pin_block_end;
	if (!error_msg.empty()) {
		error_msg.clear();
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short pin block");
		return {};
	}

	pin.pos = {static_cast<int>(x), static_cast<int>(y)};
	pin.name = pin_name;
	pin.snum = pin.name;
	pin.side = xjsonfile::LayerMapper::castPinSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	pin.top_size = {static_cast<int>(top_w), static_cast<int>(top_h)};
	pin.top_shape = static_cast<BPDPinShape>(top_shape);
	pin.size = {static_cast<int>(size_w), static_cast<int>(size_h)};
	pin.shape = static_cast<BPDPinShape>(shape);
	pin.bottom_size = {static_cast<int>(bot_w), static_cast<int>(bot_h)};
	pin.bottom_shape = static_cast<BPDPinShape>(bot_shape);
	pin.complex_draw = (top_shape != shape) || (shape != bot_shape);
	pin.radius = static_cast<double>(std::min(pin.size.x, pin.size.y)) / 2.0;
	pin.angle = static_cast<float>(angle_raw) / static_cast<float>(XZZ_GLOBAL_SCALE);
	apply_net(pin.net, pin.netId, net_index);
	return pin;
}

void XZZPCBFile::parse_text_block(const std::vector<char> &buf) {
	if (buf.size() < 24) return;
	uint32_t layer = read_uint32_t(buf, 0, error_msg);
	uint32_t x = read_uint32_t(buf, 4, error_msg);
	uint32_t y = read_uint32_t(buf, 8, error_msg);
	if (!error_msg.empty()) { error_msg.clear(); return; }
	uint32_t ptr = 24;
	std::string text;
	if (!read_named_string(buf, ptr, text, error_msg)) return;
	if (text.empty()) return;
	BRDText t;
	t.pos = {static_cast<int>(x), static_cast<int>(y)};
	t.text = text;
	t.side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	texts.push_back(t);
}

void XZZPCBFile::parse_part_block(std::vector<char> &encrypted_buf) {
	BRDPart part{};

	auto buf = des_decrypt(encrypted_buf);

	uint32_t current_pointer = 0;
	uint32_t part_size       = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	if (!error_msg.empty()) {
		return;
	}
	if (current_pointer + 18 > buf.size()) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short part header");
		return;
	}
	uint32_t layer = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	uint32_t x     = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	uint32_t y     = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	current_pointer += 4; // angle unused (BRDPart has no angle)
	if (!error_msg.empty()) {
		error_msg.clear();
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short part header");
		return;
	}
	std::string fpid;
	if (!read_named_string(buf, current_pointer, fpid, error_msg)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: part FPID unreadable");
		return;
	}

	part.mounting_side = xjsonfile::LayerMapper::castSide(static_cast<xjsonfile::PCB_LAYER_ID>(layer));
	part.part_type     = BRDPartType::SMD;
	part.name          = fpid;
	(void)x;
	(void)y;
	bool named_from_text = false;

	const uint32_t part_end = part_size + 4;
	ENSURE_OR_FAIL(buf.size() >= part_end, error_msg, return);

	auto push_unique = [](std::vector<BRDPoint> &pts, BRDPoint p) {
		if (std::find(pts.begin(), pts.end(), p) == pts.end()) {
			pts.push_back(p);
		}
	};

	while (current_pointer < part_end) {
		uint8_t sub = static_cast<uint8_t>(buf[current_pointer]);
		if (sub == 0x00) {
			current_pointer += 1;
			continue;
		}
		if (sub == 0x09) {
			current_pointer += 1;
			auto pin = parse_pin_block(buf, current_pointer);
			if (!error_msg.empty()) {
				error_msg.clear();
				break;
			}
			if (pin.name.empty() && pin.size.x == 0 && pin.size.y == 0) {
				continue;
			}
			pin.part = static_cast<unsigned int>(parts.size() + 1);
			pins.push_back(pin);
			continue;
		}
		current_pointer += 1;
		uint32_t sub_size = read_uint32_t(buf, current_pointer, error_msg);
		current_pointer += 4;
		if (!error_msg.empty()) {
			error_msg.clear();
			break;
		}
		if (current_pointer + sub_size > buf.size()) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short part sub-block 0x%02X", sub);
			break;
		}
		std::vector<char> sub_buf(buf.begin() + current_pointer, buf.begin() + current_pointer + sub_size);
		current_pointer += sub_size;
		switch (sub) {
			case 0x06: {
				const auto texts_before = texts.size();
				parse_text_block(sub_buf);
				if (!named_from_text && texts.size() > texts_before && !texts.back().text.empty()) {
					part.name = texts.back().text;
					named_from_text = true;
				}
				break;
			}
			case 0x05: {
				if (sub_buf.size() >= 20) {
					uint32_t x1 = read_uint32_t(sub_buf, 4, error_msg);
					uint32_t y1 = read_uint32_t(sub_buf, 8, error_msg);
					uint32_t x2 = read_uint32_t(sub_buf, 12, error_msg);
					uint32_t y2 = read_uint32_t(sub_buf, 16, error_msg);
					if (!error_msg.empty()) {
						error_msg.clear();
						break;
					}
					push_unique(part.format, {static_cast<int>(x1), static_cast<int>(y1)});
					push_unique(part.format, {static_cast<int>(x2), static_cast<int>(y2)});
				}
				break;
			}
			default:
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: Unknown sub block type: 0x%02X in %s",
				            sub, part.name.c_str());
				break;
		}
	}

	if (!part.format.empty()) {
		std::ranges::sort(part.format, [](auto &l, auto &r) {
			return (((int64_t)l.x << 32) | (uint32_t)l.y) < (((int64_t)r.x << 32) | (uint32_t)r.y);
		});
		auto iter = std::unique(part.format.begin(), part.format.end());
		part.format.erase(iter, part.format.end());
		if (part.format.size() >= 2) std::swap(part.format[0], part.format[1]);
	}

	part.end_of_pins = static_cast<unsigned int>(pins.size());
	parts.push_back(part);
}

void XZZPCBFile::parse_test_pad_block(const std::vector<char> &buf) {
	BRDPart part{};
	BRDPin pin{};

	uint32_t current_pointer = 0;
	// uint32_t pad_number      = read_uint32_t(buf, current_pointer, error_msg); /* unused */
	current_pointer += 4;
	uint32_t x_origin = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	uint32_t y_origin = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	current_pointer += 8; // inner_diameter + unknown1
	uint32_t name_length = read_uint32_t(buf, current_pointer, error_msg);
	current_pointer += 4;
	if (!error_msg.empty()) {
		error_msg.clear();
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short test pad block");
		return;
	}

	if (buf.size() < current_pointer + name_length) {
		error_msg.clear();
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short test pad block");
		return;
	}
	std::string name(buf.begin() + current_pointer, buf.begin() + current_pointer + name_length);
	current_pointer += name_length;
	current_pointer    = buf.size() - 4;
	uint32_t net_index = read_uint32_t(buf, current_pointer, error_msg);
	if (!error_msg.empty()) {
		error_msg.clear();
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "XZZPCBFile: short test pad block");
		return;
	}

	part.name          = strdup(("..." + name).c_str()); // To make it get the kPinTypeTestPad type
	part.mounting_side = BRDPartMountingSide::Top;
	part.part_type     = BRDPartType::SMD;

	pin.snum  = strdup(name.c_str());
	pin.side  = BRDPinSide::Top;
	pin.pos.x = static_cast<int>(x_origin);
	pin.pos.y = static_cast<int>(y_origin);
	apply_net(pin.net, pin.netId, net_index);
	pin.part = parts.size() + 1;
	pin.radius = 7.0 * XZZ_GLOBAL_SCALE;
	pins.push_back(pin);
	part.end_of_pins = pins.size();
	parts.push_back(part);
}

void XZZPCBFile::parse_net_block(const std::vector<char> &buf) {
	uint32_t current_pointer = 0;
	while (current_pointer < buf.size()) {
		uint32_t net_size = read_uint32_t(buf, current_pointer, error_msg);
		current_pointer += 4;
		uint32_t net_index = read_uint32_t(buf, current_pointer, error_msg);
		current_pointer += 4;
		if (!error_msg.empty()) { // Check if one of read_uint32_t() failed
			return;
		}

		ENSURE_OR_FAIL(net_size >= 8, error_msg, return);
		const size_t name_len = static_cast<size_t>(net_size) - 8;
		ENSURE_OR_FAIL(buf.size() >= current_pointer + name_len, error_msg, return);
		std::string net_name(buf.begin() + current_pointer, buf.begin() + current_pointer + name_len);
		current_pointer += static_cast<uint32_t>(name_len);

		net_dict[net_index] = net_name;

		BRDNet n;
		n.id = static_cast<int>(net_index);
		n.name = net_name;
		nets[static_cast<int>(net_index)] = n;
	}
}

void XZZPCBFile::apply_net(std::string &net, int &netId, uint32_t net_index) {
	netId = static_cast<int>(net_index);
	auto it = net_dict.find(net_index);
	if (it == net_dict.end() || it->second == "NC") {
		net = "UNCONNECTED";
	} else {
		net = it->second;
	}
}
