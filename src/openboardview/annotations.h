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

// ── New annotation types ──────────────────────────────────────────────────────

// Annotation bound to a net: shown at every pin that belongs to the net.
struct NetAnnotation {
	std::string net;  // key
	std::string note;
	explicit operator bool() const { return !note.empty(); }
};

// Annotation bound to a specific pin of a specific part.
struct PinAnnotation {
	std::string partName;  // key (level 1)
	std::string pinName;   // key (level 2)
	std::string note;
	explicit operator bool() const { return !note.empty(); }
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

	// New annotation maps (persisted in YAML alongside partInfos/netInfos)
	std::map<std::string, NetAnnotation> netAnnotations;                          // net name → annotation
	std::map<std::string, std::map<std::string, PinAnnotation>> pinAnnotations;   // partName → pinName → annotation

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

	// New annotation CRUD
	NetAnnotation& NewNetAnnotation(const char* netName);
	PinAnnotation& NewPinAnnotation(const char* partName, const char* pinName);
	void RemoveNetAnnotation(const std::string& netName);
	void RemovePinAnnotation(const std::string& partName, const std::string& pinName);
};

#endif
