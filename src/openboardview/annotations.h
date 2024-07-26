#include "sqlite3.h"
#include <list>
#include <map>
#ifndef __ANNOTATIONS
#define __ANNOTATIONS
#define ANNOTATION_FNAME_LEN_MAX 2048

struct Annotation {
	int id;
	int side;
	string note, net, part, pin;
	double x, y;
	bool hovered;
};

struct PinInfo {
	string partName;
	string pinName;
	string diode;
	string voltage;
	string ohm;
	string ohm_black;
};

struct PartInfo {
	string partName;
	string part_type;
	map<string, PinInfo> pins;
};

struct Annotations {
	std::string filename;
	sqlite3 *sqldb;
	bool debug = true;
	vector<Annotation> annotations;
	map<string, PartInfo> partInfos;

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
	void SavePinInfos();
	void RefreshPinInfos();
};

#endif
