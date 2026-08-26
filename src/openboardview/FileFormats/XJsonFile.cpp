#include "SDL_stdinc.h"
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <iguana/json_reader.hpp>

#include "XJsonFile.h"
#include "XzzLayers.h"

constexpr float number_scale = 10000;
namespace xjsonfile {

struct Position {
	float x;
	float y;
	YLT_REFL(Position, x, y);
};

static BRDPoint toPt(const std::optional<Position> &p) {
	if (!p) return {};
	BRDPoint ret;
	ret.x = p->x * number_scale;
	ret.y = p->y * number_scale;
	return ret;
}

struct PcbTrack {
	std::optional<float> width;
	std::optional<Position> end;
	std::optional<Position> start;
	std::string_view type = "track";
	PCB_LAYER_ID layer;
	std::optional<int> netId;

	operator BRDTrack() const {
		BRDTrack t;
		t.netId         = netId.value_or(t.netId);
		t.width         = width.value_or(0.1) * number_scale;
		t.side          = LayerMapper::castSide(layer);
		t.points.first  = toPt(start);
		t.points.second = toPt(end);
		return t;
	}
	YLT_REFL(PcbTrack, width, end, start, type, layer, netId);
};

struct PcbPad {
	std::optional<Position> position;
	std::optional<Position> topSize;
	std::optional<int> topShape;
	std::optional<Position> size;
	std::optional<int> shape;
	std::optional<Position> bottomSize;
	std::optional<int> bottomShape;
	std::optional<Position> drillSize;
	std::optional<int> drillShape;
	std::optional<float> length;
	std::optional<float> angle;
	std::optional<std::string_view> name;
	std::optional<std::string_view> diode;
	std::string_view type = "pad";
	PCB_LAYER_ID layer;
	std::optional<int> netId;

	operator BRDPin() const {
		BRDPin p;
		p.diode_vale   = diode.value_or("");
		p.pos          = toPt(position);
		p.name         = name.value_or("");
		p.side         = LayerMapper::castPinSide(layer);
		p.netId        = netId.value_or(p.netId);
		p.top_size     = toPt(topSize);
		p.top_shape    = (BPDPinShape)topShape.value_or(0);
		p.size         = toPt(size);
		p.shape        = (BPDPinShape)shape.value_or(0);
		p.bottom_size  = toPt(bottomSize);
		p.bottom_shape = (BPDPinShape)bottomShape.value_or(0);
		p.complex_draw = topShape != shape || shape != bottomShape;
		p.angle        = angle.value_or(0);
		p.radius       = length.value_or(0) * number_scale;
		if (p.radius < 0.0001) {
			p.radius = std::min(p.size.x, p.size.y) / 2;
		}
		return p;
	}
	YLT_REFL(PcbPad, position, topSize, topShape, size, shape, bottomSize, bottomShape, drillSize, drillShape, length, angle, name, diode, type, layer, netId);
};

struct PcbArc {
	std::optional<float> startAngle;
	std::optional<float> endAngle;
	std::optional<float> rectWidth;
	std::optional<float> width;
	std::optional<Position> position;
	std::string_view type = "arc";
	PCB_LAYER_ID layer;
	std::optional<int> netId;
	operator BRDArc() const {
		BRDArc a;
		a.netId           = netId.value_or(a.netId);
		a.pos             = toPt(position);
		constexpr auto pi = 3.1415926f;
		a.startAngle      = startAngle.value_or(0);
		a.endAngle        = endAngle.value_or(0);
		if (a.startAngle > a.endAngle) {
			a.startAngle -= 360;
		}
		a.startAngle *= (M_PI / 180.0);
		a.endAngle *= (M_PI / 180.0);
		a.radius = rectWidth.value_or(0.1) * number_scale;
		a.side   = LayerMapper::castSide(layer);
		a.width  = width.value_or(0.1f) * number_scale;
		return a;
	}
	YLT_REFL(PcbArc, startAngle, endAngle, rectWidth, width, position, type, layer, netId);
};

struct PcbRect {
	std::optional<float> orient;
	std::string_view type = "rect";
	PCB_LAYER_ID layer;
	std::optional<int> netId;
	YLT_REFL(PcbRect, orient, type, layer, netId);
};

struct PcbVia {
	std::optional<bool> panelPos;
	std::optional<float> aperture;
	std::optional<Position> position;
	std::string_view type = "via";
	PCB_LAYER_ID layer;
	std::optional<PCB_LAYER_ID> to;
	std::optional<float> size;
	std::optional<int> netId;

	operator BRDVia() const {
		BRDVia v;
		v.netId = netId.value_or(v.netId);
		v.side  = LayerMapper::castSide(layer);
		v.pos   = toPt(position);
		if (to.has_value()) v.target_side = LayerMapper::castSide(to.value());
		v.size = size.value_or(1) * number_scale;
		return v;
	}
	YLT_REFL(PcbVia, panelPos, aperture, position, type, layer, to, size, netId);
};

struct PcbText {
	std::optional<float> pointSize;
	std::optional<std::string_view> text;
	std::optional<float> orient;
	std::optional<int> font;
	std::string_view type = "text";
	std::optional<std::string_view> value;
	std::optional<Position> position;
	PCB_LAYER_ID layer;
	std::optional<int> netId;

