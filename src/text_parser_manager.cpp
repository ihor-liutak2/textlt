#include "text_parser_manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace textlt {
namespace fs = std::filesystem;
namespace {

std::string ReadTextFile(const fs::path& path, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    error = "Cannot open file: " + path.string();
    return {};
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad()) {
    error = "Cannot read file: " + path.string();
    return {};
  }

  return buffer.str();
}

bool CopyFileIfMissing(const fs::path& source, const fs::path& destination,
                       std::string& error) {
  std::error_code ec;
  if (fs::exists(destination, ec)) {
    return true;
  }

  fs::create_directories(destination.parent_path(), ec);
  if (ec) {
    error = "Cannot create directory: " + destination.parent_path().string();
    return false;
  }

  fs::copy_file(source, destination, fs::copy_options::none, ec);
  if (ec) {
    error = "Cannot copy " + source.string() + " to " + destination.string() +
            ": " + ec.message();
    return false;
  }

  return true;
}

std::string GetRequiredString(const nlohmann::json& object, const char* key,
                              std::string& error) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    error = std::string("Missing or invalid parser field: ") + key;
    return {};
  }
  return object.at(key).get<std::string>();
}

std::string GetOptionalString(const nlohmann::json& object, const char* key,
                              const std::string& default_value) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    return default_value;
  }
  return object.at(key).get<std::string>();
}

int GetOptionalInt(const nlohmann::json& object, const char* key,
                   int default_value) {
  if (!object.contains(key)) {
    return default_value;
  }

  const auto& value = object.at(key);
  if (value.is_number_integer()) {
    return value.get<int>();
  }

  if (value.is_string()) {
    try {
      return std::stoi(value.get<std::string>());
    } catch (...) {
      return default_value;
    }
  }

  return default_value;
}

TextParserScope ParseScope(const std::string& value) {
  if (value == "paragraph") {
    return TextParserScope::Paragraph;
  }
  return TextParserScope::Text;
}

bool IsBlankLine(const std::string& line) {
  return std::all_of(line.begin(), line.end(), [](unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
  });
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;

  for (char ch : text) {
    if (ch == '\n') {
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
      }
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }

  if (!current.empty()) {
    if (current.back() == '\r') {
      current.pop_back();
    }
    lines.push_back(current);
  }

  return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string result;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      result += '\n';
    }
    result += lines[i];
  }
  return result;
}

std::unordered_map<std::string, std::string> MergeParams(
    const TextParserDefinition& definition,
    const std::unordered_map<std::string, std::string>& provided_params) {
  std::unordered_map<std::string, std::string> merged;

  for (const auto& param : definition.params) {
    merged[param.id] = param.default_value;
  }

  for (const auto& param : provided_params) {
    merged[param.first] = param.second;
  }

  return merged;
}

}  // namespace

fs::path TextParserManager::UserConfigDirectory() {
  const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
  if (xdg_config_home != nullptr && *xdg_config_home != '\0') {
    return fs::path(xdg_config_home) / "textlt";
  }

  const char* home = std::getenv("HOME");
  if (home != nullptr && *home != '\0') {
    return fs::path(home) / ".config" / "textlt";
  }

  return fs::current_path() / ".textlt";
}

fs::path TextParserManager::UserConfigFilePath() {
  return UserConfigDirectory() / "text_parsers.json";
}

bool TextParserManager::EnsureUserConfiguration(
    const fs::path& default_processors_directory, std::string& error) const {
  error.clear();

  const fs::path user_config_directory = UserConfigDirectory();
  const fs::path user_parser_directory = user_config_directory / "text_parsers";

  const fs::path default_config = default_processors_directory / "default_text_parsers.json";
  const fs::path user_config = UserConfigFilePath();

  std::error_code ec;
  if (!fs::exists(default_config, ec)) {
    error = "Default text parser config was not found: " + default_config.string();
    return false;
  }

  if (!CopyFileIfMissing(default_config, user_config, error)) {
    return false;
  }

  const fs::path default_parser_directory = default_processors_directory / "text_parsers";
  if (!fs::exists(default_parser_directory, ec)) {
    return true;
  }

  fs::create_directories(user_parser_directory, ec);
  if (ec) {
    error = "Cannot create parser directory: " + user_parser_directory.string();
    return false;
  }

  fs::directory_iterator directory_iterator(default_parser_directory, ec);
  if (ec) {
    error = "Cannot read default parser directory: " + default_parser_directory.string();
    return false;
  }

  for (const auto& entry : directory_iterator) {
    if (!entry.is_regular_file(ec) || entry.path().extension() != ".lua") {
      ec.clear();
      continue;
    }

    const fs::path destination = user_parser_directory / entry.path().filename();
    if (!CopyFileIfMissing(entry.path(), destination, error)) {
      return false;
    }
  }

  return true;
}

bool TextParserManager::LoadFromUserConfiguration(std::string& error) {
  return LoadFromFile(UserConfigFilePath(), error);
}

