BuiltinTextProcessorResult AnalyzeMixedCyrillicLatin(const std::string& input_text) {
  std::ostringstream report;
  report << "Mixed Cyrillic/Latin words\n";
  std::string current;
  bool has_latin = false;
  bool has_cyrillic = false;
  std::size_t count = 0;
  std::size_t line = 1;

  auto flush = [&]() {
    if (!current.empty() && has_latin && has_cyrillic) {
      ++count;
      if (count <= 50) {
        report << "Line " << line << ": " << current << '\n';
      }
    }
    current.clear();
    has_latin = false;
    has_cyrillic = false;
  };

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == '\n') {
      flush();
      ++line;
      continue;
    }
    if (IsWordCodepoint(codepoint)) {
      current += EncodeUtf8(codepoint);
      has_latin = has_latin || IsLatinLetter(codepoint);
      has_cyrillic = has_cyrillic || IsCyrillicLetterCodepoint(codepoint);
      continue;
    }
    flush();
  }
  flush();

  report << "Total: " << count << '\n';
  if (count > 50) {
    report << "Shown first 50 matches only.\n";
  }
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeWordFrequency(
    const std::string& input_text,
    std::size_t top_count,
    std::size_t min_length) {
  std::unordered_map<std::string, std::size_t> counts;
  for (const std::string& word : ExtractWords(input_text)) {
    if (CountCodepoints(word) >= min_length) {
      ++counts[word];
    }
  }
  std::vector<std::pair<std::string, std::size_t>> items(counts.begin(), counts.end());
  std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
    if (left.second != right.second) {
      return left.second > right.second;
    }
    return left.first < right.first;
  });

  std::ostringstream report;
  report << "Word frequency\n";
  const std::size_t limit = std::min(top_count, items.size());
  for (std::size_t index = 0; index < limit; ++index) {
    report << items[index].first << ": " << items[index].second << '\n';
  }
  report << "Unique words: " << items.size() << '\n';
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeLongSentences(
    const std::string& input_text,
    std::size_t max_sentence_length) {
  std::ostringstream report;
  report << "Long sentences over " << max_sentence_length << " characters\n";
  std::string sentence;
  std::size_t count = 0;
  std::size_t line = 1;
  std::size_t sentence_start_line = 1;
  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (sentence.empty() && !IsHorizontalSpaceCodepoint(codepoint)) {
      sentence_start_line = line;
    }
    sentence += EncodeUtf8(codepoint);
    if (codepoint == '\n') {
      ++line;
    }
    if (IsSentenceEndCodepoint(codepoint)) {
      const std::string trimmed = TrimSpacesFromLine(sentence);
      const std::size_t length = CountCodepoints(trimmed);
      if (length > max_sentence_length) {
        ++count;
        if (count <= 30) {
          report << "Line " << sentence_start_line << ": " << length << " chars: "
                 << MakeTextPreview(trimmed, 120) << '\n';
        }
      }
      sentence.clear();
    }
  }
  const std::string trimmed = TrimSpacesFromLine(sentence);
  if (!trimmed.empty() && CountCodepoints(trimmed) > max_sentence_length) {
    ++count;
    if (count <= 30) {
      report << "Line " << sentence_start_line << ": " << CountCodepoints(trimmed)
             << " chars: " << MakeTextPreview(trimmed, 120) << '\n';
    }
  }
  report << "Total: " << count << '\n';
  if (count > 30) {
    report << "Shown first 30 sentences only.\n";
  }
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeRepeatedWords(const std::string& input_text) {
  std::ostringstream report;
  report << "Repeated adjacent words\n";
  std::string previous;
  std::string current;
  std::size_t line = 1;
  std::size_t count = 0;

  auto flush = [&]() {
    if (current.empty()) {
      return;
    }
    if (!previous.empty() && current == previous) {
      ++count;
      if (count <= 50) {
        report << "Line " << line << ": " << current << '\n';
      }
    }
    previous = current;
    current.clear();
  };

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == '\n') {
      flush();
      previous.clear();
      ++line;
      continue;
    }
    if (IsWordCodepoint(codepoint)) {
      current += EncodeUtf8(ToLowerCodepoint(codepoint));
      continue;
    }
    flush();
  }
  flush();
  report << "Total: " << count << '\n';
  if (count > 50) {
    report << "Shown first 50 matches only.\n";
  }
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeUnbalancedQuotes(const std::string& input_text) {
  std::size_t straight = 0;
  std::size_t guillemet_open = 0;
  std::size_t guillemet_close = 0;
  std::size_t curly_open = 0;
  std::size_t curly_close = 0;
  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == '"') {
      ++straight;
    } else if (codepoint == 0x00AB) {
      ++guillemet_open;
    } else if (codepoint == 0x00BB) {
      ++guillemet_close;
    } else if (codepoint == 0x201C || codepoint == 0x201E) {
      ++curly_open;
    } else if (codepoint == 0x201D) {
      ++curly_close;
    }
  }
  std::ostringstream report;
  report << "Unbalanced quotes\n";
  report << "Straight double quotes: " << straight << " (" << (straight % 2 == 0 ? "balanced" : "odd") << ")\n";
  report << "Guillemets: « " << guillemet_open << ", » " << guillemet_close
         << " (" << (guillemet_open == guillemet_close ? "balanced" : "unbalanced") << ")\n";
  report << "Curly double quotes: open " << curly_open << ", close " << curly_close
         << " (" << (curly_open == curly_close ? "balanced" : "unbalanced") << ")\n";
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeUnbalancedBrackets(const std::string& input_text) {
  std::vector<std::pair<std::uint32_t, std::size_t>> stack;
  std::ostringstream report;
  report << "Unbalanced brackets\n";
  std::size_t line = 1;
  std::size_t issues = 0;

  auto expected_close = [](std::uint32_t open) {
    if (open == '(') return static_cast<std::uint32_t>(')');
    if (open == '[') return static_cast<std::uint32_t>(']');
    if (open == '{') return static_cast<std::uint32_t>('}');
    return static_cast<std::uint32_t>(0);
  };

  for (std::uint32_t codepoint : DecodeUtf8(input_text)) {
    if (codepoint == '\n') {
      ++line;
      continue;
    }
    if (codepoint == '(' || codepoint == '[' || codepoint == '{') {
      stack.push_back({codepoint, line});
      continue;
    }
    if (codepoint == ')' || codepoint == ']' || codepoint == '}') {
      if (stack.empty() || expected_close(stack.back().first) != codepoint) {
        ++issues;
        if (issues <= 50) {
          report << "Line " << line << ": unexpected " << CodepointToText(codepoint) << '\n';
        }
      } else {
        stack.pop_back();
      }
    }
  }

  for (const auto& item : stack) {
    ++issues;
    if (issues <= 50) {
      report << "Line " << item.second << ": unclosed " << CodepointToText(item.first) << '\n';
    }
  }
  report << "Total issues: " << issues << '\n';
  return ReportResult(report.str());
}

