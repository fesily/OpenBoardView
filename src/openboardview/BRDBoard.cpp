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
	std::map<int, std::shared_ptr<Net>> netid_map;
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

	for (auto &[netid, net] : m_file->nets) {
		auto name = net.name.empty() ? to_string(netid) : std::string{net.name};
		auto n = make_shared<Net>();
		if (netid >= 0) {
			n->number = netid;
		}
		n->name = name;
		n->show_name = net.name;
		n->diode = net.diode_value;
		net_map[name] = n;
		netid_map[netid] = n;
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
					++iter;
				}
				comp->is_special_outline = true;
			}

			components_.push_back(comp);
		}
	}

	const auto transform_side_fn = [](auto side){
		using T = decltype(side);
		static_assert((int)T::Both == (int)kBoardSideBoth, "");
		static_assert((int)T::Top == (int)kBoardSideTop, "");
		static_assert((int)T::Bottom == (int)kBoardSideBottom, "");
		return EBoardSide(side);
	};

	auto getNet = [&](auto &t) {
		string net_name = string{t.net};
		std::shared_ptr<Net> net;
		if (t.netId > 0) net = netid_map[t.netId];
		if (!net) {
			net = net_map[net_name];
			if (!net) {
				if (net_name.empty() || is_prefix(kNetUnconnectedPrefix, net_name)) {
					net = net_map[kNetUnconnectedPrefix];
				} else {
					net            = make_shared<Net>();
					net->name      = net_name;
					net->board_side = transform_side_fn(t.side);
					// NOTE: net->number not set
					net_map[net_name] = net;
				}
			}
		}
		return std::make_pair(net, net->name);
	};

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
			if (!brd_pin.snum.empty()) {
				pin->number = brd_pin.snum;
			} else {
				pin->number = std::to_string(pin_idx);
			}

			// Lets us see BGA pad names finally
			//
			if (!brd_pin.name.empty()) {
				pin->name = std::string(brd_pin.name);
			} else {
				pin->name = pin->number;
			}
			pin->show_name = pin->name;
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

			if (!brd_pin.diode_vale.empty()) {
				pin->diode_value = std::string(brd_pin.diode_vale);
			}
			if (!brd_pin.voltage_value.empty()) {
				pin->voltage_value = std::string(brd_pin.voltage_value);
			}
			auto [net, net_name] = getNet(brd_pin);
			if (net) {
				pin->net = net.get();
				if (is_prefix(kNetUnconnectedPrefix, net_name)) {
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
			if (brd_pin.complex_draw) {
				pin->complex_draw = true;
				pin->top_size = {brd_pin.top_size.x / scale, brd_pin.top_size.y / scale};
				pin->top_shape = EShapeType(brd_pin.top_shape);
				pin->bottom_size = {brd_pin.bottom_size.x / scale, brd_pin.bottom_size.y / scale};
				pin->bottom_shape = EShapeType(brd_pin.bottom_shape);
			}
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

	for (auto& board_track : m_tracks) {
		auto track = make_shared<Track>();
		track->board_side = transform_side_fn(board_track.side);
		all_side.emplace(track->board_side);
		track->position_start.x = board_track.points.first.x / scale;
		track->position_start.y = board_track.points.first.y / scale;
		track->position_end.x = board_track.points.second.x / scale;
		track->position_end.y = board_track.points.second.y / scale;
		track->width = board_track.width / scale;
		auto [net, net_name] = getNet(board_track);
		if (net) {
			track->net = net.get();
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
		auto [net, net_name] = getNet(board_via);
		if (net) {
			via->net = net.get();
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
		arc->width           = board_arc.width / scale;
		auto [net, net_name] = getNet(board_arc);
		if (net) {
			arc->net = net.get();
		}
		arcs_.push_back(arc);
	}
	// Populate Net vector by using the map. (sorted by keys)
	for (auto &net : net_map) {
		// check whether the pin represents ground
		net.second->is_ground = (net.second->name == "GND" || net.second->name == "GROUND");
		nets_.push_back(net.second);
	}

	for (auto &net : net_map) {
		net.second->show_name = net.second->name;
	}

	for (auto& comp : components_) {
		if (comp->pins.size() == 1 && comp->pins.front()->net->is_ground && !comp->is_dummy()) {
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
