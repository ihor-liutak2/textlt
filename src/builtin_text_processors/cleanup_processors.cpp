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