	operator BRDText() const {
		BRDText t;
		t.side  = LayerMapper::castSide(layer);
		t.netId = netId.value_or(t.netId);
		t.pos   = toPt(position);
		t.text  = text.value();
		return t;
	}
	YLT_REFL(PcbText, pointSize, text, orient, font, type, value, position, layer, netId);
};

struct PcbModule {
	std::string_view type = "module";
	std::optional<float> angle;
	std::optional<Position> position;
	std::optional<std::string_view> FPID;
	std::optional<PcbText> text;
	std::optional<PcbText> text1;
	PCB_LAYER_ID layer;
	std::optional<int> netId;
	std::vector<std::variant<PcbPad, PcbTrack>> items;
	YLT_REFL(PcbModule, type, angle, position, FPID, text, text1, layer, netId, items);
};

struct Net {
	std::string_view name;
	std::string_view info, diode, voltage;
	YLT_REFL(Net, name, info, diode, voltage);
};

} // namespace xjsonfile

struct XJsonFileRoot {
	std::vector<xjsonfile::PcbTrack> track;
	std::vector<xjsonfile::PcbText> text;
	std::vector<xjsonfile::PcbArc> arc;
	std::vector<xjsonfile::PcbVia> via;
	std::vector<xjsonfile::PcbPad> pad;
	std::vector<xjsonfile::PcbModule> module;
	YLT_REFL(XJsonFileRoot, track, text, arc, via, pad, module);
};

struct XJsonFileImpl {
	XJsonFileRoot root;
	std::unordered_map<int, xjsonfile::Net> nets;
	std::string name;
	YLT_REFL(XJsonFileImpl, root, nets, name);
};

namespace iguana {
template <>
struct variant_type_field_helper<std::variant<xjsonfile::PcbPad, xjsonfile::PcbTrack>> : std::true_type {
	template <typename T>
	constexpr std::string_view operator()(T *) {
		return T{}.type;
	};
};
} // namespace iguana

XJsonFile::~XJsonFile() {}

XJsonFile::XJsonFile(std::vector<char> &b)
    : buf{std::move(b)} {

	scale = number_scale;
	try {
		file = std::make_unique<XJsonFileImpl>();
		iguana::from_json(*file, buf.begin(), buf.end());
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return;
	}

	for (const auto &track : file->root.track) {
		if (track.layer == xjsonfile::Board) {
			outline_segments.push_back({toPt(track.start), toPt(track.end)});
		} else {
			tracks.push_back(track);
		}
	}
	for (const auto &text : file->root.text) {
		if (!text.text || text.text->empty()) continue;

		texts.push_back(text);
	}
	for (const auto &arc : file->root.arc) {
		if (arc.layer == xjsonfile::Board) {
			BRDArc a      = arc;
			auto segments = arc_to_segments(a.startAngle, a.endAngle, a.radius, a.pos);
			std::move(segments.begin(), segments.end(), std::back_inserter(this->outline_segments));
		} else {
			arcs.push_back(arc);
		}
	}

	for (const auto &via : file->root.via) {
		vias.push_back(via);
	}

	for (const auto &pad : file->root.pad) {
		pins.push_back(pad);
		pins.back().part = parts.size() + 1;
		parts.push_back(BRDPart{
		    .name          = "..." + std::string{pad.name.value()},
		    .mounting_side = xjsonfile::LayerMapper::castSide(pad.layer),
		    .part_type     = BRDPartType::SMD,
		    .end_of_pins   = (uint32_t)pins.size(),
		});
	}

	for (const auto &mod : file->root.module) {
		BRDPart part;
		part.name          = mod.text.value().text.value_or("unknown");
		part.mounting_side = xjsonfile::LayerMapper::castSide(mod.layer);
		part.part_type     = BRDPartType::SMD;
		int partId         = parts.size() + 1;
		auto pinsz         = pins.size();
		for (const auto &item : mod.items) {
			std::visit(
			    [&part, this, partId](auto &p) {
				    using T = std::remove_cv_t<std::remove_reference_t<decltype(p)>>;
				    if constexpr (std::is_same_v<T, xjsonfile::PcbTrack>) {
					    const xjsonfile::PcbTrack &t = p;
					    part.format.push_back(toPt(t.start));
					    part.format.push_back(toPt(t.end));
				    } else if constexpr (std::is_same_v<T, xjsonfile::PcbPad>) {
					    const xjsonfile::PcbPad &t = p;
					    pins.push_back(t);
					    pins.back().part = partId;
				    }
			    },
			    item);
		}
		if (part.format.size() > 0) {
			std::ranges::sort(part.format,
			                  [](auto &l, auto &r) { return (((int64_t)l.x << 32) | l.y) < (((int64_t)r.x << 32) | r.y); });
			auto iter = std::unique(part.format.begin(), part.format.end());
			part.format.erase(iter, part.format.end());
			std::swap(part.format[0], part.format[1]);
		}
		part.end_of_pins = pins.size();
		if (part.end_of_pins - pinsz == 1) {
			part.name = "..." + part.name;
		}
		parts.push_back(part);
	}
	for (const auto &[netid, net] : file->nets) {
		BRDNet n;
		n.id          = netid;
		n.info        = net.info;
		n.name        = net.name;
		n.diode_value = net.diode;
		nets[netid]   = n;
	}
	num_parts  = parts.size();
	num_pins   = pins.size();
	num_format = format.size();
	num_nails  = nails.size();

	xjsonfile::LayerMapper::compactSequential(*this);

	boardSymmetry = true;
	valid         = num_parts > 0 || num_format > 0;
}

bool XJsonFile::verifyFormat(std::vector<char> &buf) {
	return false;
}