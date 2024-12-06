#include "SDL_stdinc.h"
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>

#include <iguana/json_reader.hpp>

#include "XJsonFile.h"
#include <set>

constexpr float number_scale = 10000;
namespace yamlfile {

struct Position {
	float x;
	float y;
};
YLT_REFL(Position, x, y);

enum PCB_LAYER_ID { Unknown = 0, Bottom = 16, Top = 17, Board3 = 18, Board = 28, TestPad = 34 };

struct LayerMapper {
	int max;
	BRDPartMountingSide toSide(PCB_LAYER_ID id) {
		switch (id) {
			case Top: return BRDPartMountingSide::Top;
			case Board: return BRDPartMountingSide::Both;
			case Bottom: return BRDPartMountingSide::Bottom;
			case Board3: return BRDPartMountingSide::Top;
			case TestPad: return BRDPartMountingSide::Top;
			// todo: both
			default: return (BRDPartMountingSide)id;
		}
	}

	BRDPinSide toPinSide(PCB_LAYER_ID id) {
		switch (id) {
			case Top: return BRDPinSide::Top;
			case Board: return BRDPinSide::Both;
			case Bottom: return BRDPinSide::Bottom;
			case Board3: return BRDPinSide::Top;
			case TestPad: return BRDPinSide::Top;
			// todo: both
			default: return (BRDPinSide)id;
		}
	}
};
static std::unique_ptr<LayerMapper> layerMapper;
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
		t.netId  = netId.value_or(t.netId);
		t.width         = width.value_or(0.1) * number_scale;
		t.side          = layerMapper->toSide(layer);
		t.points.first  = toPt(start);
		t.points.second = toPt(end);
		return t;
	}
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
		p.diode_vale = diode.value_or("");
		p.pos = toPt(position);
		p.name = name.value_or("");
		p.side              = layerMapper->toPinSide(layer);
		p.netId = netId.value_or(p.netId);
		p.top_size       = toPt(topSize);
		p.top_shape      = (BPDPinShape)topShape.value_or(0);
		p.size = toPt(size);
		p.shape = (BPDPinShape)shape.value_or(0);
		p.bottom_size       = toPt(bottomSize);
		p.bottom_shape   = (BPDPinShape)bottomShape.value_or(0);
		p.complex_draw      = topShape != shape || shape != bottomShape;
		p.angle = angle.value_or(0);
		p.radius     = length.value_or(0) * number_scale;
		if (p.radius < 0.0001) {
			p.radius = std::max(p.size.x, p.size.y) / 2;
		}
		return p;
	}
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
		a.netId  = netId.value_or(a.netId);
		a.pos        = toPt(position);
		constexpr auto pi = 3.1415926f;
		a.startAngle      = startAngle.value_or(0) ;
		a.endAngle        = endAngle.value_or(0);
		if (a.startAngle > a.endAngle) {
			a.startAngle -= 360;
		}
		a.startAngle *= (M_PI / 180.0);
		a.endAngle *= (M_PI / 180.0);
		a.radius     = rectWidth.value_or(0.1) * number_scale;
		a.side       = layerMapper->toSide(layer);
		a.width           = width.value_or(0.1f) * number_scale;
		return a;
	}
};

struct PcbRect {
	std::optional<float> orient;
	std::string_view type = "rect";
	PCB_LAYER_ID layer;
	std::optional<int> netId;
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
		v.side  = layerMapper->toSide(layer);
		v.pos = toPt(position);
		if (to.has_value()) v.target_side = layerMapper->toSide(to.value());
		v.size = size.value_or(1) * number_scale;
		return v;
	}
};

struct PcbText  {
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
		t.side  = layerMapper->toSide(layer);
		t.netId  = netId.value_or(t.netId);
		t.pos  = toPt(position);
		t.text = text.value();
		return t;
	}
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
};


