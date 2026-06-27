#include "builtin_text_processors.hpp"

#include "text_transformer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
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


std::string TrimLeadingSpacesFromLine(std::string line) {
  std::size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    line.erase(0, start);
  }
  return line;
}

std::string TrimSpacesFromLine(std::string line) {
  line = TrimTrailingSpacesFromLine(std::move(line));
  return TrimLeadingSpacesFromLine(std::move(line));
}

bool IsHorizontalSpaceCodepoint(std::uint32_t codepoint) {
  return codepoint == ' ' || codepoint == '\t' || codepoint == 0x00A0;
}

bool IsZeroWidthCodepoint(std::uint32_t codepoint) {
  return codepoint == 0xFEFF || codepoint == 0x200B || codepoint == 0x200C ||
      codepoint == 0x200D || codepoint == 0x2060;
}

bool IsAsciiControlCodepoint(std::uint32_t codepoint) {
  return (codepoint < 0x20 || codepoint == 0x7F) &&
      codepoint != '\n' && codepoint != '\r' && codepoint != '\t';
}

bool IsLowercaseLetterForJoin(std::uint32_t codepoint) {
  return IsAsciiLower(codepoint) || IsCyrillicLower(codepoint);
}

bool StartsWithLowercaseLetter(const std::string& line) {
  for (std::uint32_t codepoint : DecodeUtf8(line)) {
    if (IsHorizontalSpaceCodepoint(codepoint)) {
      continue;
    }
    return IsLowercaseLetterForJoin(codepoint);
  }
  return false;
}

BuiltinTextProcessorResult SuccessResult(std::string text) {
  BuiltinTextProcessorResult result;
  result.success = true;
  result.text = std::move(text);
  return result;
}

BuiltinTextProcessorResult ReportResult(std::string report) {
  BuiltinTextProcessorResult result;
  result.success = true;
  result.text = std::move(report);
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

TextParserDefinition MakeBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    TextParserScope scope,
    std::string group,
    TextParserOutput output,
    std::vector<TextParserParam> params = {}) {
  TextParserDefinition definition;
  definition.id = std::move(id);
  definition.name = std::move(name);
  definition.scope = scope;
  definition.description = std::move(description);
  definition.group = std::move(group);
  definition.engine = TextParserEngine::Builtin;
  definition.output = output;
  definition.builtin_id = definition.id;
  definition.locked = true;
  definition.repeat_default = 1;
  definition.params = std::move(params);
  return definition;
}

TextParserDefinition BuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Code,
      "Code",
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition GroupedBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::string group,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Code,
      std::move(group),
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition TextBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::string group,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Text,
      std::move(group),
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition AnalysisDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Text,
      "Analysis",
      TextParserOutput::Report,
      std::move(params));
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


BuiltinTextProcessorResult TrimLines(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;

  for (std::string& line : lines) {
    const std::string trimmed = TrimSpacesFromLine(line);
    if (trimmed != line) {
      line = trimmed;
      changed = true;
    }
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult TrimDocument(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  if (lines.empty()) {
    return SuccessResult(input_text);
  }

  std::size_t first = 0;
  while (first < lines.size() && IsBlankLine(lines[first])) {
    ++first;
  }

  if (first == lines.size()) {
    return SuccessResult("");
  }

  std::size_t last = lines.size() - 1;
  while (last > first && IsBlankLine(lines[last])) {
    --last;
  }

  std::vector<std::string> output;
  output.reserve(last - first + 1);
  for (std::size_t index = first; index <= last; ++index) {
    output.push_back(lines[index]);
  }

  output.front() = TrimLeadingSpacesFromLine(std::move(output.front()));
  output.back() = TrimTrailingSpacesFromLine(std::move(output.back()));

  const std::string result = JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text));
  if (result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult NormalizeSpaces(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool in_horizontal_space = false;
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == '\n' || codepoint == '\r') {
      output += EncodeUtf8(codepoint);
      in_horizontal_space = false;
      continue;
    }

    if (IsHorizontalSpaceCodepoint(codepoint)) {
      if (!in_horizontal_space) {
        output.push_back(' ');
      }
      if (codepoint != ' ' || in_horizontal_space) {
        changed = true;
      }
      in_horizontal_space = true;
      continue;
    }

    output += EncodeUtf8(codepoint);
    in_horizontal_space = false;
  }

  if (!changed && output == input_text) {
    return SuccessResult(input_text);
  }
  return TrimLines(output);
}

