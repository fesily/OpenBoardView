#include <cmath>
#include <iostream>
#include <climits>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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
	    "NOTE TEXT );"
		"CREATE TABLE components ("
		"partName TEXT NOT NULL,"
		"pinName TEXT NOT NULL,"
		"diode TEXT DEFAULT '',"
		"voltage TEXT DEFAULT '',"
		"ohm TEXT DEFAULT '',"
		"ohm_black TEXT DEFAULT '',"
		"PRIMARY KEY (partName, pinName)"
		");"
		;

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


void Annotations::AddPinInfo(
    const char *partName, const char *pinName, const char *diode, const char *voltage, const char *ohm, const char *ohm_black) {
	char sql[10240];
	char *zErrMsg = 0;
	int r;

	sqlite3_snprintf(sizeof(sql),
	                 sql,
	                 "INSERT or REPLACE into components ( partName, pinName, diode, voltage, ohm, ohm_black) \
			values (  '%s', '%s', '%s', '%s', '%s', '%s' );",
	                 partName,
	                 pinName,
	                 diode,
	                 voltage,
	                 ohm,
	                 ohm_black);

	r = sqlite3_exec(sqldb, sql, NULL, 0, &zErrMsg);
	if (r != SQLITE_OK) {
		if (debug) fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		if (debug) fprintf(stdout, "Records created successfully\n");
	}
}

typedef struct {
    char partName[100];
    char pinName[100];
    char diode[100];
    char voltage[100];
    char ohm[100];
    char ohm_black[100];
} Component;

std::list<pinInfo> Annotations::GetPinInfos() {
	char *zErrMsg = 0;
	static std::list<Component> components;

	auto r = sqlite3_exec(sqldb, "select * from components", [](void *ptr, int argc, char **argv, char **azColName) {
		components.push_back({});
		Component *component = &components.back();
		strcpy(component->partName, argv[0] ? argv[0] : "");
		strcpy(component->pinName, argv[1] ? argv[1] : "");
		strcpy(component->diode, argv[2] ? argv[2] : "");
		strcpy(component->voltage, argv[3] ? argv[3] : "");
		strcpy(component->ohm, argv[4] ? argv[4] : "");
		strcpy(component->ohm_black, argv[5] ? argv[5] : "");
		return 0;
	}, 0, &zErrMsg);
	std::list<pinInfo> res;
	for (auto &component : components)
	{
		res.push_back(pinInfo{
			component.partName,
			component.pinName,
			component.diode,
			component.voltage,
			component.ohm,
			component.ohm_black
		});
	}

	if (r != SQLITE_OK) {
		if (debug) fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		if (debug) fprintf(stdout, "Records created successfully\n");
	}
	return res;
}
