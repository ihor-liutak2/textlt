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
  Code,
};

enum class TextParserEngine {
  Lua,
  Builtin,
};

enum class TextParserOutput {
  ReplaceText,
  Report,
};

struct TextParserParam {
  std::string id;
  std::string label;
  std::string type;
  std::string default_value;
  std::string description;
  std::string decimal_separator = ".";
};

struct TextParserDefinition {
  std::string id;
  std::string name;
  TextParserScope scope = TextParserScope::Text;
  std::string description;
  std::string group = "Custom";
  TextParserEngine engine = TextParserEngine::Lua;
  TextParserOutput output = TextParserOutput::ReplaceText;
  std::filesystem::path script_path;
  std::string builtin_id;
  int repeat_default = 1;
  bool pinned = false;
  bool locked = false;
  std::vector<TextParserParam> params;
};

struct TextParserApplyResult {
  bool success = false;
  std::string text;
  std::string error;
  TextParserOutput output = TextParserOutput::ReplaceText;
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
  bool SetParserPinnedInUserConfiguration(
      const std::string& parser_id,
      bool pinned,
      std::string& error) const;

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
