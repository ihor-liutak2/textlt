#pragma once

#include <string>
#include <vector>

namespace textlt {

std::string EnsureTextExtension(std::string path);
std::string TrimTrailingNewlines(std::string value);
std::vector<std::string> LoadTextFileLines(const std::string& path);

} // namespace textlt
