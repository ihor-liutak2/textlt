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
