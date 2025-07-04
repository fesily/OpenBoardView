#pragma once

#include "FileFormats/BRDFile.h"

#include "imgui/imgui.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define EMPTY_STRING ""
#define kBoardComponentPrefix "c_"
#define kBoardPinPrefix "p_"
#define kBoardNetPrefix "n_"
#define kBoardTrackPrefix "t_"
#define kBoardViaPrefix "v_"
#define kBoardArcPrefix "a_"
#define kBoardElementNameLength 127

using namespace std;
#include "annotations.h"

struct Point;
struct BoardElement;
struct Net;
struct Pin;
struct Component;

typedef function<void(const char *)> TcharStringCallback;
typedef function<void(BoardElement *)> TboardElementCallback;

template <class T>
using SharedVector = vector<shared_ptr<T>>;

template <class T>
using SharedStringMap = map<string, shared_ptr<T>>;

enum EBoardSide {
	kBoardSideBoth,
	kBoardSideS1,
	kBoardSideS2,
	kBoardSideS3,
	kBoardSideS4,
	kBoardSideS5,
	kBoardSideS6,
	kBoardSideS7,
	kBoardSideS8,
	kBoardSideS9,
	kBoardSideS10 ,
	kBoardSideBottom = kBoardSideS10,
	kBoardSideTop = kBoardSideS1,
};

enum EShapeType {
	kShapeTypeFold = 0,
	kShapeTypeCircle  = 1,
	kShapeTypeRect = 2,
};

// Checking whether str `prefix` is a prefix of str `base`.
inline static bool is_prefix(string prefix, string base) {
	if (prefix.size() > base.size()) return false;

	auto res = mismatch(prefix.begin(), prefix.end(), base.begin());
	if (res.first == prefix.end()) return true;

	return false;
}

template <class T>
inline static bool contains(const T &element, vector<T> &v) {
	return find(begin(v), end(v), element) != end(v);
}

template <class T>
inline static bool contains(T &element, vector<T> &v) {
	return find(begin(v), end(v), element) != end(v);
}

template <class T>
inline void remove(T &element, vector<T> &v) {

	auto it = std::find(v.begin(), v.end(), element);

	if (it != v.end()) {
		using std::swap;
		swap(*it, v.back());
		v.pop_back();
	}
}

// Any element being on the board.
struct BoardElement {
	// Side of the board the element is located. (top, bottom, both?)
	EBoardSide board_side = kBoardSideBoth;

	// String uniquely identifying this element on the board.
	virtual string UniqueId() const = 0;
};

// A point/position on the board relative to top left corner of the board.
// TODO: not sure how different formats will store this info.
struct Point {
	float x, y;

	Point()
	    : x(0.0f)
	    , y(0.0f){};
	Point(float _x, float _y)
	    : x(_x)
	    , y(_y){};
	Point(int _x, int _y)
	    : x(float(_x))
	    , y(float(_y)){};
};

// Shared potential between multiple Pins/Contacts.
struct Net : BoardElement {
	int number;
	string name;
	bool is_ground;

	SharedVector<Pin> pins;

	string UniqueId() const {
		return kBoardNetPrefix + name;
	}

	std::vector<const std::string *> searchableStringDetails() const;
};

struct Track: BoardElement {

	Point position_start;

	Point position_end;

	float width = 1.0f;

	mutable string name;

	string UniqueId() const {
		if (name.empty()) {
			name = std::to_string(position_start.x) + ":" + std::to_string(position_start.y);
		}
		return kBoardTrackPrefix + name;
	}

	Net *net;
};

struct Via: BoardElement {

	Point position;

	EBoardSide target_side = kBoardSideBoth;

	float size = 0.0;

	mutable string name;

	string UniqueId() const {
		if (name.empty()) {
			name = std::to_string(position.x) + ":" + std::to_string(position.y);
		}
		return kBoardViaPrefix + name;
	}

	Net *net;
};

struct PcbArc: BoardElement {

	Point position;

	float radius = 0.0;
	
	float startAngle = 0.0;

	float endAngle = 0.0;

	mutable string name;

	string UniqueId() const {
		if (name.empty()) {
			name = std::to_string(position.x) + ":" + std::to_string(position.y);
		}
		return kBoardArcPrefix + name;
	}

	Net *net;
};

// Any observeable contact (nails, component pins).
// Convieniently/Confusingly named Pin not Contact here.
struct Pin : BoardElement {
	enum EPinType {
		kPinTypeUnkown = 0,
		kPinTypeNotConnected,
		kPinTypeComponent,
		kPinTypeVia,
		kPinTypeTestPad,
	};

