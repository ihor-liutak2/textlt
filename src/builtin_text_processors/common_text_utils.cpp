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
