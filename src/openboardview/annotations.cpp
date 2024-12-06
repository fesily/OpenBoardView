#include <cmath>
#include <iostream>
#include <climits>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <sstream>
#include <concepts>
#include <filesystem>
#define RYML_SINGLE_HDR_DEFINE_NOW 1
#include "../rapidyaml.hpp"
using namespace std;

#include "annotations.h"

int Annotations::SetFilename(const std::string &f) {
	filename = f;
	return 0;
}

/*
static int sqlCallback(void *NotUsed, int argc, char **argv, char **azColName) {
    int i;
    for (i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}
*/

int Annotations::Init(void) {

	char *zErrMsg = 0;
	int rc;
	//	char sql_table_test[] = "SELECT name FROM sqlite_master WHERE type='table' AND name='annotations'";
	char sql_table_create[] =
	    "CREATE TABLE annotations("
	    "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
	    "VISIBLE INTEGER,"
	    "PIN TEXT,"
	    "PART TEXT,"
	    "NET TEXT,"
	    "POSX INTEGER,"
	    "POSY INTEGER,"
	    "SIDE INTEGER,"
	    "NOTE TEXT );";

	if (!sqldb) return 1;

	/* Execute SQL statement */
	rc = sqlite3_exec(sqldb, sql_table_create, NULL, 0, &zErrMsg);
	if (rc != SQLITE_OK) {
		if (debug) fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		if (debug) fprintf(stdout, "Table created successfully\n");
	}

	return 0;
}
namespace c4::yml {
	void write(c4::yml::NodeRef *node, const PinInfo &pi) {
		(*node) |= c4::yml::MAP;
		if (!pi.diode.empty()) node->append_child() << key("diode") << pi.diode;
		if (!pi.voltage.empty()) node->append_child() << key("voltage") << pi.voltage;
		if (!pi.ohm.empty()) node->append_child() << key("ohm") << pi.ohm;
		if (!pi.ohm_black.empty()) node->append_child() << key("ohm_black") << pi.ohm_black;
		if (pi.voltage_flag != PinVoltageFlag::unknown) node->append_child() << key("voltage_flag") << pi.voltage_flag;
	}

	bool read(const c4::yml::ConstNodeRef& node, PinInfo* pi) {
		if (node.has_child("diode")) node["diode"] >> pi->diode;
		if (node.has_child("voltage")) node["voltage"] >> pi->voltage;
		if (node.has_child("ohm")) node["ohm"] >> pi->ohm;
		if (node.has_child("ohm_black")) node["ohm_black"] >> pi->ohm_black;
		if (node.has_child("voltage_flag")) node["voltage_flag"] >> pi->voltage_flag;
		return true;
	}

	void write(c4::yml::NodeRef *node, const PartInfo &pi) {
		(*node) |= c4::yml::MAP;
		if (!pi.part_type.empty()) node->append_child() << key("part_type") << pi.part_type;
		if (!pi.pins.empty()) node->append_child() << key("pins") << pi.pins;
	    if (pi.angle != PartAngle::_0) node->append_child() << key("angle") << pi.angle;
	}

	bool read(const c4::yml::ConstNodeRef& node, PartInfo* pi) {
		if (node.has_child("part_type")) node["part_type"] >> pi->part_type;
		if (node.has_child("pins")) node["pins"] >> pi->pins;
		if (node.has_child("angle")) node["angle"] >> pi->angle;
		return true;
	}

	void write(c4::yml::NodeRef *node, const NetInfo &net) {
		(*node) |= c4::yml::MAP;
		if (!net.showname.empty()) node->append_child() << key("showname") << net.showname;
	}

	bool read(const c4::yml::ConstNodeRef& node, NetInfo* net) {
		if (node.has_child("showname")) node["showname"] >> net->showname;
		return true;
	}
}

static void serialize(const Annotations& ann, const std::string& filename) {
    ryml::Tree tree;
    auto root = tree.rootref();
	root |= c4::yml::MAP;
	root.append_child() << c4::yml::key("Version") << "0.0.2";
	if (!ann.partInfos.empty())
		root.append_child() << c4::yml::key("PartInfos") << ann.partInfos;
	if (!ann.netInfos.empty())
		root.append_child() << c4::yml::key("NetInfos") << ann.netInfos;
    std::ofstream fout(filename, std::ios_base::trunc|std::ios_base::out);
    fout << tree;
}

static void deserialize(Annotations& ann, const std::string& filename) {
	if (!filesystem::exists(filename)) return;
    std::ifstream fin(filename);
    std::stringstream buffer;
    buffer << fin.rdbuf();
	auto buf = buffer.str();
    auto tree = ryml::parse_in_place(ryml::substr{(char*)buf.data(), buf.size()});
    auto root = tree.rootref();

	auto version = root["Version"];
	auto partInfos = root["PartInfos"];
	auto netInfos = root["NetInfos"];
	if (!partInfos.empty()) {
		for (auto child1 : partInfos.children()) {
			std::string partName = {child1.key().str, child1.key().size()};
			PartInfo partInfo;
			child1 >> partInfo;
			partInfo.partName = partName;
			for (auto& pin: partInfo.pins) {
				pin.second.partName = partName;
				pin.second.pinName = pin.first;
			}
			ann.partInfos[partName] = partInfo;
		}
	}

	if (!netInfos.empty()) {
		for (auto child1 : netInfos.children()) {
			std::string netName = {child1.key().str, child1.key().size()};
			NetInfo netInfo;
			child1 >> netInfo;
			netInfo.name = netName;
			ann.netInfos[netName] = netInfo;
		}
	}
}

