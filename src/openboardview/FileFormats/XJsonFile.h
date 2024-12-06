
#pragma once
#include <memory>

#include "BRDFileBase.h"

struct YamlFile;
struct XJsonFile : public BRDFileBase {
	XJsonFile(std::vector<char> &buf);
	~XJsonFile();
	
	static bool verifyFormat(std::vector<char> &buf);
private:
	std::vector<char> buf;	
	std::unique_ptr<YamlFile> file;
};
