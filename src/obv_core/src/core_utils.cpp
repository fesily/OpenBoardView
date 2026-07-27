#include "obv_core/core_utils.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace obv {

std::vector<char> file_as_buffer(const filesystem::path &filepath, std::string &error_msg) {
	std::vector<char> data;

	if (!filesystem::is_regular_file(filepath)) {
		error_msg = "Not a regular file";
		fprintf(stderr, "Error opening %s: %s\n", filepath.string().c_str(), error_msg.c_str());
		return data;
	}

	ifstream file;
	file.open(filepath, std::ios::in | std::ios::binary | std::ios::ate);

	if (!file.is_open()) {
		error_msg = strerror(errno);
		fprintf(stderr, "Error opening %s: %s\n", filepath.string().c_str(), error_msg.c_str());
		return data;
	}

	file.seekg(0, std::ios_base::end);
	std::streampos sz = file.tellg();
	if (sz < 0) {
		error_msg = std::string(__FILE__) + ":" + std::to_string(__LINE__) +
		            ": file_as_buffer: Assertion `sz >= 0' failed.";
		fprintf(stderr, "%s\n", error_msg.c_str());
		return data;
	}
	data.reserve(static_cast<size_t>(sz));
	file.seekg(0, std::ios_base::beg);
	data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	if (data.size() != static_cast<unsigned int>(sz)) {
		error_msg = std::string(__FILE__) + ":" + std::to_string(__LINE__) +
		            ": file_as_buffer: size mismatch after read.";
		fprintf(stderr, "%s\n", error_msg.c_str());
	}
	file.close();

	return data;
}

bool check_fileext(const filesystem::path &filepath, const std::string &fileext_lower) {
	std::string ext{filepath.extension().string()};
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext == fileext_lower;
}

} // namespace obv
