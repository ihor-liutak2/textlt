#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace textlt {

struct LuaParserLimits {
  int timeout_ms = 1000;
  int instruction_hook_count = 10000;
  std::size_t max_memory_bytes = 32U * 1024U * 1024U;
  std::size_t max_output_bytes = 20U * 1024U * 1024U;
};

struct LuaParserRunResult {
  bool success = false;
  std::string text;
  std::string error;
};

class TextParserLuaEngine {
 public:
  LuaParserRunResult RunScript(
      const std::string& script_text,
      const std::string& input_text,
      const std::unordered_map<std::string, std::string>& params,
      const LuaParserLimits& limits = LuaParserLimits{}) const;
};

}  // namespace textlt
