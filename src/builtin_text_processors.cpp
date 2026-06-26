#include "builtin_text_processors.hpp"

#include "text_transformer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace textlt {
namespace {

enum class SortLanguage {
  Auto,
  Binary,
  English,
  Russian,
  Ukrainian,
};

enum class CaseMode {
  Toggle,
  Upper,
  Lower,
  Title,
  Sentence,
  Snake,
  Camel,
  Pascal,
  Kebab,
};

struct SortToken {
  int group = 0;
  int weight = 0;
  int case_weight = 0;
  std::uint32_t codepoint = 0;
};

std::string ToLowerAscii(std::string value);

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

bool ParseBoolParam(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name,
    bool default_value,
    bool& value,
    std::string& error) {
  std::string raw_value = GetParam(params, name, default_value ? "true" : "false");
  std::transform(raw_value.begin(), raw_value.end(), raw_value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (raw_value == "1" || raw_value == "true" || raw_value == "yes" || raw_value == "on") {
    value = true;
    return true;
  }

  if (raw_value == "0" || raw_value == "false" || raw_value == "no" || raw_value == "off") {
    value = false;
    return true;
  }

  error = "Parameter " + name + " must be true or false.";
  return false;
}

bool IsBlankLine(const std::string& line) {
  return std::all_of(line.begin(), line.end(), [](unsigned char ch) {
    return ch == ' ' || ch == '\t';
  });
}

bool HasFinalNewline(const std::string& text) {
  return !text.empty() && text.back() == '\n';
}

std::string JoinLinesPreservingFinalNewline(
    const std::vector<std::string>& lines,
    bool had_final_newline) {
  std::string result = JoinLines(lines);
  if (had_final_newline && !result.empty()) {
    result += '\n';
  }
  return result;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool ParseSortLanguage(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name,
    SortLanguage default_value,
    SortLanguage& value,
    std::string& error) {
  std::string raw_value = ToLowerAscii(GetParam(params, name, "auto"));
  if (raw_value.empty()) {
    value = default_value;
    return true;
  }

  if (raw_value == "auto") {
    value = SortLanguage::Auto;
    return true;
  }
  if (raw_value == "binary" || raw_value == "byte" || raw_value == "bytes") {
    value = SortLanguage::Binary;
    return true;
  }
  if (raw_value == "english" || raw_value == "en") {
    value = SortLanguage::English;
    return true;
  }
  if (raw_value == "russian" || raw_value == "ru") {
    value = SortLanguage::Russian;
    return true;
  }
  if (raw_value == "ukrainian" || raw_value == "ukraine" || raw_value == "uk" || raw_value == "ua") {
    value = SortLanguage::Ukrainian;
    return true;
  }

  error = "Parameter " + name + " must be auto, english, russian, ukrainian, or binary.";
  return false;
}

bool ParseCaseMode(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name,
    CaseMode default_value,
    CaseMode& value,
    std::string& error) {
  std::string raw_value = ToLowerAscii(GetParam(params, name, "toggle"));
  if (raw_value.empty()) {
    value = default_value;
    return true;
  }

  if (raw_value == "toggle") {
    value = CaseMode::Toggle;
    return true;
  }
  if (raw_value == "upper" || raw_value == "uppercase") {
    value = CaseMode::Upper;
    return true;
  }
  if (raw_value == "lower" || raw_value == "lowercase") {
    value = CaseMode::Lower;
    return true;
  }
  if (raw_value == "title" || raw_value == "titlecase" || raw_value == "title_case") {
    value = CaseMode::Title;
    return true;
  }
  if (raw_value == "sentence" || raw_value == "sentencecase" || raw_value == "sentence_case") {
    value = CaseMode::Sentence;
    return true;
  }
  if (raw_value == "snake" || raw_value == "snake_case") {
    value = CaseMode::Snake;
    return true;
  }
  if (raw_value == "camel" || raw_value == "camelcase" || raw_value == "camel_case") {
    value = CaseMode::Camel;
    return true;
  }
  if (raw_value == "pascal" || raw_value == "pascalcase" || raw_value == "pascal_case") {
    value = CaseMode::Pascal;
    return true;
  }
  if (raw_value == "kebab" || raw_value == "kebab-case" || raw_value == "kebab_case") {
    value = CaseMode::Kebab;
    return true;
  }

  error = "Parameter " + name + " must be toggle, upper, lower, title, sentence, snake, camel, pascal, or kebab.";
  return false;
}

std::vector<std::uint32_t> DecodeUtf8(const std::string& value) {
  std::vector<std::uint32_t> codepoints;
  codepoints.reserve(value.size());

  for (std::size_t i = 0; i < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[i]);
    if (first < 0x80) {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    std::uint32_t codepoint = 0;
    std::size_t expected = 0;
    if ((first & 0xE0) == 0xC0) {
      codepoint = first & 0x1F;
      expected = 2;
    } else if ((first & 0xF0) == 0xE0) {
      codepoint = first & 0x0F;
      expected = 3;
    } else if ((first & 0xF8) == 0xF0) {
      codepoint = first & 0x07;
      expected = 4;
    } else {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    if (i + expected > value.size()) {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    bool valid = true;
    for (std::size_t j = 1; j < expected; ++j) {
      const unsigned char next = static_cast<unsigned char>(value[i + j]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6) | (next & 0x3F);
    }

    if (!valid) {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    codepoints.push_back(codepoint);
    i += expected;
  }

  return codepoints;
}

std::string EncodeUtf8(std::uint32_t codepoint) {
  std::string output;
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return output;
}

bool IsAsciiLower(std::uint32_t codepoint) {
  return codepoint >= 'a' && codepoint <= 'z';
}

bool IsAsciiUpper(std::uint32_t codepoint) {
  return codepoint >= 'A' && codepoint <= 'Z';
}

bool IsCyrillicUpper(std::uint32_t codepoint) {
  return (codepoint >= 0x0410 && codepoint <= 0x042F) ||
      codepoint == 0x0401 || codepoint == 0x0404 || codepoint == 0x0406 ||
      codepoint == 0x0407 || codepoint == 0x0490;
}

bool IsUpperForSort(std::uint32_t codepoint) {
  return IsAsciiUpper(codepoint) || IsCyrillicUpper(codepoint);
}

std::uint32_t ToLowerCodepoint(std::uint32_t codepoint) {
  if (IsAsciiUpper(codepoint)) {
    return codepoint + 32;
  }
  if (codepoint >= 0x0410 && codepoint <= 0x042F) {
    return codepoint + 32;
  }

  switch (codepoint) {
    case 0x0401: return 0x0451;  // Ё -> ё
    case 0x0404: return 0x0454;  // Є -> є
    case 0x0406: return 0x0456;  // І -> і
    case 0x0407: return 0x0457;  // Ї -> ї
    case 0x0490: return 0x0491;  // Ґ -> ґ
    default: return codepoint;
  }
}

bool IsCyrillicLower(std::uint32_t codepoint) {
  return (codepoint >= 0x0430 && codepoint <= 0x044F) ||
      codepoint == 0x0451 || codepoint == 0x0454 || codepoint == 0x0456 ||
      codepoint == 0x0457 || codepoint == 0x0491;
}

std::uint32_t ToUpperCodepoint(std::uint32_t codepoint) {
  if (IsAsciiLower(codepoint)) {
    return codepoint - 32;
  }
  if (codepoint >= 0x0430 && codepoint <= 0x044F) {
    return codepoint - 32;
  }

  switch (codepoint) {
    case 0x0451: return 0x0401;  // ё -> Ё
    case 0x0454: return 0x0404;  // є -> Є
    case 0x0456: return 0x0406;  // і -> І
    case 0x0457: return 0x0407;  // ї -> Ї
    case 0x0491: return 0x0490;  // ґ -> Ґ
    default: return codepoint;
  }
}

std::uint32_t ToggleCodepointCase(std::uint32_t codepoint) {
  if (IsAsciiLower(codepoint) || IsCyrillicLower(codepoint)) {
    return ToUpperCodepoint(codepoint);
  }
  if (IsAsciiUpper(codepoint) || IsCyrillicUpper(codepoint)) {
    return ToLowerCodepoint(codepoint);
  }
  return codepoint;
}

bool IsLetterOrDigitCodepoint(std::uint32_t codepoint) {
  return codepoint == '_' ||
      (codepoint >= '0' && codepoint <= '9') ||
      IsAsciiLower(codepoint) || IsAsciiUpper(codepoint) ||
      IsCyrillicLower(codepoint) || IsCyrillicUpper(codepoint);
}


bool ContainsUkrainianLetter(const std::string& text) {
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    const std::uint32_t lower = ToLowerCodepoint(codepoint);
    if (lower == 0x0491 || lower == 0x0454 || lower == 0x0456 || lower == 0x0457) {
      return true;
    }
  }
  return false;
}

bool ContainsCyrillicLetter(const std::string& text) {
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    if (IsCyrillicLower(ToLowerCodepoint(codepoint))) {
      return true;
    }
  }
  return false;
}

SortLanguage ResolveAutoSortLanguage(SortLanguage language, const std::string& text) {
  if (language != SortLanguage::Auto) {
    return language;
  }
  if (ContainsUkrainianLetter(text)) {
    return SortLanguage::Ukrainian;
  }
  if (ContainsCyrillicLetter(text)) {
    return SortLanguage::Russian;
  }
  return SortLanguage::English;
}

int RussianLetterWeight(std::uint32_t lower) {
  switch (lower) {
    case 0x0430: return 1;   // а
    case 0x0431: return 2;   // б
    case 0x0432: return 3;   // в
    case 0x0433: return 4;   // г
    case 0x0434: return 5;   // д
    case 0x0435: return 6;   // е
    case 0x0451: return 7;   // ё
    case 0x0436: return 8;   // ж
    case 0x0437: return 9;   // з
    case 0x0438: return 10;  // и
    case 0x0439: return 11;  // й
    case 0x043A: return 12;  // к
    case 0x043B: return 13;  // л
    case 0x043C: return 14;  // м
    case 0x043D: return 15;  // н
    case 0x043E: return 16;  // о
    case 0x043F: return 17;  // п
    case 0x0440: return 18;  // р
    case 0x0441: return 19;  // с
    case 0x0442: return 20;  // т
    case 0x0443: return 21;  // у
    case 0x0444: return 22;  // ф
    case 0x0445: return 23;  // х
    case 0x0446: return 24;  // ц
    case 0x0447: return 25;  // ч
    case 0x0448: return 26;  // ш
    case 0x0449: return 27;  // щ
    case 0x044A: return 28;  // ъ
    case 0x044B: return 29;  // ы
    case 0x044C: return 30;  // ь
    case 0x044D: return 31;  // э
    case 0x044E: return 32;  // ю
    case 0x044F: return 33;  // я
    default: return 0;
  }
}

int UkrainianLetterWeight(std::uint32_t lower) {
  switch (lower) {
    case 0x0430: return 1;   // а
    case 0x0431: return 2;   // б
    case 0x0432: return 3;   // в
    case 0x0433: return 4;   // г
    case 0x0491: return 5;   // ґ
    case 0x0434: return 6;   // д
    case 0x0435: return 7;   // е
    case 0x0454: return 8;   // є
    case 0x0436: return 9;   // ж
    case 0x0437: return 10;  // з
    case 0x0438: return 11;  // и
    case 0x0456: return 12;  // і
    case 0x0457: return 13;  // ї
    case 0x0439: return 14;  // й
    case 0x043A: return 15;  // к
    case 0x043B: return 16;  // л
    case 0x043C: return 17;  // м
    case 0x043D: return 18;  // н
    case 0x043E: return 19;  // о
    case 0x043F: return 20;  // п
    case 0x0440: return 21;  // р
    case 0x0441: return 22;  // с
    case 0x0442: return 23;  // т
    case 0x0443: return 24;  // у
    case 0x0444: return 25;  // ф
    case 0x0445: return 26;  // х
    case 0x0446: return 27;  // ц
    case 0x0447: return 28;  // ч
    case 0x0448: return 29;  // ш
    case 0x0449: return 30;  // щ
    case 0x044C: return 31;  // ь
    case 0x044E: return 32;  // ю
    case 0x044F: return 33;  // я
    default: return 0;
  }
}

int EnglishLetterWeight(std::uint32_t lower) {
  if (lower >= 'a' && lower <= 'z') {
    return static_cast<int>(lower - 'a') + 1;
  }
  return 0;
}

SortToken MakeSortToken(std::uint32_t codepoint, SortLanguage language, bool case_sensitive) {
  const std::uint32_t lower = ToLowerCodepoint(codepoint);
  if (language == SortLanguage::Binary) {
    const std::uint32_t binary_codepoint = case_sensitive ? codepoint : lower;
    return {1, static_cast<int>(binary_codepoint), 0, binary_codepoint};
  }

  const int case_weight = case_sensitive && IsUpperForSort(codepoint) ? 0 :
      (case_sensitive ? 1 : 0);

  if (language == SortLanguage::Russian) {
    const int weight = RussianLetterWeight(lower);
    if (weight > 0) {
      return {1, weight, case_weight, lower};
    }
  }

  if (language == SortLanguage::Ukrainian) {
    const int weight = UkrainianLetterWeight(lower);
    if (weight > 0) {
      return {1, weight, case_weight, lower};
    }
  }

  if (language == SortLanguage::English) {
    const int weight = EnglishLetterWeight(lower);
    if (weight > 0) {
      return {1, weight, case_weight, lower};
    }
  }

  return {2, static_cast<int>(lower), case_weight, lower};
}

std::vector<SortToken> BuildSortKey(
    const std::string& value,
    SortLanguage language,
    bool case_sensitive) {
  std::vector<SortToken> key;
  for (std::uint32_t codepoint : DecodeUtf8(value)) {
    key.push_back(MakeSortToken(codepoint, language, case_sensitive));
  }
  return key;
}

bool SortKeyLess(const std::vector<SortToken>& left, const std::vector<SortToken>& right) {
  const std::size_t common_size = std::min(left.size(), right.size());
  for (std::size_t i = 0; i < common_size; ++i) {
    const SortToken& l = left[i];
    const SortToken& r = right[i];
    if (l.group != r.group) {
      return l.group < r.group;
    }
    if (l.weight != r.weight) {
      return l.weight < r.weight;
    }
    if (l.case_weight != r.case_weight) {
      return l.case_weight < r.case_weight;
    }
    if (l.codepoint != r.codepoint) {
      return l.codepoint < r.codepoint;
    }
  }
  return left.size() < right.size();
}

std::string SortKeyToString(const std::vector<SortToken>& key) {
  std::string result;
  for (const SortToken& token : key) {
    result += std::to_string(token.group);
    result += ':';
    result += std::to_string(token.weight);
    result += ':';
    result += std::to_string(token.case_weight);
    result += ':';
    result += std::to_string(token.codepoint);
    result += ';';
  }
  return result;
}

std::string TrimTrailingSpacesFromLine(std::string line) {
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  return line;
}

BuiltinTextProcessorResult SuccessResult(std::string text) {
  BuiltinTextProcessorResult result;
  result.success = true;
  result.text = std::move(text);
  return result;
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

TextParserParam BooleanParam(
    std::string id,
    std::string label,
    std::string default_value,
    std::string description) {
  TextParserParam param;
  param.id = std::move(id);
  param.label = std::move(label);
  param.type = "boolean";
  param.default_value = std::move(default_value);
  param.description = std::move(description);
  return param;
}

TextParserParam TextParam(
    std::string id,
    std::string label,
    std::string default_value,
    std::string description) {
  TextParserParam param;
  param.id = std::move(id);
  param.label = std::move(label);
  param.type = "text";
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

BuiltinTextProcessorResult TrimTrailingSpaces(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;

  for (std::string& line : lines) {
    const std::string trimmed = TrimTrailingSpacesFromLine(line);
    if (trimmed.size() != line.size()) {
      line = trimmed;
      changed = true;
    }
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult EnsureFinalNewline(const std::string& input_text) {
  if (input_text.empty() || HasFinalNewline(input_text)) {
    return SuccessResult(input_text);
  }
  return SuccessResult(input_text + "\n");
}

BuiltinTextProcessorResult RemoveFinalExtraBlankLines(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  const std::size_t original_size = lines.size();

  while (!lines.empty() && IsBlankLine(lines.back())) {
    lines.pop_back();
  }

  if (lines.size() == original_size) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult RemoveEmptyLines(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  std::vector<std::string> output;
  output.reserve(lines.size());

  for (const std::string& line : lines) {
    if (!IsBlankLine(line)) {
      output.push_back(line);
    }
  }

  if (output.size() == lines.size()) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult CollapseEmptyLines(
    const std::string& input_text,
    std::size_t max_blank_lines) {
  std::vector<std::string> lines = SplitLines(input_text);
  std::vector<std::string> output;
  output.reserve(lines.size());
  std::size_t blank_count = 0;
  bool changed = false;

  for (const std::string& line : lines) {
    if (IsBlankLine(line)) {
      ++blank_count;
      if (blank_count <= max_blank_lines) {
        output.push_back("");
      } else {
        changed = true;
      }
      continue;
    }

    blank_count = 0;
    output.push_back(line);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult ConvertTabsToSpaces(
    const std::string& input_text,
    std::size_t tab_width) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;

  for (std::string& line : lines) {
    std::string output;
    output.reserve(line.size());
    std::size_t column = 0;

    for (char ch : line) {
      if (ch == '\t') {
        const std::size_t spaces = tab_width - (column % tab_width);
        output.append(spaces, ' ');
        column += spaces;
        changed = true;
      } else {
        output.push_back(ch);
        ++column;
      }
    }

    line = std::move(output);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult ConvertLeadingSpacesToTabs(
    const std::string& input_text,
    std::size_t tab_width) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;

  for (std::string& line : lines) {
    std::size_t spaces = 0;
    while (spaces < line.size() && line[spaces] == ' ') {
      ++spaces;
    }

    const std::size_t tab_count = spaces / tab_width;
    if (tab_count == 0) {
      continue;
    }

    const std::size_t remainder = spaces % tab_width;
    line = std::string(tab_count, '\t') + std::string(remainder, ' ') + line.substr(spaces);
    changed = true;
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult SortLines(
    const std::string& input_text,
    bool descending,
    bool case_sensitive,
    SortLanguage language) {
  std::vector<std::string> lines = SplitLines(input_text);
  const SortLanguage resolved_language = ResolveAutoSortLanguage(language, input_text);

  struct SortItem {
    std::string line;
    std::vector<SortToken> key;
  };

  std::vector<SortItem> items;
  items.reserve(lines.size());
  for (const std::string& line : lines) {
    items.push_back({line, BuildSortKey(line, resolved_language, case_sensitive)});
  }

  std::stable_sort(items.begin(), items.end(), [descending](const SortItem& left, const SortItem& right) {
    if (descending) {
      return SortKeyLess(right.key, left.key);
    }
    return SortKeyLess(left.key, right.key);
  });

  std::vector<std::string> sorted;
  sorted.reserve(items.size());
  for (const SortItem& item : items) {
    sorted.push_back(item.line);
  }

  if (sorted == lines) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(sorted, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult UniqueLines(
    const std::string& input_text,
    bool case_sensitive,
    bool trim_before_compare,
    SortLanguage language) {
  std::vector<std::string> lines = SplitLines(input_text);
  const SortLanguage resolved_language = ResolveAutoSortLanguage(language, input_text);
  std::vector<std::string> output;
  output.reserve(lines.size());
  std::unordered_set<std::string> seen;

  auto make_key = [case_sensitive, trim_before_compare, resolved_language](std::string value) {
    if (trim_before_compare) {
      value = TrimTrailingSpacesFromLine(std::move(value));
      std::size_t start = 0;
      while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
      }
      value.erase(0, start);
    }
    return SortKeyToString(BuildSortKey(value, resolved_language, case_sensitive));
  };

  for (const std::string& line : lines) {
    const std::string key = make_key(line);
    if (seen.insert(key).second) {
      output.push_back(line);
    }
  }

  if (output.size() == lines.size()) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

std::string MapCodepoints(
    const std::string& input_text,
    const std::function<std::uint32_t(std::uint32_t)>& mapper) {
  std::string output;
  output.reserve(input_text.size());
  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    output += EncodeUtf8(mapper(codepoint));
  }
  return output;
}

BuiltinTextProcessorResult ChangeSimpleCase(const std::string& input_text, CaseMode mode) {
  if (mode == CaseMode::Toggle) {
    return SuccessResult(MapCodepoints(input_text, [](std::uint32_t codepoint) {
      return ToggleCodepointCase(codepoint);
    }));
  }

  if (mode == CaseMode::Upper) {
    return SuccessResult(MapCodepoints(input_text, [](std::uint32_t codepoint) {
      return ToUpperCodepoint(codepoint);
    }));
  }

  if (mode == CaseMode::Lower) {
    return SuccessResult(MapCodepoints(input_text, [](std::uint32_t codepoint) {
      return ToLowerCodepoint(codepoint);
    }));
  }

  if (mode == CaseMode::Title) {
    bool word_start = true;
    return SuccessResult(MapCodepoints(input_text, [&word_start](std::uint32_t codepoint) {
      if (!IsLetterOrDigitCodepoint(codepoint)) {
        word_start = true;
        return codepoint;
      }
      const std::uint32_t mapped = word_start ? ToUpperCodepoint(codepoint) : ToLowerCodepoint(codepoint);
      word_start = false;
      return mapped;
    }));
  }

  if (mode == CaseMode::Sentence) {
    bool sentence_start = true;
    return SuccessResult(MapCodepoints(input_text, [&sentence_start](std::uint32_t codepoint) {
      if (IsLetterOrDigitCodepoint(codepoint)) {
        const std::uint32_t mapped = sentence_start ? ToUpperCodepoint(codepoint) : ToLowerCodepoint(codepoint);
        sentence_start = false;
        return mapped;
      }
      if (codepoint == '.' || codepoint == '!' || codepoint == '?' || codepoint == '\n') {
        sentence_start = true;
      }
      return codepoint;
    }));
  }

  return SuccessResult(input_text);
}

struct CaseWordPart {
  std::uint32_t codepoint = 0;
  bool ascii_upper = false;
  bool ascii_lower = false;
};

std::vector<std::vector<std::uint32_t>> ExtractIdentifierWords(const std::string& line) {
  std::vector<std::vector<std::uint32_t>> words;
  std::vector<std::uint32_t> current;
  bool previous_was_lower_or_digit = false;

  for (std::uint32_t codepoint : DecodeUtf8(line)) {
    const bool is_word = IsLetterOrDigitCodepoint(codepoint) && codepoint != '_';
    if (!is_word) {
      if (!current.empty()) {
        words.push_back(current);
        current.clear();
      }
      previous_was_lower_or_digit = false;
      continue;
    }

    const bool current_is_ascii_upper = IsAsciiUpper(codepoint);
    if (!current.empty() && current_is_ascii_upper && previous_was_lower_or_digit) {
      words.push_back(current);
      current.clear();
    }

    current.push_back(codepoint);
    previous_was_lower_or_digit = IsAsciiLower(codepoint) ||
        IsCyrillicLower(codepoint) ||
        (codepoint >= '0' && codepoint <= '9');
  }

  if (!current.empty()) {
    words.push_back(current);
  }
  return words;
}

std::string WordToLower(const std::vector<std::uint32_t>& word) {
  std::string output;
  for (std::uint32_t codepoint : word) {
    output += EncodeUtf8(ToLowerCodepoint(codepoint));
  }
  return output;
}

std::string WordToTitle(const std::vector<std::uint32_t>& word) {
  std::string output;
  bool first = true;
  for (std::uint32_t codepoint : word) {
    output += EncodeUtf8(first ? ToUpperCodepoint(codepoint) : ToLowerCodepoint(codepoint));
    first = false;
  }
  return output;
}

std::string JoinIdentifierWords(
    const std::vector<std::vector<std::uint32_t>>& words,
    CaseMode mode) {
  std::string output;
  for (std::size_t index = 0; index < words.size(); ++index) {
    if (mode == CaseMode::Snake && index > 0) {
      output += '_';
    } else if (mode == CaseMode::Kebab && index > 0) {
      output += '-';
    }

    if (mode == CaseMode::Camel) {
      output += index == 0 ? WordToLower(words[index]) : WordToTitle(words[index]);
    } else if (mode == CaseMode::Pascal) {
      output += WordToTitle(words[index]);
    } else {
      output += WordToLower(words[index]);
    }
  }
  return output;
}

BuiltinTextProcessorResult ChangeIdentifierCase(const std::string& input_text, CaseMode mode) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    const std::vector<std::vector<std::uint32_t>> words = ExtractIdentifierWords(line);
    if (words.empty()) {
      continue;
    }
    const std::string converted = JoinIdentifierWords(words, mode);
    if (converted != line) {
      line = converted;
      changed = true;
    }
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult ChangeCase(const std::string& input_text, CaseMode mode) {
  if (mode == CaseMode::Snake || mode == CaseMode::Camel ||
      mode == CaseMode::Pascal || mode == CaseMode::Kebab) {
    return ChangeIdentifierCase(input_text, mode);
  }
  return ChangeSimpleCase(input_text, mode);
}

BuiltinTextProcessorResult AddLinePrefix(
    const std::string& input_text,
    const std::string& prefix,
    bool only_non_empty) {
  std::vector<std::string> lines = SplitLines(input_text);
  if (lines.empty() || prefix.empty()) {
    return SuccessResult(input_text);
  }

  bool changed = false;
  for (std::string& line : lines) {
    if (only_non_empty && IsBlankLine(line)) {
      continue;
    }
    line = prefix + line;
    changed = true;
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult RemoveLinePrefix(
    const std::string& input_text,
    const std::string& prefix) {
  std::vector<std::string> lines = SplitLines(input_text);
  if (lines.empty() || prefix.empty()) {
    return SuccessResult(input_text);
  }

  bool changed = false;
  for (std::string& line : lines) {
    if (line.rfind(prefix, 0) == 0) {
      line.erase(0, prefix.size());
      changed = true;
    }
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
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
          "Toggles letter case in the selected text. Supports Ukrainian and Russian letters."),
      BuiltinDefinition(
          "builtin_change_case",
          "Change case",
          "Changes selected text case. Identifier modes are applied per line.",
          {TextParam(
              "mode",
              "Mode",
              "toggle",
              "Case mode: toggle, upper, lower, title, sentence, snake, camel, pascal, or kebab.")}),
      BuiltinDefinition(
          "builtin_trim_trailing_spaces",
          "Trim trailing spaces",
          "Removes spaces and tabs from the end of each line."),
      BuiltinDefinition(
          "builtin_ensure_final_newline",
          "Ensure final newline",
          "Adds one newline at the end of non-empty text if it is missing."),
      BuiltinDefinition(
          "builtin_remove_final_extra_blank_lines",
          "Remove final extra blank lines",
          "Removes blank lines after the last non-blank line."),
      BuiltinDefinition(
          "builtin_remove_empty_lines",
          "Remove empty lines",
          "Removes all empty or whitespace-only lines."),
      BuiltinDefinition(
          "builtin_collapse_empty_lines",
          "Collapse empty lines",
          "Limits consecutive empty lines to the selected count.",
          {IntegerParam(
              "max_blank_lines",
              "Max blank lines",
              "1",
              "Maximum number of consecutive empty lines to keep.")}),
      BuiltinDefinition(
          "builtin_tabs_to_spaces",
          "Tabs to spaces",
          "Replaces tab characters with spaces using the selected tab width.",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of columns represented by one tab.")}),
      BuiltinDefinition(
          "builtin_spaces_to_tabs",
          "Leading spaces to tabs",
          "Converts leading indentation spaces to tabs using the selected tab width.",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of leading spaces represented by one tab.")}),
      BuiltinDefinition(
          "builtin_sort_lines_az",
          "Sort lines A-Z",
          "Sorts lines in ascending order.",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Sorting language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "false",
               "Use case-sensitive comparison when sorting lines.")}),
      BuiltinDefinition(
          "builtin_sort_lines_za",
          "Sort lines Z-A",
          "Sorts lines in descending order.",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Sorting language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "false",
               "Use case-sensitive comparison when sorting lines.")}),
      BuiltinDefinition(
          "builtin_unique_lines",
          "Unique lines",
          "Keeps the first occurrence of each line and removes duplicates.",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Comparison language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "true",
               "Use case-sensitive comparison when detecting duplicates."),
           BooleanParam(
               "trim_before_compare",
               "Trim compare",
               "false",
               "Ignore leading and trailing spaces when comparing lines.")}),
      BuiltinDefinition(
          "builtin_add_line_prefix",
          "Add line prefix",
          "Adds selected prefix to every line.",
          {TextParam(
               "prefix",
               "Prefix",
               "// ",
               "Text to insert at the beginning of each line."),
           BooleanParam(
               "only_non_empty",
               "Only non-empty",
               "true",
               "Skip empty or whitespace-only lines.")}),
      BuiltinDefinition(
          "builtin_remove_line_prefix",
          "Remove line prefix",
          "Removes selected prefix from the beginning of lines where it exists.",
          {TextParam(
              "prefix",
              "Prefix",
              "// ",
              "Text to remove from the beginning of each line.")}),
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

  if (definition.builtin_id == "builtin_change_case") {
    CaseMode mode = CaseMode::Toggle;
    std::string error;
    if (!ParseCaseMode(params, "mode", CaseMode::Toggle, mode, error)) {
      return {false, {}, error};
    }
    return ChangeCase(input_text, mode);
  }

  if (definition.builtin_id == "builtin_trim_trailing_spaces") {
    return TrimTrailingSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_ensure_final_newline") {
    return EnsureFinalNewline(input_text);
  }

  if (definition.builtin_id == "builtin_remove_final_extra_blank_lines") {
    return RemoveFinalExtraBlankLines(input_text);
  }

  if (definition.builtin_id == "builtin_remove_empty_lines") {
    return RemoveEmptyLines(input_text);
  }

  if (definition.builtin_id == "builtin_collapse_empty_lines") {
    std::size_t max_blank_lines = 1;
    std::string error;
    if (!ParsePositiveSize(params, "max_blank_lines", 0, 20, max_blank_lines, error)) {
      return {false, {}, error};
    }
    return CollapseEmptyLines(input_text, max_blank_lines);
  }

  if (definition.builtin_id == "builtin_tabs_to_spaces") {
    std::size_t tab_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "tab_width", 1, 16, tab_width, error)) {
      return {false, {}, error};
    }
    return ConvertTabsToSpaces(input_text, tab_width);
  }

  if (definition.builtin_id == "builtin_spaces_to_tabs") {
    std::size_t tab_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "tab_width", 1, 16, tab_width, error)) {
      return {false, {}, error};
    }
    return ConvertLeadingSpacesToTabs(input_text, tab_width);
  }

  if (definition.builtin_id == "builtin_sort_lines_az" ||
      definition.builtin_id == "builtin_sort_lines_za") {
    SortLanguage language = SortLanguage::Auto;
    bool case_sensitive = false;
    std::string error;
    if (!ParseSortLanguage(params, "language", SortLanguage::Auto, language, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "case_sensitive", false, case_sensitive, error)) {
      return {false, {}, error};
    }
    return SortLines(
        input_text,
        definition.builtin_id == "builtin_sort_lines_za",
        case_sensitive,
        language);
  }

  if (definition.builtin_id == "builtin_unique_lines") {
    SortLanguage language = SortLanguage::Auto;
    bool case_sensitive = true;
    bool trim_before_compare = false;
    std::string error;
    if (!ParseSortLanguage(params, "language", SortLanguage::Auto, language, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "case_sensitive", true, case_sensitive, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "trim_before_compare", false, trim_before_compare, error)) {
      return {false, {}, error};
    }
    return UniqueLines(input_text, case_sensitive, trim_before_compare, language);
  }

  if (definition.builtin_id == "builtin_add_line_prefix") {
    bool only_non_empty = true;
    std::string error;
    if (!ParseBoolParam(params, "only_non_empty", true, only_non_empty, error)) {
      return {false, {}, error};
    }
    return AddLinePrefix(input_text, GetParam(params, "prefix", "// "), only_non_empty);
  }

  if (definition.builtin_id == "builtin_remove_line_prefix") {
    return RemoveLinePrefix(input_text, GetParam(params, "prefix", "// "));
  }

  return {false, {}, "Unknown built-in text processor: " + definition.builtin_id};
}

}  // namespace textlt