BuiltinTextProcessorResult RemoveDuplicateSpaces(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;

  for (std::string& line : lines) {
    std::string output;
    output.reserve(line.size());
    bool previous_space = false;
    for (char ch : line) {
      if (ch == ' ') {
        if (!previous_space) {
          output.push_back(ch);
        } else {
          changed = true;
        }
        previous_space = true;
      } else {
        output.push_back(ch);
        previous_space = false;
      }
    }
    if (output != line) {
      line = std::move(output);
    }
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult RemoveInvisibleCharacters(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (IsZeroWidthCodepoint(codepoint) || codepoint == 0x00AD || IsAsciiControlCodepoint(codepoint)) {
      changed = true;
      continue;
    }
    if (codepoint == 0x00A0) {
      output.push_back(' ');
      changed = true;
      continue;
    }
    output += EncodeUtf8(codepoint);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult RemoveZeroWidthCharacters(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (IsZeroWidthCodepoint(codepoint)) {
      changed = true;
      continue;
    }
    output += EncodeUtf8(codepoint);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult ReplaceNonBreakingSpaces(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == 0x00A0) {
      output.push_back(' ');
      changed = true;
    } else {
      output += EncodeUtf8(codepoint);
    }
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult RemoveSoftHyphens(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == 0x00AD) {
      changed = true;
      continue;
    }
    output += EncodeUtf8(codepoint);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult RemoveBom(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == 0xFEFF) {
      changed = true;
      continue;
    }
    output += EncodeUtf8(codepoint);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult RemoveControlCharacters(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (IsAsciiControlCodepoint(codepoint)) {
      changed = true;
      continue;
    }
    output += EncodeUtf8(codepoint);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult NormalizeLineEndings(
    const std::string& input_text,
    const std::string& target_ending) {
  std::string normalized;
  normalized.reserve(input_text.size());
  bool changed = false;

  for (std::size_t index = 0; index < input_text.size(); ++index) {
    const char ch = input_text[index];
    if (ch == '\r') {
      if (index + 1 < input_text.size() && input_text[index + 1] == '\n') {
        ++index;
      }
      normalized += target_ending;
      changed = true;
    } else if (ch == '\n') {
      normalized += target_ending;
      if (target_ending != "\n") {
        changed = true;
      }
    } else {
      normalized.push_back(ch);
    }
  }

  if (!changed && normalized == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(normalized);
}

BuiltinTextProcessorResult RemovePageNumbers(
    const std::string& input_text,
    std::size_t max_number_digits) {
  std::vector<std::string> lines = SplitLines(input_text);
  std::vector<std::string> output;
  output.reserve(lines.size());
  bool changed = false;

  for (const std::string& line : lines) {
    const std::string trimmed = TrimSpacesFromLine(line);
    const bool digit_only = !trimmed.empty() &&
        trimmed.size() <= max_number_digits &&
        std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
          return std::isdigit(ch) != 0;
        });
    if (digit_only) {
      changed = true;
      continue;
    }
    output.push_back(line);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult FixOcrHyphenation(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  if (lines.size() < 2) {
    return SuccessResult(input_text);
  }

  std::vector<std::string> output;
  output.reserve(lines.size());
  bool changed = false;

  for (std::size_t index = 0; index < lines.size(); ++index) {
    std::string line = lines[index];
    while (index + 1 < lines.size() && !line.empty()) {
      std::string trimmed_line = TrimTrailingSpacesFromLine(line);
      const bool ascii_hyphen = !trimmed_line.empty() && trimmed_line.back() == '-';
      bool soft_hyphen = false;
      const std::vector<std::uint32_t> codepoints = DecodeUtf8(trimmed_line);
      if (!codepoints.empty() && codepoints.back() == 0x00AD) {
        soft_hyphen = true;
      }

      if (!ascii_hyphen && !soft_hyphen) {
        break;
      }
      if (!StartsWithLowercaseLetter(lines[index + 1])) {
        break;
      }

      if (ascii_hyphen) {
        trimmed_line.pop_back();
      } else {
        trimmed_line.clear();
        for (std::size_t cp_index = 0; cp_index + 1 < codepoints.size(); ++cp_index) {
          trimmed_line += EncodeUtf8(codepoints[cp_index]);
        }
      }

      line = trimmed_line + TrimLeadingSpacesFromLine(lines[index + 1]);
      ++index;
      changed = true;
    }
    output.push_back(line);
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult JoinBrokenLines(
    const std::string& input_text,
    bool keep_paragraph_breaks) {
  std::vector<std::string> lines = SplitLines(input_text);
  if (lines.empty()) {
    return SuccessResult(input_text);
  }

  std::vector<std::string> output;
  std::string paragraph;
  bool changed = false;

  auto flush_paragraph = [&]() {
    if (!paragraph.empty()) {
      output.push_back(paragraph);
      paragraph.clear();
    }
  };

  for (const std::string& raw_line : lines) {
    const std::string line = TrimSpacesFromLine(raw_line);
    if (line.empty()) {
      flush_paragraph();
      if (keep_paragraph_breaks && (output.empty() || !output.back().empty())) {
        output.push_back("");
      }
      if (!raw_line.empty()) {
        changed = true;
      }
      continue;
    }

    if (paragraph.empty()) {
      paragraph = line;
    } else {
      paragraph += ' ';
      paragraph += line;
      changed = true;
    }

    if (line != raw_line) {
      changed = true;
    }
  }
  flush_paragraph();

  while (!output.empty() && output.back().empty()) {
    output.pop_back();
    changed = true;
  }

  const std::string result = JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text));
  if (!changed && result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

std::size_t CountLineEndings(const std::string& text, const std::string& ending) {
  std::size_t count = 0;
  for (std::size_t index = 0; index + ending.size() <= text.size();) {
    if (text.compare(index, ending.size(), ending) == 0) {
      ++count;
      index += ending.size();
    } else {
      ++index;
    }
  }
  return count;
}

std::size_t CountCodepoints(const std::string& text) {
  return DecodeUtf8(text).size();
}

std::size_t CountWords(const std::string& text) {
  std::size_t words = 0;
  bool inside_word = false;
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    if (IsLetterOrDigitCodepoint(codepoint)) {
      if (!inside_word) {
        ++words;
        inside_word = true;
      }
    } else {
      inside_word = false;
    }
  }
  return words;
}

std::size_t CountParagraphs(const std::vector<std::string>& lines) {
  std::size_t paragraphs = 0;
  bool inside_paragraph = false;
  for (const std::string& line : lines) {
    if (IsBlankLine(line)) {
      inside_paragraph = false;
      continue;
    }
    if (!inside_paragraph) {
      ++paragraphs;
      inside_paragraph = true;
    }
  }
  return paragraphs;
}

BuiltinTextProcessorResult AnalyzeTextStatistics(const std::string& input_text) {
  const std::vector<std::string> lines = SplitLines(input_text);
  std::size_t non_empty_lines = 0;
  for (const std::string& line : lines) {
    if (!IsBlankLine(line)) {
      ++non_empty_lines;
    }
  }

  std::ostringstream report;
  report << "Text statistics\n";
  report << "Bytes: " << input_text.size() << '\n';
  report << "Characters: " << CountCodepoints(input_text) << '\n';
  report << "Words: " << CountWords(input_text) << '\n';
  report << "Lines: " << lines.size() << '\n';
  report << "Non-empty lines: " << non_empty_lines << '\n';
  report << "Paragraphs: " << CountParagraphs(lines) << '\n';
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeLongLines(
    const std::string& input_text,
    std::size_t max_line_length) {
  const std::vector<std::string> lines = SplitLines(input_text);
  std::ostringstream report;
  std::size_t count = 0;
  report << "Long lines over " << max_line_length << " characters\n";

  for (std::size_t index = 0; index < lines.size(); ++index) {
    const std::size_t length = CountCodepoints(lines[index]);
    if (length <= max_line_length) {
      continue;
    }
    ++count;
    if (count <= 30) {
      report << "Line " << (index + 1) << ": " << length << " characters\n";
    }
  }

  report << "Total: " << count << '\n';
  if (count > 30) {
    report << "Shown first 30 lines only.\n";
  }
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeDuplicateLines(
    const std::string& input_text,
    bool case_sensitive,
    bool trim_before_compare,
    SortLanguage language) {
  const std::vector<std::string> lines = SplitLines(input_text);
  const SortLanguage resolved_language = ResolveAutoSortLanguage(language, input_text);

  struct DuplicateInfo {
    std::string line;
    std::size_t count = 0;
    std::vector<std::size_t> line_numbers;
  };

  std::vector<DuplicateInfo> duplicates;
  std::unordered_map<std::string, std::size_t> index_by_key;

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

  for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const std::string key = make_key(lines[line_index]);
    auto it = index_by_key.find(key);
    if (it == index_by_key.end()) {
      index_by_key[key] = duplicates.size();
      DuplicateInfo info;
      info.line = lines[line_index];
      info.count = 1;
      info.line_numbers.push_back(line_index + 1);
      duplicates.push_back(std::move(info));
      continue;
    }
    DuplicateInfo& info = duplicates[it->second];
    ++info.count;
    info.line_numbers.push_back(line_index + 1);
  }

  std::ostringstream report;
  std::size_t duplicate_groups = 0;
  std::size_t duplicate_extra_lines = 0;
  report << "Duplicate lines\n";
  for (const DuplicateInfo& info : duplicates) {
    if (info.count <= 1) {
      continue;
    }
    ++duplicate_groups;
    duplicate_extra_lines += info.count - 1;
    if (duplicate_groups <= 25) {
      report << "Count " << info.count << ": " << info.line << "\n  Lines: ";
      for (std::size_t i = 0; i < info.line_numbers.size(); ++i) {
        if (i > 0) {
          report << ", ";
        }
        report << info.line_numbers[i];
      }
      report << '\n';
    }
  }
  report << "Groups: " << duplicate_groups << '\n';
  report << "Extra duplicate lines: " << duplicate_extra_lines << '\n';
  if (duplicate_groups > 25) {
    report << "Shown first 25 groups only.\n";
  }
  return ReportResult(report.str());
}

std::string HexCodepoint(std::uint32_t codepoint) {
  std::ostringstream output;
  output << "U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << codepoint;
  return output.str();
}

BuiltinTextProcessorResult AnalyzeInvisibleCharacters(const std::string& input_text) {
  std::map<std::string, std::size_t> counts;
  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == 0xFEFF) {
      ++counts["BOM / zero width no-break space U+FEFF"];
    } else if (codepoint == 0x00A0) {
      ++counts["Non-breaking space U+00A0"];
    } else if (codepoint == 0x00AD) {
      ++counts["Soft hyphen U+00AD"];
    } else if (codepoint >= 0x200B && codepoint <= 0x200D) {
      ++counts["Zero-width character " + HexCodepoint(codepoint)];
    } else if (codepoint == 0x2060) {
      ++counts["Word joiner U+2060"];
    } else if (codepoint == '\t') {
      ++counts["Tab U+0009"];
    } else if (codepoint < 0x20 && codepoint != '\n' && codepoint != '\r') {
      ++counts["Control character " + HexCodepoint(codepoint)];
    }
  }

  std::ostringstream report;
  report << "Invisible characters\n";
  if (counts.empty()) {
    report << "No invisible characters found.\n";
    return ReportResult(report.str());
  }

  std::size_t total = 0;
  for (const auto& item : counts) {
    total += item.second;
    report << item.first << ": " << item.second << '\n';
  }
  report << "Total: " << total << '\n';
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeLineEndings(const std::string& input_text) {
  std::size_t crlf = 0;
  std::size_t lf = 0;
  std::size_t cr = 0;

  for (std::size_t index = 0; index < input_text.size(); ++index) {
    if (input_text[index] == '\r') {
      if (index + 1 < input_text.size() && input_text[index + 1] == '\n') {
        ++crlf;
        ++index;
      } else {
        ++cr;
      }
    } else if (input_text[index] == '\n') {
      ++lf;
    }
  }

  const std::size_t styles = (crlf > 0 ? 1 : 0) + (lf > 0 ? 1 : 0) + (cr > 0 ? 1 : 0);
  std::ostringstream report;
  report << "Line endings\n";
  report << "CRLF: " << crlf << '\n';
  report << "LF: " << lf << '\n';
  report << "CR: " << cr << '\n';
  report << "Mixed: " << (styles > 1 ? "yes" : "no") << '\n';
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeCharacterInventory(const std::string& input_text) {
  std::unordered_map<std::uint32_t, std::size_t> counts;
  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint > 0x7F) {
      ++counts[codepoint];
    }
  }

  std::vector<std::pair<std::uint32_t, std::size_t>> items(counts.begin(), counts.end());
  std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
    if (left.second != right.second) {
      return left.second > right.second;
    }
    return left.first < right.first;
  });

  std::ostringstream report;
  report << "Non-ASCII character inventory\n";
  if (items.empty()) {
    report << "No non-ASCII characters found.\n";
    return ReportResult(report.str());
  }

  std::size_t shown = 0;
  for (const auto& item : items) {
    if (shown >= 40) {
      break;
    }
    report << EncodeUtf8(item.first) << " " << HexCodepoint(item.first) << ": " << item.second << '\n';
    ++shown;
  }
  report << "Unique non-ASCII characters: " << items.size() << '\n';
  if (items.size() > shown) {
    report << "Shown first " << shown << " characters only.\n";
  }
  return ReportResult(report.str());
}

}  // namespace

std::vector<TextParserDefinition> CreateBuiltinTextProcessors() {
  return {
      GroupedBuiltinDefinition(
          "builtin_indent_lines",
          "Indent lines",
          "Adds spaces to the beginning of each selected line.",
          "Code",
          {IntegerParam(
              "indent_width",
              "Indent width",
              "4",
              "Number of spaces to insert at the beginning of each line.")}),
      GroupedBuiltinDefinition(
          "builtin_outdent_lines",
          "Outdent lines",
          "Removes one indentation level from each selected line.",
          "Code",
          {IntegerParam(
              "indent_width",
              "Indent width",
              "4",
              "Maximum number of spaces to remove from the beginning of each line.")}),
      GroupedBuiltinDefinition(
          "builtin_convert_4_spaces_to_2",
          "Convert 4 spaces to 2",
          "Converts leading indentation blocks of four spaces into two spaces.",
          "Code"),
      GroupedBuiltinDefinition(
          "builtin_convert_2_spaces_to_4",
          "Convert 2 spaces to 4",
          "Converts leading indentation blocks of two spaces into four spaces.",
          "Code"),
      GroupedBuiltinDefinition(
          "builtin_toggle_case",
          "Toggle case",
          "Toggles letter case in the selected text. Supports Ukrainian and Russian letters.",
          "Case"),
      GroupedBuiltinDefinition(
          "builtin_change_case",
          "Change case",
          "Changes selected text case. Identifier modes are applied per line.",
          "Case",
          {TextParam(
              "mode",
              "Mode",
              "toggle",
              "Case mode: toggle, upper, lower, title, sentence, snake, camel, pascal, or kebab.")}),
      GroupedBuiltinDefinition(
          "builtin_trim_trailing_spaces",
          "Trim trailing spaces",
          "Removes spaces and tabs from the end of each line.",
          "Cleanup"),
      GroupedBuiltinDefinition(
          "builtin_ensure_final_newline",
          "Ensure final newline",
          "Adds one newline at the end of non-empty text if it is missing.",
          "Cleanup"),
      GroupedBuiltinDefinition(
          "builtin_remove_final_extra_blank_lines",
          "Remove final extra blank lines",
          "Removes blank lines after the last non-blank line.",
          "Cleanup"),
      GroupedBuiltinDefinition(
          "builtin_remove_empty_lines",
          "Remove empty lines",
          "Removes all empty or whitespace-only lines.",
          "Lines"),
      GroupedBuiltinDefinition(
          "builtin_collapse_empty_lines",
          "Collapse empty lines",
          "Limits consecutive empty lines to the selected count.",
          "Lines",
          {IntegerParam(
              "max_blank_lines",
              "Max blank lines",
              "1",
              "Maximum number of consecutive empty lines to keep.")}),
      GroupedBuiltinDefinition(
          "builtin_tabs_to_spaces",
          "Tabs to spaces",
          "Replaces tab characters with spaces using the selected tab width.",
          "Code",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of columns represented by one tab.")}),
      GroupedBuiltinDefinition(
          "builtin_spaces_to_tabs",
          "Leading spaces to tabs",
          "Converts leading indentation spaces to tabs using the selected tab width.",
          "Code",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of leading spaces represented by one tab.")}),
      GroupedBuiltinDefinition(
          "builtin_sort_lines_az",
          "Sort lines A-Z",
          "Sorts lines in ascending order.",
          "Lines",
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
      GroupedBuiltinDefinition(
          "builtin_sort_lines_za",
          "Sort lines Z-A",
          "Sorts lines in descending order.",
          "Lines",
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
      GroupedBuiltinDefinition(
          "builtin_unique_lines",
          "Unique lines",
          "Keeps the first occurrence of each line and removes duplicates.",
          "Lines",
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
      GroupedBuiltinDefinition(
          "builtin_add_line_prefix",
          "Add line prefix",
          "Adds selected prefix to every line.",
          "Lines",
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
      GroupedBuiltinDefinition(
          "builtin_remove_line_prefix",
          "Remove line prefix",
          "Removes selected prefix from the beginning of lines where it exists.",
          "Lines",
          {TextParam(
              "prefix",
              "Prefix",
              "// ",
              "Text to remove from the beginning of each line.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_trim_lines",
          "Trim lines",
          "Removes leading and trailing spaces or tabs from every line.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_trim_document",
          "Trim document",
          "Removes blank lines and outer spaces at the beginning and end of the document.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_normalize_spaces",
          "Normalize spaces",
          "Replaces tabs and non-breaking spaces with regular spaces, collapses repeated spaces, and trims lines.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_duplicate_spaces",
          "Remove duplicate spaces",
          "Collapses repeated regular spaces inside each line.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_empty_lines",
          "Remove empty lines",
          "Removes all empty or whitespace-only lines from text.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_collapse_empty_lines",
          "Collapse empty lines",
          "Limits consecutive empty lines to the selected count.",
          "Cleanup",
          {IntegerParam(
              "max_blank_lines",
              "Max blank lines",
              "1",
              "Maximum number of consecutive empty lines to keep.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_invisible_chars",
          "Remove invisible characters",
          "Removes zero-width characters, soft hyphens, BOM, and control characters. Non-breaking spaces become spaces.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_zero_width_chars",
          "Remove zero-width characters",
          "Removes BOM, zero-width spaces, zero-width joiners, and word joiners.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_replace_nbsp",
          "Replace non-breaking spaces",
          "Replaces non-breaking spaces with regular spaces.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_soft_hyphens",
          "Remove soft hyphens",
          "Removes invisible soft hyphen characters.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_bom",
          "Remove BOM",
          "Removes UTF-8 BOM / zero-width no-break space characters.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_control_chars",
          "Remove control characters",
          "Removes ASCII control characters except tabs and line breaks.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_normalize_line_endings",
          "Normalize line endings",
          "Converts line endings to LF or CRLF.",
          "Cleanup",
          {TextParam(
              "ending",
              "Ending",
              "lf",
              "Target line ending: lf or crlf.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_normalize_tabs",
          "Normalize tabs",
          "Replaces tab characters with spaces using the selected tab width.",
          "Cleanup",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of columns represented by one tab.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_page_numbers",
          "Remove page numbers",
          "Removes lines that contain only short page numbers.",
          "Cleanup",
          {IntegerParam(
              "max_number_digits",
              "Max number digits",
              "4",
              "Only digit-only lines up to this length are removed.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_fix_ocr_hyphenation",
          "Fix OCR hyphenation",
          "Joins words split across lines by a hyphen or soft hyphen.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_join_broken_lines",
          "Join broken lines",
          "Joins wrapped lines inside paragraphs into normal paragraph lines.",
          "Cleanup",
          {BooleanParam(
              "keep_paragraph_breaks",
              "Keep paragraph breaks",
              "true",
              "Keep blank lines between paragraphs.")}),
      AnalysisDefinition(
          "builtin_analysis_text_statistics",
          "Text statistics",
          "Counts bytes, characters, words, lines, non-empty lines, and paragraphs."),
      AnalysisDefinition(
          "builtin_analysis_long_lines",
          "Find long lines",
          "Reports lines longer than the selected maximum length.",
          {IntegerParam(
              "max_line_length",
              "Max line length",
              "120",
              "Lines longer than this value will be reported.")}),
      AnalysisDefinition(
          "builtin_analysis_duplicate_lines",
          "Find duplicate lines",
          "Reports duplicate lines and their line numbers.",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Comparison language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "false",
               "Use case-sensitive comparison when detecting duplicate lines."),
           BooleanParam(
               "trim_before_compare",
               "Trim compare",
               "true",
               "Ignore leading and trailing spaces when comparing lines.")}),
      AnalysisDefinition(
          "builtin_analysis_invisible_chars",
          "Find invisible characters",
          "Reports tabs, non-breaking spaces, soft hyphens, zero-width characters, and control characters."),
      AnalysisDefinition(
          "builtin_analysis_line_endings",
          "Check line endings",
          "Reports CRLF, LF, CR counts and whether line endings are mixed."),
      AnalysisDefinition(
          "builtin_analysis_character_inventory",
          "Non-ASCII inventory",
          "Reports non-ASCII characters and their counts."),
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

  if (definition.builtin_id == "builtin_text_cleanup_trim_lines") {
    return TrimLines(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_trim_document") {
    return TrimDocument(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_normalize_spaces") {
    return NormalizeSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_duplicate_spaces") {
    return RemoveDuplicateSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_empty_lines") {
    return RemoveEmptyLines(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_collapse_empty_lines") {
    std::size_t max_blank_lines = 1;
    std::string error;
    if (!ParsePositiveSize(params, "max_blank_lines", 0, 20, max_blank_lines, error)) {
      return {false, {}, error};
    }
    return CollapseEmptyLines(input_text, max_blank_lines);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_invisible_chars") {
    return RemoveInvisibleCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_zero_width_chars") {
    return RemoveZeroWidthCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_replace_nbsp") {
    return ReplaceNonBreakingSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_soft_hyphens") {
    return RemoveSoftHyphens(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_bom") {
    return RemoveBom(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_control_chars") {
    return RemoveControlCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_normalize_line_endings") {
    std::string ending = ToLowerAscii(GetParam(params, "ending", "lf"));
    if (ending == "lf" || ending == "unix") {
      return NormalizeLineEndings(input_text, "\n");
    }
    if (ending == "crlf" || ending == "windows") {
      return NormalizeLineEndings(input_text, "\r\n");
    }
    return {false, {}, "Parameter ending must be lf or crlf."};
  }

  if (definition.builtin_id == "builtin_text_cleanup_normalize_tabs") {
    std::size_t tab_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "tab_width", 1, 16, tab_width, error)) {
      return {false, {}, error};
    }
    return ConvertTabsToSpaces(input_text, tab_width);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_page_numbers") {
    std::size_t max_number_digits = 4;
    std::string error;
    if (!ParsePositiveSize(params, "max_number_digits", 1, 20, max_number_digits, error)) {
      return {false, {}, error};
    }
    return RemovePageNumbers(input_text, max_number_digits);
  }

  if (definition.builtin_id == "builtin_text_cleanup_fix_ocr_hyphenation") {
    return FixOcrHyphenation(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_join_broken_lines") {
    bool keep_paragraph_breaks = true;
    std::string error;
    if (!ParseBoolParam(params, "keep_paragraph_breaks", true, keep_paragraph_breaks, error)) {
      return {false, {}, error};
    }
    return JoinBrokenLines(input_text, keep_paragraph_breaks);
  }

  if (definition.builtin_id == "builtin_analysis_text_statistics") {
    return AnalyzeTextStatistics(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_long_lines") {
    std::size_t max_line_length = 120;
    std::string error;
    if (!ParsePositiveSize(params, "max_line_length", 1, 100000, max_line_length, error)) {
      return {false, {}, error};
    }
    return AnalyzeLongLines(input_text, max_line_length);
  }

  if (definition.builtin_id == "builtin_analysis_duplicate_lines") {
    SortLanguage language = SortLanguage::Auto;
    bool case_sensitive = false;
    bool trim_before_compare = true;
    std::string error;
    if (!ParseSortLanguage(params, "language", SortLanguage::Auto, language, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "case_sensitive", false, case_sensitive, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "trim_before_compare", true, trim_before_compare, error)) {
      return {false, {}, error};
    }
    return AnalyzeDuplicateLines(input_text, case_sensitive, trim_before_compare, language);
  }

  if (definition.builtin_id == "builtin_analysis_invisible_chars") {
    return AnalyzeInvisibleCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_line_endings") {
    return AnalyzeLineEndings(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_character_inventory") {
    return AnalyzeCharacterInventory(input_text);
  }

  return {false, {}, "Unknown built-in text processor: " + definition.builtin_id};
}

}  // namespace textlt
