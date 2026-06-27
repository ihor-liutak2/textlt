#pragma once

#include <filesystem>

namespace textlt {

void EnsureStartupResources();
void EnsureAssistantResources();
std::filesystem::path FindTextProcessorsDirectory();

} // namespace textlt