struct Net {
	std::string_view name;
	std::string_view info, diode, voltage;
};

} // namespace yamlfile
struct YamlFile {
	struct YamlFileRoot{
		std::vector<yamlfile::PcbTrack> track;
		std::vector<yamlfile::PcbText> text;
		std::vector<yamlfile::PcbArc> arc;
		std::vector<yamlfile::PcbVia> via;
		std::vector<yamlfile::PcbPad> pad;
		std::vector<yamlfile::PcbModule> module;
	} root;
	std::unordered_map<int, yamlfile::Net> nets;
	std::string name;
};

namespace iguana {
	template<>
	struct variant_type_field_helper<std::variant<yamlfile::PcbPad, yamlfile::PcbTrack>> : std::true_type {
		template<typename T>
	    constexpr std::string_view operator()(T *) {
		    return T{}.type;
	    };
	};
} 

XJsonFile::~XJsonFile() {
	
}

auto get_all_layer(YamlFile* file) {
	std::set<int> layers;
	ylt::reflection::for_each(file->root, [&](auto &filed, auto name, auto index) {
		for (const auto &v : filed) {
			layers.emplace(v.layer);
		}
	});
	return std::vector<int>{layers.cbegin(), layers.cend()};
}

XJsonFile::XJsonFile(std::vector<char> &b)
    : buf{std::move(b)} {

	scale = number_scale;
	try
	{
		file = std::make_unique<YamlFile>();
		iguana::from_json(*file, buf.begin(), buf.end());
	}
	catch(const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return;
	}
	yamlfile::layerMapper = std::make_unique<yamlfile::LayerMapper>();
	auto layers = get_all_layer(file.get());
	std::ranges::sort(layers);

	
	for (const auto &track : file->root.track) {
		if (track.layer == yamlfile::Board) {
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
		if (arc.layer == yamlfile::Board) {
			BRDArc a = arc;
			auto segments = arc_to_segments(a.startAngle, a.endAngle, a.radius, a.pos);
			std::move(segments.begin(), segments.end(), std::back_inserter(this->outline_segments));
		} else {
			arcs.push_back(arc);
		}
	}

	for (const auto& via : file->root.via) {
		vias.push_back(via);
	}

	for (const auto& pad : file->root.pad) {
		pins.push_back(pad);
		pins.back().part = parts.size() + 1;
		parts.push_back(BRDPart{
		    .name          = "..." + std::string{pad.name.value()},
			.mounting_side = yamlfile::layerMapper->toSide(pad.layer),
			.part_type = BRDPartType::SMD,
			.end_of_pins = (uint32_t)pins.size(),
		});
	}
	
	for (const auto &mod : file->root.module) {
		BRDPart part;
		part.name          = mod.text.value().text.value();
		part.mounting_side = yamlfile::layerMapper->toSide(mod.layer);
		part.part_type     = BRDPartType::SMD;
		int partId = parts.size() + 1;
		auto pinsz         = pins.size();
		for (const auto &item : mod.items) {
			std::visit(
			    [&part, this, partId](auto &p) {
				    using T = std::remove_cv_t<std::remove_reference_t<decltype(p)>>;
				    if constexpr (std::is_same_v<T, yamlfile::PcbTrack>) {
						const yamlfile::PcbTrack& t = p;
					    part.format.push_back(toPt(t.start));
					    part.format.push_back(toPt(t.end));
				    } else if constexpr(std::is_same_v<T, yamlfile::PcbPad>) {
						const yamlfile::PcbPad& t = p;
						pins.push_back(t);
						pins.back().part = partId;
				    }
			    },
			    item);
		}
		if (part.format.size() > 0) {
			std::ranges::sort(part.format,
			                  [](auto &l, auto &r) { return (((int64_t)l.x << 32) | l.y) < (((int64_t)r.x << 32) | r.y);
			});
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
		n.id = netid;
		n.info = net.info;
		n.name = net.name;
		n.diode_value = net.diode;
		nets[netid] = n;
	}
	num_parts  = parts.size();
	num_pins   = pins.size();
	num_format = format.size();
	num_nails  = nails.size();

	valid = num_parts > 0 || num_format > 0;
}

bool XJsonFile::verifyFormat(std::vector<char> &buf) {return false;}
