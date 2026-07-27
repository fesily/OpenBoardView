#ifdef HAVE_SQLITE3
#include "sqlite3.h"
#endif
#include <list>
#include <map>
#ifndef __ANNOTATIONS
#define __ANNOTATIONS
#define ANNOTATION_FNAME_LEN_MAX 2048

struct Annotation {
	int id;
	int side;
	std::string note, net, part, pin;
	double x, y;
	bool hovered;
};


enum class PinVoltageFlag {
	unknown,
	input,
	output,
};

struct PinInfo {
	std::string partName;
    std::string pinName;
	std::string show_name;
    std::string diode;
    std::string voltage;
    std::string ohm;
    std::string ohm_black;
	std::string note;
	PinVoltageFlag voltage_flag = PinVoltageFlag::unknown;

	explicit operator bool() const {
		return !(show_name.empty() && diode.empty() && voltage.empty() && ohm.empty() && ohm_black.empty() &&
		        note.empty() && voltage_flag == PinVoltageFlag::unknown);
	}
};

enum class PartAngle {
	_0,
	_270,
	_180,
	_90,
	sorted,
};
struct PartInfo {
    std::string partName;
    std::string part_type;
	PartAngle angle = PartAngle::_0;
    std::map<std::string, PinInfo> pins;
	explicit operator bool() const {
		return !(part_type.empty() && angle == PartAngle::_0 && pins.empty());
	}
};

struct NetInfo {
	std::string name;
	std::string showname;
	std::string note;
	explicit operator bool() const {
		return !(showname.empty() && note.empty());
	}
};

struct Annotations {
	std::string filename;
#ifdef HAVE_SQLITE3
	sqlite3 *sqldb = nullptr;
#endif
	bool debug = false;
	std::vector<Annotation> annotations;
	std::map<std::string, PartInfo> partInfos;
	std::map<std::string, NetInfo> netInfos;

	int Init(void);

	int SetFilename(const std::string &f);
	int Load(void);
	int Close(void);
	int Remove(int id);
	int Add(int side, double x, double y, const char *net, const char *part, const char *pin, const char *note);
	int Update(int id, const char *note);
	void GenerateList(void);

	PartInfo& NewPartInfo(const char* partName);
	PinInfo& NewPinInfo(const char* partName, const char* pinName);
	NetInfo& NewNetInfo(const char* netName);
	void SavePinInfos();
	void RefreshPinInfos();
};

#endif
