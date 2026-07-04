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