	// Type of Contact, e.g. pin, via, probe/test point.
	EPinType type;

	// Pin number / Nail count.
	string number;

	string name; // for BGA pads will be AZ82 etc

	EShapeType shape = EShapeType::kShapeTypeCircle;
	// Position according to board file. (probably in inches)
	Point position;
	// Contact diameter, e.g. via or pin size. (probably in inches)
	float diameter;

	// Rect size
	Point size;
	// Rect angle
	int angle;

	// Net this contact is connected to, nulltpr if no info available.
	Net *net;

	// Contact belonging to this component (pin), nullptr if nail.
	std::shared_ptr<Component> component;

	string diode_value; // the pin diode

	string voltage_value; // the pin voltage

	string ohm_value;

	string ohm_black_value;

	PinVoltageFlag voltage_flag = PinVoltageFlag::unknown;

	string UniqueId() const {
		return kBoardPinPrefix + number;
	}
};

// A component on the board having multiple Pins.
struct Component : BoardElement {
	enum EMountType { kMountTypeUnknown = 0, kMountTypeSMD, kMountTypeDIP };

	enum EComponentType {
		kComponentTypeUnknown = 0,
		kComponentTypeDummy,
		kComponentTypeConnector,
		kComponentTypeIC,
		kComponentTypeResistor,
		kComponentTypeCapacitor,
		kComponentTypeDiode,
		kComponentTypeTransistor,
		kComponentTypeCrystal,
		kComponentTypeJellyBean,
		kComponentTypeBoard,
		kComponentTypeInductor 
	};

	// How the part is attached to the board, either SMD, .., through-hole?
	EMountType mount_type = kMountTypeUnknown;

	// Type of component, eg. resistor, cap, etc.
	EComponentType component_type = kComponentTypeUnknown;

	// Part name as stored in board file.
	string name;

	// Part type
	string part_type;

	// Part manufacturing code (aka. part number).
	string mfgcode;

	// Pins belonging to this component.
	SharedVector<Pin> pins;

	// Post calculated outlines
	//
	std::array<ImVec2, 4> outline;
	std::array<ImVec2, 4> special_outline;
	bool is_special_outline = false;
	Point p1{0.0f, 0.0f}, p2{0.0f, 0.0f}; // for debugging

	bool outline_done = false;
	std::vector<ImVec2> hull;
	ImVec2 omin, omax;
	ImVec2 centerpoint;
	double expanse = 0.0f; // quick measure of distance between pins.

	PartAngle angle = PartAngle::unknown;

	// enum ComponentVisualModes { CVMNormal = 0, CVMSelected, CVMShowPins, CVMModeCount };
	enum ComponentVisualModes { CVMNormal = 0, CVMSelected, CVMModeCount };

	uint8_t visualmode = 0;

	// Mount type as readable string.
	string mount_type_str() {
		switch (mount_type) {
			case Component::kMountTypeSMD: return "SMD";
			case Component::kMountTypeDIP: return "DIP";
			default: return "UNKNOWN";
		}
	}

	// true if component is not representing a real/physical component.
	bool is_dummy() {
		return component_type == kComponentTypeDummy;
	}

	string UniqueId() const {
		return kBoardComponentPrefix + name;
	}

	void set_part_type(const string& part_type) {
		if (part_type.empty()) return;
		const auto t = part_type[0];
		switch (t) {
			case 'R':
			case 'r':
				component_type = kComponentTypeResistor;
				break;
			case 'c':
			case 'C':
				component_type = kComponentTypeCapacitor;
				break;
			case 'l':
			case 'L':
				component_type = kComponentTypeInductor;
				break;
		}
		this->part_type = part_type;
	}

	std::vector<const std::string *> searchableStringDetails() const;
};

class Board {
  public:
	enum EBoardType { kBoardTypeUnknown = 0, kBoardTypeBRD = 0x01, kBoardTypeBDV = 0x02 };

	virtual ~Board() {}

	virtual SharedVector<Net> &Nets()                               = 0;
	virtual SharedVector<Component> &Components()                   = 0;
	virtual SharedVector<Pin> &Pins()                               = 0;
	virtual SharedVector<Point> &OutlinePoints()                    = 0;
	virtual SharedVector<Track> &Tracks()                           = 0;
	virtual SharedVector<Via> &Vias()                               = 0;
	virtual SharedVector<PcbArc> &arcs()                               = 0;
	virtual std::vector<std::pair<Point, Point>> &OutlineSegments() = 0;
	virtual std::vector<EBoardSide> &AllSide() = 0;

	EBoardType BoardType() {
		return kBoardTypeUnknown;
	}
};