bool TextParserManager::LoadFromFile(const fs::path& config_path, std::string& error) {
  error.clear();
  parsers_.clear();
  config_base_directory_ = config_path.parent_path();

  const std::string content = ReadTextFile(config_path, error);
  if (!error.empty()) {
    return false;
  }

  nlohmann::json root;
  try {
    root = nlohmann::json::parse(content);
  } catch (const std::exception& ex) {
    error = std::string("Cannot parse text parser config: ") + ex.what();
    return false;
  }

  if (!root.contains("parsers") || !root.at("parsers").is_array()) {
    error = "Text parser config must contain parsers array.";
    return false;
  }

  for (const auto& parser_json : root.at("parsers")) {
    if (!parser_json.is_object()) {
      error = "Each parser entry must be an object.";
      return false;
    }

    TextParserDefinition definition;
    definition.id = GetRequiredString(parser_json, "id", error);
    if (!error.empty()) {
      return false;
    }

    definition.name = GetRequiredString(parser_json, "name", error);
    if (!error.empty()) {
      return false;
    }

    definition.scope = ParseScope(GetOptionalString(parser_json, "scope", "text"));
    definition.description = GetOptionalString(parser_json, "description", "");
    definition.repeat_default =
        std::max(1, GetOptionalInt(parser_json, "repeat_default", 1));

    const std::string script = GetRequiredString(parser_json, "script", error);
    if (!error.empty()) {
      return false;
    }
    definition.script_path = fs::path(script);

    if (parser_json.contains("params")) {
      if (!parser_json.at("params").is_array()) {
        error = "Parser params must be an array.";
        return false;
      }

      for (const auto& param_json : parser_json.at("params")) {
        if (!param_json.is_object()) {
          error = "Each parser param must be an object.";
          return false;
        }

        TextParserParam param;
        param.id = GetRequiredString(param_json, "id", error);
        if (!error.empty()) {
          return false;
        }

        param.label = GetOptionalString(param_json, "label", param.id);
        param.type = GetOptionalString(param_json, "type", "string");
        param.default_value = GetOptionalString(param_json, "default", "");
        definition.params.push_back(param);
      }
    }

    if (definition.params.size() > 4) {
      error = "Parser " + definition.id + " has more than 4 parameters.";
      return false;
    }

    parsers_.push_back(definition);
  }

  return true;
}

const std::vector<TextParserDefinition>& TextParserManager::GetParsers() const {
  return parsers_;
}

const TextParserDefinition* TextParserManager::FindParser(
    const std::string& parser_id) const {
  for (const auto& parser : parsers_) {
    if (parser.id == parser_id) {
      return &parser;
    }
  }
  return nullptr;
}

TextParserApplyResult TextParserManager::ApplyParser(
    const std::string& parser_id, const std::string& input_text,
    const std::unordered_map<std::string, std::string>& params, int repeat_count,
    const LuaParserLimits& limits) const {
  const TextParserDefinition* definition = FindParser(parser_id);
  if (definition == nullptr) {
    TextParserApplyResult result;
    result.error = "Text parser not found: " + parser_id;
    return result;
  }

  return ApplyDefinition(*definition, input_text, params, repeat_count, limits);
}

TextParserApplyResult TextParserManager::ApplyDefinition(
    const TextParserDefinition& definition, const std::string& input_text,
    const std::unordered_map<std::string, std::string>& params, int repeat_count,
    const LuaParserLimits& limits) const {
  TextParserApplyResult result;
  std::string error;

  fs::path script_path = definition.script_path;
  if (script_path.is_relative()) {
    script_path = config_base_directory_ / script_path;
  }

  const std::string script_text = ReadTextFile(script_path, error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  const auto merged_params = MergeParams(definition, params);
  const int requested_repeats = repeat_count > 0 ? repeat_count : definition.repeat_default;
  const int repeats = std::max(1, std::min(requested_repeats, 50));

  std::string current_text = input_text;

  for (int i = 0; i < repeats; ++i) {
    if (definition.scope == TextParserScope::Text) {
      LuaParserRunResult lua_result =
          lua_engine_.RunScript(script_text, current_text, merged_params, limits);
      if (!lua_result.success) {
        result.error = lua_result.error;
        return result;
      }
      current_text = std::move(lua_result.text);
      continue;
    }

    std::vector<std::string> lines = SplitLines(current_text);
    std::vector<std::string> output_lines;
    std::vector<std::string> paragraph_lines;

    auto flush_paragraph = [&]() -> bool {
      if (paragraph_lines.empty()) {
        return true;
      }

      const std::string paragraph_text = JoinLines(paragraph_lines);
      LuaParserRunResult lua_result =
          lua_engine_.RunScript(script_text, paragraph_text, merged_params, limits);
      if (!lua_result.success) {
        result.error = lua_result.error;
        return false;
      }

      output_lines.push_back(lua_result.text);
      paragraph_lines.clear();
      return true;
    };

    for (const auto& line : lines) {
      if (IsBlankLine(line)) {
        if (!flush_paragraph()) {
          return result;
        }
        if (!output_lines.empty() && !output_lines.back().empty()) {
          output_lines.push_back("");
        }
      } else {
        paragraph_lines.push_back(line);
      }
    }

    if (!flush_paragraph()) {
      return result;
    }

    current_text = JoinLines(output_lines);
  }

  result.success = true;
  result.text = std::move(current_text);
  return result;
}

}  // namespace textlt
