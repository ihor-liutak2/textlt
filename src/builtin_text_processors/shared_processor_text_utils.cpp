bool IsLatinLetter(std::uint32_t codepoint) {
  return IsAsciiLower(codepoint) || IsAsciiUpper(codepoint);
}

bool IsCyrillicLetterCodepoint(std::uint32_t codepoint) {
  return IsCyrillicLower(codepoint) || IsCyrillicUpper(codepoint);
}

bool IsWordCodepoint(std::uint32_t codepoint) {
  return IsLetterOrDigitCodepoint(codepoint) || codepoint == 0x2019 || codepoint == 0x02BC;
}

bool IsPunctuationRequiringNoSpaceBefore(std::uint32_t codepoint) {
  return codepoint == '.' || codepoint == ',' || codepoint == ';' ||
      codepoint == ':' || codepoint == '!' || codepoint == '?' ||
      codepoint == 0x2026;
}

bool IsOpeningBracketCodepoint(std::uint32_t codepoint) {
  return codepoint == '(' || codepoint == '[' || codepoint == '{' ||
      codepoint == 0x00AB || codepoint == 0x201E || codepoint == 0x201C;
}

bool IsClosingBracketCodepoint(std::uint32_t codepoint) {
  return codepoint == ')' || codepoint == ']' || codepoint == '}' ||
      codepoint == 0x00BB || codepoint == 0x201D;
}

bool IsWordBoundaryCodepoint(std::uint32_t codepoint) {
  return !IsWordCodepoint(codepoint);
}

std::vector<std::uint32_t> TextToCodepoints(const std::string& input_text) {
  return DecodeUtf8(input_text);
}

std::string CodepointsToText(const std::vector<std::uint32_t>& codepoints) {
  std::string output;
  for (std::uint32_t codepoint : codepoints) {
    output += EncodeUtf8(codepoint);
  }
  return output;
}

std::string CodepointToText(std::uint32_t codepoint) {
  return EncodeUtf8(codepoint);
}

std::string NormalizeMarkdownLineText(std::string line) {
  return TrimSpacesFromLine(CollapseHorizontalSpacesInLine(line));
}

std::vector<std::string> SplitSemicolonCells(const std::string& line) {
  std::vector<std::string> cells;
  std::string current;
  for (char ch : line) {
    if (ch == ';') {
      cells.push_back(TrimSpacesFromLine(current));
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  cells.push_back(TrimSpacesFromLine(current));
  return cells;
}

std::string JoinSemicolonCells(const std::vector<std::string>& cells) {
  std::string output;
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (index > 0) {
      output += "; ";
    }
    output += TrimSpacesFromLine(CollapseHorizontalSpacesInLine(cells[index]));
  }
  return output;
}

bool AllCellsEmpty(const std::string& line) {
  for (const std::string& cell : SplitSemicolonCells(line)) {
    if (!TrimSpacesFromLine(cell).empty()) {
      return false;
    }
  }
  return true;
}

std::string RemoveMarkdownListMarker(std::string line) {
  line = TrimSpacesFromLine(std::move(line));
  if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
    return TrimSpacesFromLine(line.substr(2));
  }

  std::size_t index = 0;
  while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0) {
    ++index;
  }
  if (index > 0 && index + 1 < line.size() && line[index] == '.' && line[index + 1] == ' ') {
    return TrimSpacesFromLine(line.substr(index + 2));
  }

  return line;
}

bool IsNumberedMarkdownLine(const std::string& line) {
  const std::string trimmed = TrimSpacesFromLine(line);
  std::size_t index = 0;
  while (index < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[index])) != 0) {
    ++index;
  }
  return index > 0 && index + 1 < trimmed.size() && trimmed[index] == '.' && trimmed[index + 1] == ' ';
}

std::string StripMarkdownInlineFormatting(const std::string& input_text) {
  std::string output;
  output.reserve(input_text.size());
  bool inside_link_text = false;
  bool skipping_link_url = false;
  int link_parentheses = 0;

  for (std::size_t index = 0; index < input_text.size(); ++index) {
    const char ch = input_text[index];
    if (skipping_link_url) {
      if (ch == '(') {
        ++link_parentheses;
      } else if (ch == ')') {
        if (link_parentheses == 0) {
          skipping_link_url = false;
        } else {
          --link_parentheses;
        }
      }
      continue;
    }

    if (ch == '[') {
      inside_link_text = true;
      continue;
    }
    if (inside_link_text && ch == ']') {
      inside_link_text = false;
      if (index + 1 < input_text.size() && input_text[index + 1] == '(') {
        skipping_link_url = true;
        link_parentheses = 0;
        ++index;
      }
      continue;
    }

    if (ch == '*' || ch == '_' || ch == '`') {
      continue;
    }

    output.push_back(ch);
  }
  return output;
}

std::vector<std::string> ExtractWords(const std::string& text) {
  std::vector<std::string> words;
  std::string current;
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    if (IsWordCodepoint(codepoint)) {
      current += EncodeUtf8(ToLowerCodepoint(codepoint));
      continue;
    }
    if (!current.empty()) {
      words.push_back(current);
      current.clear();
    }
  }
  if (!current.empty()) {
    words.push_back(current);
  }
  return words;
}

std::string MakeTextPreview(const std::string& text, std::size_t max_codepoints) {
  std::string output;
  std::size_t count = 0;
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    if (count >= max_codepoints) {
      output += "...";
      break;
    }
    if (codepoint == '\n' || codepoint == '\r' || codepoint == '\t') {
      output.push_back(' ');
    } else {
      output += EncodeUtf8(codepoint);
    }
    ++count;
  }
  return TrimSpacesFromLine(output);
}
