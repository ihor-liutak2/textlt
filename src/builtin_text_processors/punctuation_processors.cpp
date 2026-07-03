BuiltinTextProcessorResult NormalizeQuotes(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool open_quote = true;
  bool changed = false;

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == '\n' || codepoint == '\r') {
      output += EncodeUtf8(codepoint);
      open_quote = true;
      continue;
    }

    const bool double_quote = codepoint == '"' || codepoint == 0x201C || codepoint == 0x201D ||
        codepoint == 0x201E || codepoint == 0x00AB || codepoint == 0x00BB;
    if (double_quote) {
      output += open_quote ? "«" : "»";
      open_quote = !open_quote;
      if ((open_quote && codepoint != 0x00BB) || (!open_quote && codepoint != 0x00AB)) {
        changed = true;
      }
      continue;
    }

    output += EncodeUtf8(codepoint);
  }

  if (!changed && output == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult NormalizeDashes(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::size_t index = 0; index < input_text.size();) {
    if (index + 1 < input_text.size() && input_text[index] == '-' && input_text[index + 1] == '-') {
      output += "—";
      index += 2;
      changed = true;
      continue;
    }

    const std::vector<std::uint32_t> one = DecodeUtf8(input_text.substr(index, 4));
    if (!one.empty() && (one[0] == 0x2013 || one[0] == 0x2212)) {
      output += "—";
      index += EncodeUtf8(one[0]).size();
      changed = true;
      continue;
    }

    output.push_back(input_text[index]);
    ++index;
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult NormalizeEllipsis(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool changed = false;

  for (std::size_t index = 0; index < input_text.size();) {
    if (index + 2 < input_text.size() && input_text[index] == '.' &&
        input_text[index + 1] == '.' && input_text[index + 2] == '.') {
      output += "…";
      index += 3;
      changed = true;
      continue;
    }
    output.push_back(input_text[index]);
    ++index;
  }

  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(output);
}

BuiltinTextProcessorResult FixPunctuationSpaces(const std::string& input_text) {
  const std::vector<std::uint32_t> codepoints = TextToCodepoints(input_text);
  std::vector<std::uint32_t> output;
  output.reserve(codepoints.size());
  bool changed = false;

  for (std::size_t index = 0; index < codepoints.size(); ++index) {
    const std::uint32_t codepoint = codepoints[index];
    if (codepoint == ' ' || codepoint == '\t' || codepoint == 0x00A0) {
      std::size_t next = index;
      while (next + 1 < codepoints.size() &&
             (codepoints[next + 1] == ' ' || codepoints[next + 1] == '\t' || codepoints[next + 1] == 0x00A0)) {
        ++next;
      }
      if (next + 1 < codepoints.size() && IsPunctuationRequiringNoSpaceBefore(codepoints[next + 1])) {
        index = next;
        changed = true;
        continue;
      }
      if (!output.empty() && output.back() == 0x2014) {
        index = next;
        changed = true;
        continue;
      }
      output.push_back(' ');
      if (codepoint != ' ' || next != index) {
        changed = true;
      }
      index = next;
      continue;
    }

    if (codepoint == 0x2014) {
      while (!output.empty() && output.back() == ' ') {
        output.pop_back();
        changed = true;
      }
      if (!output.empty() && output.back() != '\n') {
        output.push_back(' ');
      }
      output.push_back(codepoint);
      if (index + 1 < codepoints.size() && codepoints[index + 1] != ' ' && codepoints[index + 1] != '\n') {
        output.push_back(' ');
        changed = true;
      }
      continue;
    }

    output.push_back(codepoint);
    if (IsPunctuationRequiringNoSpaceBefore(codepoint) && index + 1 < codepoints.size()) {
      const std::uint32_t next = codepoints[index + 1];
      if (next != ' ' && next != '\n' && next != '\r' && !IsClosingBracketCodepoint(next) &&
          !IsPunctuationRequiringNoSpaceBefore(next)) {
        output.push_back(' ');
        changed = true;
      }
    }
  }

  const std::string result = CodepointsToText(output);
  if (!changed && result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult NormalizeUkrainianApostrophe(const std::string& input_text) {
  const std::vector<std::uint32_t> codepoints = TextToCodepoints(input_text);
  std::vector<std::uint32_t> output;
  output.reserve(codepoints.size());
  bool changed = false;

  for (std::size_t index = 0; index < codepoints.size(); ++index) {
    const std::uint32_t codepoint = codepoints[index];
    const bool apostrophe_like = codepoint == '\'' || codepoint == 0x2018 || codepoint == 0x2019 ||
        codepoint == 0x02BC || codepoint == 0x0060 || codepoint == 0x00B4;
    if (apostrophe_like && index > 0 && index + 1 < codepoints.size() &&
        IsCyrillicLetterCodepoint(codepoints[index - 1]) && IsCyrillicLetterCodepoint(codepoints[index + 1])) {
      output.push_back(0x2019);
      if (codepoint != 0x2019) {
        changed = true;
      }
      continue;
    }
    output.push_back(codepoint);
  }

  const std::string result = CodepointsToText(output);
  if (!changed && result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult NormalizeDialogueDashes(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    std::string trimmed_left = TrimLeadingSpacesFromLine(line);
    const std::size_t leading = line.size() - trimmed_left.size();
    if (trimmed_left.empty()) {
      continue;
    }
    if (trimmed_left.rfind("-", 0) == 0 || trimmed_left.rfind("–", 0) == 0 || trimmed_left.rfind("—", 0) == 0) {
      std::string rest;
      if (trimmed_left.rfind("—", 0) == 0 || trimmed_left.rfind("–", 0) == 0) {
        rest = trimmed_left.substr(3);
      } else {
        rest = trimmed_left.substr(1);
      }
      rest = TrimLeadingSpacesFromLine(rest);
      const std::string updated = std::string(leading, ' ') + "— " + rest;
      if (updated != line) {
        line = updated;
        changed = true;
      }
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult NormalizeUkrainianPunctuation(const std::string& input_text) {
  BuiltinTextProcessorResult result = NormalizeEllipsis(input_text);
  result = NormalizeDashes(result.text);
  result = NormalizeQuotes(result.text);
  result = NormalizeUkrainianApostrophe(result.text);
  result = NormalizeDialogueDashes(result.text);
  return FixPunctuationSpaces(result.text);
}
