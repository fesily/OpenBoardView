#include "BRDBoard.h"

#include "FileFormats/BRDFile.h"

#include <cerrno>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
using namespace std;
using namespace std::placeholders;

const string BRDBoard::kNetUnconnectedPrefix = "UNCONNECTED";
const string BRDBoard::kComponentDummyName   = "...";

BRDBoard::BRDBoard(const BRDFileBase * const boardFile)
    : m_file(boardFile) {
	// TODO: strip / trim all strings, especially those used as keys
	// TODO: just loop through original arrays?
	vector<BRDPart> m_parts(m_file->num_parts);
	vector<BRDPin> m_pins(m_file->num_pins);
	vector<BRDNail> m_nails(m_file->num_nails);
	vector<BRDPoint> m_points(m_file->num_format);
	vector<BRDTrack> m_tracks(m_file->tracks.size());
	vector<BRDVia> m_vias(m_file->vias.size());
	vector<BRDArc> m_arcs(m_file->arcs.size());
	set<EBoardSide> all_side;
	auto scale = m_file->scale;

	m_parts  = m_file->parts;
	m_pins   = m_file->pins;
	m_nails  = m_file->nails;
	m_points = m_file->format;
	m_tracks = m_file->tracks;
	m_vias = m_file->vias;
	m_arcs = m_file->arcs;

	// Set outline
	{
		for (auto &brdPoint : m_points) {
			auto point = make_shared<Point>(brdPoint.x / scale, brdPoint.y/ scale);
			outline_points_.push_back(point);
		}
	}

	outline_segments_.reserve(m_file->outline_segments.size());
	std::transform(m_file->outline_segments.begin(), m_file->outline_segments.end(), std::back_inserter(outline_segments_), [scale](const std::pair<BRDPoint, BRDPoint> &s) -> std::pair<Point, Point> {
		return {{s.first.x / scale, s.first.y/ scale}, {s.second.x/ scale, s.second.y / scale}};
	});

	// Populate map of unique nets
	SharedStringMap<Net> net_map;
	{
		// adding special net 'UNCONNECTED'
		auto net_nc           = make_shared<Net>();
		net_nc->name          = kNetUnconnectedPrefix;
		net_nc->is_ground     = false;
		net_map[net_nc->name] = net_nc;

		// handle all the others
		for (auto &brd_nail : m_nails) {
			auto net = make_shared<Net>();

			// copy NET name and number (probe)
			net->name = string(brd_nail.net);

			// avoid having multiple UNCONNECTED<XXX> references
			if (is_prefix(kNetUnconnectedPrefix, net->name)) continue;

			net->number    = brd_nail.probe;

			if (brd_nail.side == BRDPartMountingSide::Top) {
				net->board_side = kBoardSideTop;
			} else {
				net->board_side = kBoardSideBottom;
			}

			// so we can find nets later by name (making unique by name)
			net_map[net->name] = net;
		}
	}

	// Populate parts
	{
		for (auto &brd_part : m_parts) {
			auto comp = make_shared<Component>();

			comp->name    = string(brd_part.name);
			comp->mfgcode = std::move(brd_part.mfgcode);

			comp->p1 = {brd_part.p1.x, brd_part.p1.y};
			comp->p2 = {brd_part.p2.x, brd_part.p2.y};

			// is it some dummy component to indicate test pads?
			if (is_prefix(kComponentDummyName, comp->name)) comp->component_type = Component::kComponentTypeDummy;

			// check what side the board is on (sorcery?)
			if (brd_part.mounting_side == BRDPartMountingSide::Top) {
				comp->board_side = kBoardSideTop;
			} else if (brd_part.mounting_side == BRDPartMountingSide::Bottom) {
				comp->board_side = kBoardSideBottom;
			} else {
				comp->board_side = kBoardSideBoth;
			}

			comp->mount_type = (brd_part.part_type == BRDPartType::SMD) ? Component::kMountTypeSMD : Component::kMountTypeDIP;

			if (brd_part.format.size() == 4) {
				auto iter = comp->special_outline.begin();
				for (auto &item : brd_part.format) {
					*iter = {float(item.x / scale), float(item.y / scale)};
					iter++;
				}
				comp->is_special_outline = true;
			}

			components_.push_back(comp);
		}
	}

	// Populate pins
	{
		// NOTE: originally the pin diameter depended on part.name[0] == 'U' ?
		unsigned int pin_idx  = 0;
		unsigned int part_idx = 1;
		auto pins             = m_pins;
		auto parts            = m_parts;

		for (size_t i = 0; i < pins.size(); i++) {
			// (originally from BoardView::DrawPins)
			const BRDPin &brd_pin = pins[i];
			std::shared_ptr<Component> comp       = components_[brd_pin.part - 1];

			if (!comp) continue;

			auto pin = make_shared<Pin>();

			if (comp->is_dummy()) {
				// component is virtual, i.e. "...", pin is test pad
				pin->type      = Pin::kPinTypeTestPad;
			} else {
				// component is regular / not virtual
				pin->type       = Pin::kPinTypeComponent;
			}
			pin->component  = comp;
			comp->pins.push_back(pin);

			// determine pin number on part
			++pin_idx;
			if (brd_pin.part != part_idx) {
				part_idx = brd_pin.part;
				pin_idx  = 1;
			}
			if (brd_pin.snum) {
				pin->number = brd_pin.snum;
			} else {
				pin->number = std::to_string(pin_idx);
			}

			// Lets us see BGA pad names finally
			//
			if (brd_pin.name) {
				pin->name = std::string(brd_pin.name);
			} else {
				pin->name = pin->number;
			}

			// copy position
			pin->position = Point(brd_pin.pos.x / scale, brd_pin.pos.y / scale);

			// Set board side for pins from specific setting
			if (brd_pin.side == BRDPinSide::Top) {
				pin->board_side = kBoardSideTop;
			} else if (brd_pin.side == BRDPinSide::Bottom) {
				pin->board_side = kBoardSideBottom;
			} else {
				pin->board_side = kBoardSideBoth;
			}

			if (brd_pin.diode_vale) {
				pin->diode_value = std::string(brd_pin.diode_vale);
			}
			if (brd_pin.voltage_value) {
				pin->voltage_value = std::string(brd_pin.voltage_value);
			}

			// set net reference (here's our NET key string again)
			string net_name = string(brd_pin.net);
			if (net_map.count(net_name)) {
				// there is a net with that name in our map
				pin->net = net_map[net_name].get();
				if (is_prefix(kNetUnconnectedPrefix, net_name)) {
					pin->type = Pin::kPinTypeNotConnected;
				}
			} else {
				// no net with that name registered, so create one
				if (!net_name.empty()) {
					if (is_prefix(kNetUnconnectedPrefix, net_name)) {
						// pin is unconnected, so reference our special net
						pin->net  = net_map[kNetUnconnectedPrefix].get();
						pin->type = Pin::kPinTypeNotConnected;
					} else {
						// indeed a new net
						auto net        = make_shared<Net>();
						net->name       = net_name;
						net->board_side = pin->board_side;
						// NOTE: net->number not set
						net_map[net_name] = net;
						pin->net          = net.get();
					}
				} else {
					// not sure this can happen -> no info
					// It does happen in .fz apparently and produces a SEGFAULT… Use
					// unconnected net.
					pin->net  = net_map[kNetUnconnectedPrefix].get();
					pin->type = Pin::kPinTypeNotConnected;
				}
			}

			// TODO: should either depend on file specs or type etc
			//
			//  if(brd_pin.radius) pin->diameter = brd_pin.radius; // some format
			//  (.fz) contains a radius field
			//    else pin->diameter = 0.5f;
			pin->diameter = brd_pin.radius / scale; // some format (.fz) contains a radius field
			pin->size = {brd_pin.size.x / scale, brd_pin.size.y / scale};
			pin->shape = EShapeType(brd_pin.shape);
			pin->angle = brd_pin.angle;
			pin->net->pins.push_back(pin);
			pins_.push_back(pin);
		}

		for (auto& comp : components_) {
			if (comp->is_dummy()) {
				comp->name = comp->name.substr(3);
			}
		}
	}

	const auto transform_side_fn = [](BRDPartMountingSide side){
		static_assert((int)BRDPartMountingSide::Both == (int)kBoardSideBoth, "");
		static_assert((int)BRDPartMountingSide::Top == (int)kBoardSideTop, "");
		static_assert((int)BRDPartMountingSide::Bottom == (int)kBoardSideBottom, "");
		return EBoardSide(side);
	};
	for (auto& board_track : m_tracks) {
		auto track = make_shared<Track>();
		track->board_side = transform_side_fn(board_track.side);
		all_side.emplace(track->board_side);
		track->position_start.x = board_track.points.first.x / scale;
		track->position_start.y = board_track.points.first.y / scale;
		track->position_end.x = board_track.points.second.x / scale;
		track->position_end.y = board_track.points.second.y / scale;
		track->width = board_track.width / scale;
		auto net_name = string(board_track.net);
		if (!net_name.empty()) {
			if (!net_map.count(net_name)) {
				auto net        = make_shared<Net>();
				net->name       = net_name;
				net->board_side = track->board_side;
				// NOTE: net->number not set
				net_map[net_name] = net;
				track->net = net.get();
			} else {
				track->net = net_map[net_name].get();
			}
		}
		tracks_.push_back(track);
	}
	for (auto& board_via : m_vias) {
		auto via = make_shared<Via>();
		via->board_side = transform_side_fn(board_via.side);
		all_side.emplace(via->board_side);
		via->target_side = transform_side_fn(board_via.target_side);
		all_side.emplace(via->target_side);
		via->size = board_via.size / scale;
		via->position.x = board_via.pos.x / scale;
		via->position.y = board_via.pos.y / scale;
		auto net_name = string(board_via.net);
		if (!net_name.empty()) {
			if (!net_map.count(net_name)) {
				auto net        = make_shared<Net>();
				net->name       = net_name;
				net->board_side = via->board_side;
				// NOTE: net->number not set
				net_map[net_name] = net;
				via->net = net.get();
			} else {
				via->net = net_map[net_name].get();
			}
		}
		vias_.push_back(via);
	}

	for (auto& board_arc : m_arcs) {
		auto arc = make_shared<PcbArc>();
		arc->board_side = transform_side_fn(board_arc.side);
		arc->radius = board_arc.radius/ scale;
		arc->startAngle = board_arc.startAngle;
		arc->endAngle = board_arc.endAngle;
		arc->position.x = board_arc.pos.x/ scale;
		arc->position.y = board_arc.pos.y/ scale;
		auto net_name = string(board_arc.net);
		if (!net_name.empty()) {
			if (!net_map.count(net_name)) {
				auto net        = make_shared<Net>();
				net->name       = net_name;
				net->board_side = arc->board_side;
				// NOTE: net->number not set
				net_map[net_name] = net;
				arc->net = net.get();
			} else {
				arc->net = net_map[net_name].get();
			}
		}
		arcs_.push_back(arc);
	}
	// Populate Net vector by using the map. (sorted by keys)
	for (auto &net : net_map) {
		// check whether the pin represents ground
		net.second->is_ground = (net.second->name == "GND" || net.second->name == "GROUND");
		nets_.push_back(net.second);
	}

	for (auto& comp : components_) {
		if (comp->pins.size() == 1 && comp->pins.front()->net->is_ground) {
			comp->component_type = Component::kComponentTypeBoard;
		}
	}

	// Sort components by name
	sort(begin(components_), end(components_), [](const shared_ptr<Component> &lhs, const shared_ptr<Component> &rhs) {
		return lhs->name < rhs->name;
	});

	std::move(all_side.begin(), all_side.end(), std::back_inserter(all_side_));
	std::sort(all_side_.begin(), all_side_.end());
}

BRDBoard::~BRDBoard() {}

SharedVector<Component> &BRDBoard::Components() {
	return components_;
}

SharedVector<Pin> &BRDBoard::Pins() {
	return pins_;
}

SharedVector<Net> &BRDBoard::Nets() {
	return nets_;
}

SharedVector<Point> &BRDBoard::OutlinePoints() {
	return outline_points_;
}

SharedVector<Track> &BRDBoard::Tracks() {
	return tracks_;
}

SharedVector<Via> &BRDBoard::Vias() {
	return vias_;
}

SharedVector<PcbArc> &BRDBoard::arcs() {
	return arcs_;
}

std::vector<EBoardSide> &BRDBoard::AllSide() {
	return all_side_;
}

std::vector<std::pair<Point, Point>> &BRDBoard::OutlineSegments() {
	return outline_segments_;
}

Board::EBoardType BRDBoard::BoardType() {
	return kBoardTypeBRD;
}
