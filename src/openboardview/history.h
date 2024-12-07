#ifndef FHISTORY
#define FHISTORY
#include <list>
#include <string>
#define FHISTORY_COUNT_MAX 20
#define FHISTORY_FNAME_LEN_MAX 2048
struct FHistory {
	std::list<std::string> history;
	std::string fname;

	~FHistory();
	char *Trim_filename(char *s, int stops);
	int Set_filename(const std::string &name);
	int Load(void);
	int Prepend_save(const std::string &newfile);
};
#endif
