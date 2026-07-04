std::size_t CountCodepoints(const std::string& text);

using ParagraphBlock = std::vector<std::string>;

std::vector<ParagraphBlock> SplitParagraphBlocks(const std::string& input_text) {
  std::vector<ParagraphBlock> paragraphs;
  ParagraphBlock current;

  for (const std::string& line : SplitLines(input_text)) {
    if (IsBlankLine(line)) {
      if (!current.empty()) {
        paragraphs.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(line);
  }

  if (!current.empty()) {
    paragraphs.push_back(std::move(current));
  }
  return paragraphs;
}

std::string JoinParagraphTexts(
    const std::vector<std::string>& paragraphs,
    bool had_final_newline) {
  std::string result;
  for (std::size_t index = 0; index < paragraphs.size(); ++index) {
    if (index > 0) {
      result += "\n\n";
    }
    result += paragraphs[index];
  }
  if (had_final_newline && !result.empty()) {
    result += '\n';
  }
  return result;
}

std::string CollapseHorizontalSpacesInLine(const std::string& line) {
  std::string output;
  output.reserve(line.size());
  bool in_space = false;
  for (std::uint32_t codepoint : DecodeUtf8(line)) {
    if (IsHorizontalSpaceCodepoint(codepoint)) {
      if (!in_space) {
        output.push_back(' ');
      }
      in_space = true;
      continue;
    }
    output += EncodeUtf8(codepoint);
    in_space = false;
  }
  return output;
}

std::string ParagraphBlockToSingleLine(const ParagraphBlock& block) {
  std::string paragraph;
  for (const std::string& raw_line : block) {
    const std::string line = TrimSpacesFromLine(CollapseHorizontalSpacesInLine(raw_line));
    if (line.empty()) {
      continue;
    }
    if (!paragraph.empty()) {
      paragraph.push_back(' ');
    }
    paragraph += line;
  }
  return paragraph;
}

std::vector<std::string> SplitParagraphsToSingleLines(const std::string& input_text) {
  std::vector<std::string> paragraphs;
  for (const ParagraphBlock& block : SplitParagraphBlocks(input_text)) {
    const std::string paragraph = ParagraphBlockToSingleLine(block);
    if (!paragraph.empty()) {
      paragraphs.push_back(paragraph);
    }
  }
  return paragraphs;
}

std::vector<std::string> WrapSingleParagraph(
    const std::string& paragraph,
    std::size_t width) {
  std::vector<std::string> output;
  std::istringstream stream(paragraph);
  std::string word;
  std::string line;

  while (stream >> word) {
    if (line.empty()) {
      line = word;
      continue;
    }

    const std::size_t next_length = CountCodepoints(line) + 1 + CountCodepoints(word);
    if (next_length > width) {
      output.push_back(line);
      line = word;
    } else {
      line.push_back(' ');
      line += word;
    }
  }

  if (!line.empty()) {
    output.push_back(line);
  }
  return output;
}

BuiltinTextProcessorResult TrimParagraphs(const std::string& input_text) {
  std::vector<std::string> output;
  bool changed = false;

  for (const ParagraphBlock& block : SplitParagraphBlocks(input_text)) {
    if (!output.empty()) {
      output.push_back("");
    }
    for (const std::string& line : block) {
      const std::string trimmed = TrimSpacesFromLine(line);
      output.push_back(trimmed);
      if (trimmed != line) {
        changed = true;
      }
    }
  }

  const std::string result = JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text));
  if (!changed && result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult NormalizeParagraphSpaces(const std::string& input_text) {
  const std::vector<std::string> paragraphs = SplitParagraphsToSingleLines(input_text);
  const std::string result = JoinParagraphTexts(paragraphs, HasFinalNewline(input_text));
  if (result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult RemoveDuplicateParagraphs(const std::string& input_text) {
  const std::vector<std::string> paragraphs = SplitParagraphsToSingleLines(input_text);
  std::vector<std::string> output;
  std::unordered_set<std::string> seen;
  bool changed = false;

  for (const std::string& paragraph : paragraphs) {
    const std::string key = ToLowerAscii(TrimSpacesFromLine(paragraph));
    if (seen.find(key) != seen.end()) {
      changed = true;
      continue;
    }
    seen.insert(key);
    output.push_back(paragraph);
  }

  const std::string result = JoinParagraphTexts(output, HasFinalNewline(input_text));
  if (!changed && result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult CollapseParagraphGaps(
    const std::string& input_text,
    std::size_t max_blank_lines) {
  std::vector<std::string> lines = SplitLines(input_text);
  std::vector<std::string> output;
  std::size_t blank_count = 0;
  bool changed = false;

  for (const std::string& line : lines) {
    if (IsBlankLine(line)) {
      if (blank_count < max_blank_lines) {
        output.push_back("");
      } else {
        changed = true;
      }
      ++blank_count;
      if (!line.empty()) {
        changed = true;
      }
      continue;
    }
    blank_count = 0;
    output.push_back(line);
  }

  const std::string result = JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text));
  if (!changed && result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult UnwrapParagraphs(const std::string& input_text) {
  return NormalizeParagraphSpaces(input_text);
}

BuiltinTextProcessorResult WrapParagraphs(
    const std::string& input_text,
    std::size_t width) {
  const std::vector<std::string> paragraphs = SplitParagraphsToSingleLines(input_text);
  std::vector<std::string> output;

  for (const std::string& paragraph : paragraphs) {
    if (!output.empty()) {
      output.push_back("");
    }
    const std::vector<std::string> wrapped = WrapSingleParagraph(paragraph, width);
    output.insert(output.end(), wrapped.begin(), wrapped.end());
  }

  const std::string result = JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text));
  if (result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

bool IsSentenceEndCodepoint(std::uint32_t codepoint) {
  return codepoint == '.' || codepoint == '!' || codepoint == '?' ||
      codepoint == 0x2026;
}

BuiltinTextProcessorResult SplitSentencesToLines(const std::string& input_text) {
  const std::vector<std::string> paragraphs = SplitParagraphsToSingleLines(input_text);
  std::vector<std::string> output;

  for (const std::string& paragraph : paragraphs) {
    if (!output.empty()) {
      output.push_back("");
    }

    std::string sentence;
    bool after_sentence_end = false;
    for (std::uint32_t codepoint : DecodeUtf8(paragraph)) {
      sentence += EncodeUtf8(codepoint);
      if (IsSentenceEndCodepoint(codepoint)) {
        after_sentence_end = true;
        continue;
      }
      if (after_sentence_end && IsHorizontalSpaceCodepoint(codepoint)) {
        std::string trimmed = TrimSpacesFromLine(sentence);
        if (!trimmed.empty()) {
          output.push_back(trimmed);
        }
        sentence.clear();
        after_sentence_end = false;
        continue;
      }
      if (!IsHorizontalSpaceCodepoint(codepoint)) {
        after_sentence_end = false;
      }
    }

    const std::string trimmed = TrimSpacesFromLine(sentence);
    if (!trimmed.empty()) {
      output.push_back(trimmed);
    }
  }

  const std::string result = JoinLinesPreservingFinalNewline(output, HasFinalNewline(input_text));
  if (result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}

BuiltinTextProcessorResult MergeShortParagraphs(
    const std::string& input_text,
    std::size_t min_chars) {
  const std::vector<std::string> paragraphs = SplitParagraphsToSingleLines(input_text);
  std::vector<std::string> output;
  std::string current;

  auto flush_current = [&]() {
    if (!current.empty()) {
      output.push_back(current);
      current.clear();
    }
  };

  for (const std::string& paragraph : paragraphs) {
    if (current.empty()) {
      current = paragraph;
    } else {
      current += ' ';
      current += paragraph;
    }

    if (CountCodepoints(current) >= min_chars) {
      flush_current();
    }
  }
  flush_current();

  const std::string result = JoinParagraphTexts(output, HasFinalNewline(input_text));
  if (result == input_text) {
    return SuccessResult(input_text);
  }
  return SuccessResult(result);
}
