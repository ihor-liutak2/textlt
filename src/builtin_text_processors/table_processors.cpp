BuiltinTextProcessorResult NormalizeSemicolonRows(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    if (line.find(';') == std::string::npos) {
      continue;
    }
    const std::string updated = JoinSemicolonCells(SplitSemicolonCells(line));
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

BuiltinTextProcessorResult TabsToSemicolonRows(const std::string& input_text) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    if (line.find('\t') == std::string::npos) {
      continue;
    }
    std::vector<std::string> cells;
    std::string current;
    for (char ch : line) {
      if (ch == '\t') {
        cells.push_back(current);
        current.clear();
      } else {
        current.push_back(ch);
      }
    }
    cells.push_back(current);
    line = JoinSemicolonCells(cells);
    changed = true;
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult MultiSpaceColumnsToSemicolonRows(
    const std::string& input_text,
    std::size_t min_spaces) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    std::string output;
    std::size_t index = 0;
    bool line_changed = false;
    while (index < line.size()) {
      if (line[index] == ' ') {
        std::size_t end = index;
        while (end < line.size() && line[end] == ' ') {
          ++end;
        }
        const std::size_t count = end - index;
        if (count >= min_spaces && !TrimSpacesFromLine(output).empty() && end < line.size()) {
          output += "; ";
          line_changed = true;
        } else {
          output.append(count, ' ');
        }
        index = end;
        continue;
      }
      output.push_back(line[index]);
      ++index;
    }
    if (line_changed) {
      line = JoinSemicolonCells(SplitSemicolonCells(output));
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult RemoveEmptyTableRows(const std::string& input_text) {
  std::vector<std::string> output;
  bool changed = false;
  for (const std::string& line : SplitLines(input_text)) {
    if (line.find(';') != std::string::npos && AllCellsEmpty(line)) {
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

BuiltinTextProcessorResult RemoveDuplicateTableRows(const std::string& input_text) {
  std::vector<std::string> output;
  std::unordered_set<std::string> seen;
  bool changed = false;
  for (const std::string& line : SplitLines(input_text)) {
    if (line.find(';') == std::string::npos) {
      output.push_back(line);
      continue;
    }
    const std::string normalized = JoinSemicolonCells(SplitSemicolonCells(line));
    if (!seen.insert(ToLowerAscii(normalized)).second) {
      changed = true;
      continue;
    }
    output.push_back(normalized);
    if (normalized != line) {
      changed = true;
    }
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult ExtractTableColumn(
    const std::string& input_text,
    std::size_t column) {
  std::vector<std::string> output;
  bool changed = false;
  for (const std::string& line : SplitLines(input_text)) {
    if (line.find(';') == std::string::npos) {
      output.push_back(line);
      continue;
    }
    const std::vector<std::string> cells = SplitSemicolonCells(line);
    output.push_back(column <= cells.size() ? TrimSpacesFromLine(cells[column - 1]) : "");
    changed = true;
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text)));
}

BuiltinTextProcessorResult RemoveTableColumn(
    const std::string& input_text,
    std::size_t column) {
  std::vector<std::string> lines = SplitLines(input_text);
  bool changed = false;
  for (std::string& line : lines) {
    if (line.find(';') == std::string::npos) {
      continue;
    }
    std::vector<std::string> cells = SplitSemicolonCells(line);
    if (column == 0 || column > cells.size()) {
      continue;
    }
    cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(column - 1));
    line = JoinSemicolonCells(cells);
    changed = true;
  }
  if (!changed) {
    return SuccessResult(input_text);
  }
  return SuccessResult(JoinLinesPreservingFinalNewline(lines, HasFinalNewline(input_text)));
}
