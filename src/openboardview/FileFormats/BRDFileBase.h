#pragma once

#include <cstdlib>
#include <string>
#include <vector>
#include <map>

#define READ_INT() strtol(p, &p, 10);
// Warning: read as int then cast to uint if positive
#define READ_UINT                                \
	[&]() {                                      \
		int value = strtol(p, &p, 10);           \
		ENSURE(value >= 0, error_msg);           \
		return static_cast<unsigned int>(value); \
	}
#define READ_DOUBLE() strtod(p, &p)
#define READ_STR                                     \
	[&]() {                                          \
		while ((*p) && (isspace((uint8_t)*p))) ++p;  \
		s = p;                                       \
		while ((*p) && (!isspace((uint8_t)*p))) ++p; \
		*p = 0;                                      \
		p++;                                         \
		return fix_to_utf8(s, &arena, arena_end);    \
	}

struct BRDPoint {
	// mil (thou) is used here
	int x = 0;
	int y = 0;

	// Declaring these constructors wouldn't be required in C++17
	BRDPoint() = default;
	BRDPoint(int x, int y) : x(x), y(y) {}

	bool operator==(const BRDPoint& point) {
		return x == point.x && y == point.y;
	}
	bool operator!=(const BRDPoint& point) {
		return x != point.x || y != point.y;
	}
};

enum class BRDPartMountingSide { Both, Top, S1 = Top, S2, S3, S4, S5, S6, S7, S8, S9, S10, Bottom = S10 };
enum class BRDPartType { SMD, ThroughHole };

struct BRDPart {
	std::string name;
	std::string mfgcode;
	BRDPartMountingSide mounting_side{};
	BRDPartType part_type{}; // SMD or TH
	unsigned int end_of_pins = 0;
	BRDPoint p1{0, 0};
	BRDPoint p2{0, 0};
	std::vector<BRDPoint> format;
};

enum class BRDPinSide { Both, Top, S1 = Top, S2, S3, S4, S5, S6, S7, S8, S9, S10, Bottom = S10 };
enum class BPDPinShape { Fold, Circle, Rect };

struct BRDPin {
	BRDPoint pos;
	float angle = 0.0;
	BRDPoint top_size     = {0, 0};
	BPDPinShape top_shape = BPDPinShape::Circle;
	BRDPoint size     = {0, 0};
	BPDPinShape shape = BPDPinShape::Circle;
	BRDPoint bottom_size     = {0, 0};
	BPDPinShape bottom_shape = BPDPinShape::Circle;
	bool complex_draw       = false;
	int probe = 0;
	unsigned int part = 0;
	BRDPinSide side{};
	std::string net  = "UNCONNECTED";
	int netId = -1;
	double radius    = 7.0f;
	std::string snum;
	std::string name;
	std::string diode_vale;
	std::string voltage_value;

	bool operator<(const BRDPin &p) const // For sorting the vector
	{
		return part == p.part ? (std::string(snum) < std::string(p.snum)) : (part < p.part); // sort by part number then pin number
	}
};

struct BRDNail {
	unsigned int probe = 0;
	BRDPoint pos;
	BRDPartMountingSide side{};
	std::string net = "UNCONNECTED";
	int netId = -1;
};

struct BRDVia {
	BRDPoint pos;
	float size;
	BRDPartMountingSide side{};
	BRDPartMountingSide target_side{};
	std::string net = "UNCONNECTED";
	int netId = -1;
};

struct BRDTrack {
	std::pair<BRDPoint, BRDPoint> points;
	BRDPartMountingSide side{};
	float width = 1.0f;
	std::string net = "UNCONNECTED";
	int netId = -1;
};

struct BRDText {
	std::string net = "UNCONNECTED";
	int netId = -1;
	BRDPartMountingSide side{};
	std::string text;
	BRDPoint pos;
};

struct BRDArc {
	BRDPoint pos;
	BRDPartMountingSide side{};
	float radius;
	float width = 1.0;
	float startAngle, endAngle;
	std::string net = "UNCONNECTED";
	int netId = -1;
};

struct BRDNet {
	int id = -1;
	std::string name;
	std::string info;
	std::string diode_value;
};

class BRDFileBase {
  public:
	unsigned int num_format = 0;
	unsigned int num_parts  = 0;
	unsigned int num_pins   = 0;
	unsigned int num_nails  = 0;
	float scale = 1.0f;

	std::vector<BRDPoint> format;
	std::vector<std::pair<BRDPoint, BRDPoint>> outline_segments;
	std::vector<BRDPart> parts;
	std::vector<BRDPin> pins;
	std::vector<BRDNail> nails;
	std::vector<BRDTrack> tracks;
	std::vector<BRDVia> vias;
	std::vector<BRDArc> arcs;
	std::vector<BRDText> texts;
	std::map<int, BRDNet> nets;

	bool valid = false;
	std::string error_msg = "";

	virtual ~BRDFileBase()
	{
		free(file_buf);
		file_buf = nullptr;
	}
  protected:
	void AddNailsAsPins();
	BRDFileBase() {}
	// file_buf is used by some implementations. But since the derived class constructurs
	// are already passed a memory buffer most usages are "historic unneeded extra copies".
	char *file_buf = nullptr;

	std::vector<std::pair<BRDPoint, BRDPoint>> arc_to_segments(double startAngle, double endAngle, double r, BRDPoint pc);

	static double arc_slice_angle_rad;

	double distance(const BRDPoint &p1, const BRDPoint &p2);
};

void stringfile(char *buffer, std::vector<char*> &lines);
char *fix_to_utf8(char *s, char **arena, char *arena_end);
