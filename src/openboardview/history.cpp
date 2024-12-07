#include <cmath>
#include <iostream>
#include <climits>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "history.h"
#include <iguana/json_reader.hpp>
#include <iguana/json_writer.hpp>

FHistory::~FHistory() {}

int FHistory::Set_filename(const std::string &name) {
	fname = name;
	return 0;
}

int FHistory::Load(void) {
	if (!fname.empty()) {

		try
		{
			history.clear();
			iguana::from_json_file(history, fname);
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << '\n';
			return -1;
		}
	}

	return history.size();
}

int FHistory::Prepend_save(const std::string &newfile) {
	if (!fname.empty()) {
		try
		{
			history.remove(newfile);
			history.push_front(newfile);
			std::string ss;
			iguana::to_json(history, ss);
			auto fp = fopen(fname.c_str(), "w");
			if (!fp) return -1;
			fwrite(ss.c_str(), sizeof(char), ss.size(), fp);
			fclose(fp);
			Load();
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << '\n';
			return -1;
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
