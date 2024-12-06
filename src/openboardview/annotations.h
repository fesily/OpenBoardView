#include "sqlite3.h"
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
	std::string diode;
	std::string voltage;
	std::string ohm;
	std::string ohm_black;
	PinVoltageFlag voltage_flag = PinVoltageFlag::unknown;

	explicit operator bool() const {
		return !(diode.empty() && voltage.empty() && ohm.empty() && ohm_black.empty() &&
		        voltage_flag == PinVoltageFlag::unknown);
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
	explicit operator bool() const {
		return !showname.empty();
	}
};

struct Annotations {
	std::string filename;
	sqlite3 *sqldb;
	bool debug = true;
	std::vector<Annotation> annotations;
	std::map<std::string, PartInfo> partInfos;
	std::map<std::string, NetInfo> netInfos;

	int Init(void);

	int SetFilename(const std::string &f);
	int Load(void);
	int Close(void);
	void Remove(int id);
	void Add(int side, double x, double y, const char *net, const char *part, const char *pin, const char *note);
	void Update(int id, char *note);
	void GenerateList(void);

	PartInfo& NewPartInfo(const char* partName);
	PinInfo& NewPinInfo(const char* partName, const char* pinName);
	NetInfo& NewNetInfo(const char* netName);
	void SavePinInfos();
	void RefreshPinInfos();
};

#endif
