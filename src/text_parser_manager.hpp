#pragma once

#include "text_parser_lua_engine.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace textlt {

enum class TextParserScope {
  Text,
  Paragraph,
};

struct TextParserParam {
  std::string id;
  std::string label;
  std::string type;
  std::string default_value;
};

struct TextParserDefinition {
  std::string id;
  std::string name;
  TextParserScope scope = TextParserScope::Text;
  std::string description;
  std::filesystem::path script_path;
  int repeat_default = 1;
  std::vector<TextParserParam> params;
};

struct TextParserApplyResult {
  bool success = false;
  std::string text;
  std::string error;
};

class TextParserManager {
 public:
  static std::filesystem::path UserConfigDirectory();
  static std::filesystem::path UserConfigFilePath();

  bool EnsureUserConfiguration(const std::filesystem::path& default_processors_directory,
                               std::string& error) const;

  bool LoadFromFile(const std::filesystem::path& config_path,
                    std::string& error);
  bool LoadFromUserConfiguration(std::string& error);

  const std::vector<TextParserDefinition>& GetParsers() const;
  const TextParserDefinition* FindParser(const std::string& parser_id) const;

  TextParserApplyResult ApplyParser(
      const std::string& parser_id,
      const std::string& input_text,
      const std::unordered_map<std::string, std::string>& params,
      int repeat_count = 0,
      const LuaParserLimits& limits = LuaParserLimits{}) const;

 private:
  std::filesystem::path config_base_directory_;
  std::vector<TextParserDefinition> parsers_;
  TextParserLuaEngine lua_engine_;

  TextParserApplyResult ApplyDefinition(
      const TextParserDefinition& definition,
      const std::string& input_text,
      const std::unordered_map<std::string, std::string>& params,
      int repeat_count,
      const LuaParserLimits& limits) const;
};

}  // namespace textlt
