BuiltinTextProcessorResult MarkdownAddHeading(const std::string& input_text, std::size_t level) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  const std::string marker(level, '#');
  for (std::string& line : lines) {
    if (IsBlankLine(line)) {
      continue;
    }
    std::string text = NormalizeMarkdownLineText(line);
    while (!text.empty() && text[0] == '#') {
      text.erase(text.begin());
    }
    text = TrimSpacesFromLine(std::move(text));
    const std::string updated = marker + " " + text;
    if (updated != line) {
      line = updated;
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownRemoveHeadings(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    std::string trimmed = TrimSpacesFromLine(line);
    std::size_t index = 0;
    while (index < trimmed.size() && trimmed[index] == '#') {
      ++index;
    }
    if (index > 0 && index <= 6 && index < trimmed.size() && trimmed[index] == ' ') {
      line = TrimSpacesFromLine(trimmed.substr(index + 1));
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownBulletList(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    if (IsBlankLine(line)) {
      continue;
    }
    const std::string updated = "- " + RemoveMarkdownListMarker(line);
    if (updated != line) {
      line = updated;
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownNumberedList(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  std::size_t number = 1;
  for (std::string& line : lines) {
    if (IsBlankLine(line)) {
      continue;
    }
    const std::string updated = std::to_string(number) + ". " + RemoveMarkdownListMarker(line);
    ++number;
    if (updated != line) {
      line = updated;
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownRenumberList(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  std::size_t number = 1;
  for (std::string& line : lines) {
    if (!IsNumberedMarkdownLine(line)) {
      continue;
    }
    const std::string updated = std::to_string(number) + ". " + RemoveMarkdownListMarker(line);
    ++number;
    if (updated != line) {
      line = updated;
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownQuoteBlock(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    if (IsBlankLine(line)) {
      continue;
    }
    std::string trimmed = TrimSpacesFromLine(line);
    if (trimmed.rfind(">", 0) == 0) {
      trimmed = TrimSpacesFromLine(trimmed.substr(1));
    }
    const std::string updated = "> " + trimmed;
    if (updated != line) {
      line = updated;
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownWrap(const std::string& input_text, const std::string& marker) {
  if (input_text.empty()) {
    return SuccessResult(input_text);
  }
  if (input_text.rfind(marker, 0) == 0 && input_text.size() >= marker.size() * 2 &&
      input_text.compare(input_text.size() - marker.size(), marker.size(), marker) == 0) {
    return SuccessResult(input_text);
  }
  return SuccessResult(marker + input_text + marker);
}

BuiltinTextProcessorResult MarkdownStripFormatting(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    std::string stripped = TrimSpacesFromLine(line);
    while (!stripped.empty() && stripped[0] == '>') {
      stripped = TrimSpacesFromLine(stripped.substr(1));
    }
    std::size_t heading = 0;
    while (heading < stripped.size() && stripped[heading] == '#') {
      ++heading;
    }
    if (heading > 0 && heading <= 6 && heading < stripped.size() && stripped[heading] == ' ') {
      stripped = TrimSpacesFromLine(stripped.substr(heading + 1));
    }
    stripped = RemoveMarkdownListMarker(stripped);
    stripped = StripMarkdownInlineFormatting(stripped);
    if (stripped != line) {
      line = stripped;
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MarkdownExtractHeadings(const std::string& input_text) {
  std::ostringstream report;
  report << "Markdown headings\n";
  std::size_t count = 0;
  const std::vector<std::string> lines = SplitLines(input_text);
  for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const std::string trimmed = TrimSpacesFromLine(lines[line_index]);
    std::size_t level = 0;
    while (level < trimmed.size() && trimmed[level] == '#') {
      ++level;
    }
    if (level == 0 || level > 6 || level >= trimmed.size() || trimmed[level] != ' ') {
      continue;
    }
    ++count;
    report << "Line " << (line_index + 1) << ": H" << level << " " << trimmed.substr(level + 1) << '\n';
  }
  report << "Total: " << count << '\n';
  return ReportResult(report.str());
}

BuiltinTextProcessorResult MarkdownGenerateToc(const std::string& input_text) {
  std::ostringstream toc;
  const std::vector<std::string> lines = SplitLines(input_text);
  std::size_t count = 0;
  for (const std::string& line : lines) {
    const std::string trimmed = TrimSpacesFromLine(line);
    std::size_t level = 0;
    while (level < trimmed.size() && trimmed[level] == '#') {
      ++level;
    }
    if (level == 0 || level > 6 || level >= trimmed.size() || trimmed[level] != ' ') {
      continue;
    }
    const std::string title = TrimSpacesFromLine(trimmed.substr(level + 1));
    const std::string indent((level - 1) * 2, ' ');
    toc << indent << "- " << title << '\n';
    ++count;
  }
  if (count == 0) {
    toc << "No Markdown headings found.\n";
  }
  return ReportResult(toc.str());
}
