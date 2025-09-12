#include <cmath>
#include <iostream>
#include <climits>
#include <memory>
#include <cstdio>
#include <fstream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <cstring>

#include "history.h"

FHistory::~FHistory() {}

int FHistory::Set_filename(const std::string &name) {
	fname = name;
	return 0;
}

int FHistory::Load(void) {
	if (!fname.empty()) {
		std::ifstream fin(fname, std::ios::in);
		count = 0;
		if (!fin.is_open()) return 0;
		std::string line;
		while (count < FHISTORY_COUNT_MAX && std::getline(fin, line)) {
			// 去除结尾的 \r 或 \n
			size_t end = line.find_first_of("\r\n");
			if (end != std::string::npos) {
				line = line.substr(0, end);
			}
			std::u8string u8line = reinterpret_cast<const char8_t*>(line.c_str());
			strncpy(history[count], reinterpret_cast<const char*>(u8line.c_str()), FHISTORY_FNAME_LEN_MAX - 1);
			history[count][FHISTORY_FNAME_LEN_MAX - 1] = '\0';
			count++;
		}
		fin.close();
	} else {
		return -1;
	}
	return count;
}

int FHistory::Prepend_save(const std::u8string &newfile) {
	if (!fname.empty()) {
		std::ofstream fout(fname, std::ios::out | std::ios::trunc);
		if (fout.is_open()) {
			// u8string 转 std::string 写入
			fout << std::string(reinterpret_cast<const char*>(newfile.c_str())) << "\n";
			for (int i = 0; i < count; i++) {
				// Don't create duplicate entries, so check each one against the newfile
				if (newfile != reinterpret_cast<const char8_t*>(history[i])) {
					fout << std::string(history[i]) << "\n";
				}
			}
			fout.close();
			Load();
		}
	}
	return 0;
}

/**
 * Only displays the tail end of the filename path, where
 * 'stops' indicates how many paths up to truncate at
 *
 * PLD20160618-1729
 */
char *FHistory::Trim_filename(char *s, int stops) {

	int l   = strlen(s);
	char *p = s + l - 1;

	while ((stops) && (p > s)) {
		if ((*p == '/') || (*p == '\\')) stops--;
		p--;
	}
	if (!stops) p += 2;

	return p;
}