int Annotations::Load(void) {
	std::string sqlfn                        = filename;
	auto pos                                 = sqlfn.rfind('.');
	if (pos != std::string::npos) sqlfn[pos] = '_';
	sqlfn += ".sqlite3";

	sqldb = nullptr;
	int r = sqlite3_open(sqlfn.c_str(), &sqldb);
	if (r) {
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(sqldb));
	} else {
		if (debug) fprintf(stderr, "Opened database successfully\n");
		Init();
		GenerateList();
	}
	return 0;
}

int Annotations::Close(void) {
	if (sqldb) {
		sqlite3_close(sqldb);
		sqldb = NULL;
	}

	return 0;
}

void Annotations::GenerateList(void) {
	sqlite3_stmt *stmt;
	char sql[] = "SELECT id,side,posx,posy,net,part,pin,note from annotations where visible=1;";
	int rc;

	rc = sqlite3_prepare_v2(sqldb, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		if (debug) cerr << "SELECT failed: " << sqlite3_errmsg(sqldb) << endl;
		return; // or throw
	}

	annotations.clear();
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Annotation ann;
		ann.id      = sqlite3_column_int(stmt, 0);
		ann.side    = sqlite3_column_int(stmt, 1);
		ann.x       = sqlite3_column_int(stmt, 2);
		ann.y       = sqlite3_column_int(stmt, 3);
		ann.hovered = false;

		const char *p = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
		if (!p) p     = "";
		ann.net       = p;

		p         = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
		if (!p) p = "";
		ann.part  = p;

		p         = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
		if (!p) p = "";
		ann.pin   = p;

		p         = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
		if (!p) p = "";
		ann.note  = p;

		if (debug)
			fprintf(stderr,
			        "%d(%d:%f,%f) Net:%s Part:%s Pin:%s: Note:%s\nAdded\n",
			        ann.id,
			        ann.side,
			        ann.x,
			        ann.y,
			        ann.net.c_str(),
			        ann.part.c_str(),
			        ann.pin.c_str(),
			        ann.note.c_str());

		annotations.push_back(ann);
	}
	if (rc != SQLITE_DONE) {
		if (debug) cerr << "SELECT failed: " << sqlite3_errmsg(sqldb) << endl;
		// if you return/throw here, don't forget the finalize
	}
	sqlite3_finalize(stmt);
}

void Annotations::Add(int side, double x, double y, const char *net, const char *part, const char *pin, const char *note) {
	char sql[10240];
	char *zErrMsg = 0;
	int r;

	sqlite3_snprintf(sizeof(sql),
	                 sql,
	                 "INSERT into annotations ( visible, side, posx, posy, net, part, pin, note ) \
			values ( 1, %d, %0.0f, %0.0f, '%s', '%s', '%s', '%q' );",
	                 side,
	                 x,
	                 y,
	                 net,
	                 part,
	                 pin,
	                 note);

	r = sqlite3_exec(sqldb, sql, NULL, 0, &zErrMsg);
	if (r != SQLITE_OK) {
		if (debug) fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		if (debug) fprintf(stdout, "Records created successfully\n");
	}
}

void Annotations::Remove(int id) {
	char sql[1024];
	char *zErrMsg = 0;
	int r;

	sqlite3_snprintf(sizeof(sql), sql, "UPDATE annotations set visible = 0 where id=%d;", id);
	r = sqlite3_exec(sqldb, sql, NULL, 0, &zErrMsg);
	if (r != SQLITE_OK) {
		if (debug) fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		if (debug) fprintf(stdout, "Records created successfully\n");
	}
}

void Annotations::Update(int id, char *note) {
	char sql[10240];
	char *zErrMsg = 0;
	int r;

	sqlite3_snprintf(sizeof(sql), sql, "UPDATE annotations set note = '%q' where id=%d;", note, id);
	r = sqlite3_exec(sqldb, sql, NULL, 0, &zErrMsg);
	if (r != SQLITE_OK) {
		if (debug) fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		if (debug) fprintf(stdout, "Records created successfully\n");
	}
}

PartInfo& Annotations::NewPartInfo(const char* partName) {
	auto& i = partInfos[partName];
	i.partName = partName;
	return i;
}


PinInfo& Annotations::NewPinInfo(const char* partName, const char* pinName) {
	auto& pinInfo = NewPartInfo(partName).pins[pinName];
	pinInfo.partName = partName;
	pinInfo.pinName = pinName;
	return pinInfo;
}

NetInfo& Annotations::NewNetInfo(const char* netname) {
	auto& n = netInfos[netname];
	n.name = netname;
	return n;
}

void Annotations::SavePinInfos() {
	for (auto &[part_name, part_info] : partInfos) {
		std::erase_if(part_info.pins, [](auto &p) { return !p.second; });
	}
	std::erase_if(partInfos, [](auto &p) { return !p.second; });
	serialize(*this, filename + ".yaml");
}

void Annotations::RefreshPinInfos() {
	partInfos.clear();
	netInfos.clear();
	deserialize(*this, filename + ".yaml");
}
