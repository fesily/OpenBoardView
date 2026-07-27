#pragma once
#include "filesystem_impl.h"
#include <string>
#include <vector>
namespace obv {
std::vector<char> file_as_buffer(const filesystem::path &filepath, std::string &error_msg);
bool check_fileext(const filesystem::path &filepath, const std::string &fileext_lower);
} // namespace obv
