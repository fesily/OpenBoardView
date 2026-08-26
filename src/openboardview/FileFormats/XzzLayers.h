#pragma once

#include <map>
#include <set>
#include <type_traits>
#include <vector>

#include "BRDFileBase.h"

namespace xjsonfile {

enum PCB_LAYER_ID { Unknown = 0, Bottom = 16, SILKSCREEN = 17, Board3 = 18, Board = 28, PART_OUTLINES = 29, BottomSilk = 30, TestPad = 34 };

struct LayerMapper {
	std::vector<int> layers;
	LayerMapper(std::vector<int> &&l = {})
	    : layers(l) {
		if (layers.size() < 2) return;
		for (size_t i = 1; i < layers.size(); i++) {
			if (layers[i] != layers[i - 1] + 1) {
				max = layers[i - 1];
				break;
			}
		}
		if (max == 1) { // only top no bottom
			max = -1;
		}
	}
	int max = -1;

	// File copper layer N (1..16) -> SN. S1 == Top, so the UI name is "N".
	// Do not emit Bottom: BRDBoard remaps the highest SN to Bottom itself.
	static int copperSideValue(int id) {
		if (id < 1)
			return (int)BRDPartMountingSide::Top;
		if (id > 16)
			return (int)BRDPartMountingSide::S16;
		return (int)BRDPartMountingSide::Top + id - 1;
	}

	static BRDPartMountingSide castSide(PCB_LAYER_ID id) {
		switch (id) {
			case Unknown: return BRDPartMountingSide::Top;
			case Board: return BRDPartMountingSide::Both;
			case SILKSCREEN:
			case PART_OUTLINES:
			case Board3:
			case TestPad: return BRDPartMountingSide::Top;
			case Bottom:
			case BottomSilk: return (BRDPartMountingSide)copperSideValue(16);
			default: return (BRDPartMountingSide)copperSideValue((int)id);
		}
	}

	BRDPartMountingSide toSide(PCB_LAYER_ID id) {
		auto side = castSide(id);
		return side;
	}

	static BRDPinSide castPinSide(PCB_LAYER_ID id) {
		switch (id) {
			case Unknown: return BRDPinSide::Top;
			case Board: return BRDPinSide::Both;
			case SILKSCREEN:
			case PART_OUTLINES:
			case Board3:
			case TestPad: return BRDPinSide::Top;
			case Bottom:
			case BottomSilk: return (BRDPinSide)copperSideValue(16);
			default: return (BRDPinSide)copperSideValue((int)id);
		}
	}

	BRDPinSide toPinSide(PCB_LAYER_ID id) {
		auto side = castPinSide(id);
		return side;
	}

	// Collapse used copper sides to S1,S2,S3... so the UI lists 1,2,3,4 with no gaps.
	static void compactSequential(BRDFileBase &file) {
		std::set<int> used;
		bool has_bottom = false;
		auto consider = [&](auto side) {
			const int s = static_cast<int>(side);
			if (s == static_cast<int>(BRDPartMountingSide::Both)) return;
			if (s == static_cast<int>(BRDPartMountingSide::Bottom)) {
				has_bottom = true;
				return;
			}
			used.insert(s);
		};
		for (const auto &t : file.tracks) consider(t.side);
		for (const auto &v : file.vias) {
			consider(v.side);
			consider(v.target_side);
		}
		for (const auto &a : file.arcs) consider(a.side);
		for (const auto &p : file.pins) consider(p.side);
		for (const auto &p : file.parts) consider(p.mounting_side);
		for (const auto &t : file.texts) consider(t.side);
		for (const auto &n : file.nails) consider(n.side);
		if (used.empty() && !has_bottom) return;

		std::map<int, int> remap;
		int next = static_cast<int>(BRDPartMountingSide::Top);
		const int last = static_cast<int>(BRDPartMountingSide::S16);
		auto take = [&](int s) {
			remap[s] = next;
			if (next < last) ++next;
		};
		for (int s : used) take(s);
		if (has_bottom) take(static_cast<int>(BRDPartMountingSide::Bottom));

		auto apply = [&](auto &side) {
			using T = std::remove_reference_t<decltype(side)>;
			if (static_cast<int>(side) == static_cast<int>(BRDPartMountingSide::Both)) return;
			auto it = remap.find(static_cast<int>(side));
			if (it != remap.end()) side = static_cast<T>(it->second);
		};
		for (auto &t : file.tracks) apply(t.side);
		for (auto &v : file.vias) {
			apply(v.side);
			apply(v.target_side);
		}
		for (auto &a : file.arcs) apply(a.side);
		for (auto &p : file.pins) apply(p.side);
		for (auto &p : file.parts) apply(p.mounting_side);
		for (auto &t : file.texts) apply(t.side);
		for (auto &n : file.nails) apply(n.side);
	}
};

} // namespace xjsonfile
