#include "builtin_text_processors.hpp"

#include "text_transformer.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace textlt {
namespace {

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

std::string GetParam(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name,
    const std::string& default_value) {
  const auto it = params.find(name);
  if (it == params.end()) {
    return default_value;
  }
  return it->second;
}

bool ParsePositiveSize(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name,
    std::size_t min_value,
    std::size_t max_value,
    std::size_t& value,
    std::string& error) {
  const std::string raw_value = GetParam(params, name, "");
  if (raw_value.empty()) {
    error = "Missing parameter: " + name;
    return false;
  }

  try {
    std::size_t parsed = 0;
    const unsigned long long number = std::stoull(raw_value, &parsed, 10);
    if (parsed != raw_value.size()) {
      error = "Parameter " + name + " must be an integer.";
      return false;
    }
    if (number < min_value || number > max_value) {
      error = "Parameter " + name + " must be between " +
          std::to_string(min_value) + " and " + std::to_string(max_value) + ".";
      return false;
    }
    value = static_cast<std::size_t>(number);
    return true;
  } catch (...) {
    error = "Parameter " + name + " must be an integer.";
    return false;
  }
}

transform::CursorState FullTextCursor(const std::vector<std::string>& lines) {
  if (lines.empty()) {
    return {};
  }

  return {lines.back().size(), lines.size() - 1};
}

transform::SelectionState FullTextSelection(const std::vector<std::string>& lines) {
  transform::SelectionState selection;
  selection.active = !lines.empty();
  selection.anchor_x = 0;
  selection.anchor_y = 0;
  return selection;
}

TextParserParam IntegerParam(
    std::string id,
    std::string label,
    std::string default_value,
    std::string description) {
  TextParserParam param;
  param.id = std::move(id);
  param.label = std::move(label);
  param.type = "integer";
  param.default_value = std::move(default_value);
  param.description = std::move(description);
  return param;
}

TextParserDefinition BuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::vector<TextParserParam> params = {}) {
  TextParserDefinition definition;
  definition.id = std::move(id);
  definition.name = std::move(name);
  definition.scope = TextParserScope::Code;
  definition.description = std::move(description);
  definition.engine = TextParserEngine::Builtin;
  definition.builtin_id = definition.id;
  definition.locked = true;
  definition.repeat_default = 1;
  definition.params = std::move(params);
  return definition;
}

BuiltinTextProcessorResult RunTransformer(
    const std::string& input_text,
    const std::function<transform::TransformResult(
        std::vector<std::string>&,
        transform::CursorState,
        transform::SelectionState)>& callback) {
  BuiltinTextProcessorResult result;
  std::vector<std::string> lines = SplitLines(input_text);
  const bool had_final_newline = !input_text.empty() && input_text.back() == '\n';
  const transform::CursorState cursor = FullTextCursor(lines);
  const transform::SelectionState selection = FullTextSelection(lines);
  const transform::TransformResult transform_result = callback(lines, cursor, selection);

  result.success = true;
  if (!transform_result.changed) {
    result.text = input_text;
    return result;
  }

  result.text = JoinLines(lines);
  if (had_final_newline) {
    result.text += '\n';
  }
  return result;
}

}  // namespace

std::vector<TextParserDefinition> CreateBuiltinTextProcessors() {
  return {
      BuiltinDefinition(
          "builtin_indent_lines",
          "Indent lines",
          "Adds spaces to the beginning of each selected line.",
          {IntegerParam(
              "indent_width",
              "Indent width",
              "4",
              "Number of spaces to insert at the beginning of each line.")}),
      BuiltinDefinition(
          "builtin_outdent_lines",
          "Outdent lines",
          "Removes one indentation level from each selected line.",
          {IntegerParam(
              "indent_width",
              "Indent width",
              "4",
              "Maximum number of spaces to remove from the beginning of each line.")}),
      BuiltinDefinition(
          "builtin_convert_4_spaces_to_2",
          "Convert 4 spaces to 2",
          "Converts leading indentation blocks of four spaces into two spaces."),
      BuiltinDefinition(
          "builtin_convert_2_spaces_to_4",
          "Convert 2 spaces to 4",
          "Converts leading indentation blocks of two spaces into four spaces."),
      BuiltinDefinition(
          "builtin_toggle_case",
          "Toggle case",
          "Toggles letter case in the selected text."),
  };
}

bool IsBuiltinTextProcessor(const std::string& processor_id) {
  const std::vector<TextParserDefinition> processors = CreateBuiltinTextProcessors();
  return std::any_of(processors.begin(), processors.end(), [&](const TextParserDefinition& processor) {
    return processor.id == processor_id || processor.builtin_id == processor_id;
  });
}

BuiltinTextProcessorResult ApplyBuiltinTextProcessor(
    const TextParserDefinition& definition,
    const std::string& input_text,
    const std::unordered_map<std::string, std::string>& params) {
  if (definition.builtin_id == "builtin_indent_lines") {
    std::size_t indent_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "indent_width", 1, 64, indent_width, error)) {
      return {false, {}, error};
    }
    return RunTransformer(input_text, [indent_width](
        std::vector<std::string>& lines,
        transform::CursorState cursor,
        transform::SelectionState selection) {
      return transform::IndentLines(lines, cursor, selection, indent_width);
    });
  }

  if (definition.builtin_id == "builtin_outdent_lines") {
    std::size_t indent_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "indent_width", 1, 64, indent_width, error)) {
      return {false, {}, error};
    }
    return RunTransformer(input_text, [indent_width](
        std::vector<std::string>& lines,
        transform::CursorState cursor,
        transform::SelectionState selection) {
      return transform::OutdentLines(lines, cursor, selection, indent_width);
    });
  }

  if (definition.builtin_id == "builtin_convert_4_spaces_to_2") {
    return RunTransformer(input_text, [](std::vector<std::string>& lines,
                                         transform::CursorState cursor,
                                         transform::SelectionState selection) {
      return transform::Convert4To2Spaces(lines, cursor, selection);
    });
  }

  if (definition.builtin_id == "builtin_convert_2_spaces_to_4") {
    return RunTransformer(input_text, [](std::vector<std::string>& lines,
                                         transform::CursorState cursor,
                                         transform::SelectionState selection) {
      return transform::Convert2To4Spaces(lines, cursor, selection);
    });
  }

  if (definition.builtin_id == "builtin_toggle_case") {
    return RunTransformer(input_text, [](std::vector<std::string>& lines,
                                         transform::CursorState cursor,
                                         transform::SelectionState selection) {
      return transform::ToggleCase(lines, cursor, selection);
    });
  }

  return {false, {}, "Unknown built-in text processor: " + definition.builtin_id};
}

}  // namespace textlt
