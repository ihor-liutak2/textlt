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