BuiltinTextProcessorResult AnalyzeSuspiciousPunctuation(const std::string& input_text) {
  const std::vector<std::uint32_t> codepoints = DecodeUtf8(input_text);
  std::ostringstream report;
  report << "Suspicious punctuation\n";
  std::size_t line = 1;
  std::size_t issues = 0;
  for (std::size_t index = 0; index < codepoints.size(); ++index) {
    const std::uint32_t codepoint = codepoints[index];
    if (codepoint == '\n') {
      ++line;
      continue;
    }

    bool suspicious = false;
    std::string message;
    if (index + 1 < codepoints.size() && codepoint == codepoints[index + 1] &&
        (codepoint == ',' || codepoint == ';' || codepoint == ':' || codepoint == '!' || codepoint == '?')) {
      suspicious = true;
      message = "repeated punctuation " + CodepointToText(codepoint) + CodepointToText(codepoint);
    } else if (index + 1 < codepoints.size() && codepoint == '.' && codepoints[index + 1] == '.' &&
               !(index + 2 < codepoints.size() && codepoints[index + 2] == '.')) {
      suspicious = true;
      message = "double dot";
    } else if (codepoint == ' ' && index + 1 < codepoints.size() &&
               IsPunctuationRequiringNoSpaceBefore(codepoints[index + 1])) {
      suspicious = true;
      message = "space before punctuation";
    } else if (IsPunctuationRequiringNoSpaceBefore(codepoint) && index + 1 < codepoints.size() &&
               IsWordCodepoint(codepoints[index + 1])) {
      suspicious = true;
      message = "missing space after punctuation";
    }

    if (suspicious) {
      ++issues;
      if (issues <= 50) {
        report << "Line " << line << ": " << message << '\n';
      }
    }
  }
  report << "Total: " << issues << '\n';
  if (issues > 50) {
    report << "Shown first 50 issues only.\n";
  }
  return ReportResult(report.str());
}

}  // namespace
