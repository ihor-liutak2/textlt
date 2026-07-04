std::vector<std::uint32_t> DecodeUtf8(const std::string& value) {
  std::vector<std::uint32_t> codepoints;
  codepoints.reserve(value.size());

  for (std::size_t i = 0; i < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[i]);
    if (first < 0x80) {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    std::uint32_t codepoint = 0;
    std::size_t expected = 0;
    if ((first & 0xE0) == 0xC0) {
      codepoint = first & 0x1F;
      expected = 2;
    } else if ((first & 0xF0) == 0xE0) {
      codepoint = first & 0x0F;
      expected = 3;
    } else if ((first & 0xF8) == 0xF0) {
      codepoint = first & 0x07;
      expected = 4;
    } else {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    if (i + expected > value.size()) {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    bool valid = true;
    for (std::size_t j = 1; j < expected; ++j) {
      const unsigned char next = static_cast<unsigned char>(value[i + j]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6) | (next & 0x3F);
    }

    if (!valid) {
      codepoints.push_back(first);
      ++i;
      continue;
    }

    codepoints.push_back(codepoint);
    i += expected;
  }

  return codepoints;
}

std::string EncodeUtf8(std::uint32_t codepoint) {
  std::string output;
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return output;
}

bool IsAsciiLower(std::uint32_t codepoint) {
  return codepoint >= 'a' && codepoint <= 'z';
}

bool IsAsciiUpper(std::uint32_t codepoint) {
  return codepoint >= 'A' && codepoint <= 'Z';
}

bool IsCyrillicUpper(std::uint32_t codepoint) {
  return (codepoint >= 0x0410 && codepoint <= 0x042F) ||
      codepoint == 0x0401 || codepoint == 0x0404 || codepoint == 0x0406 ||
      codepoint == 0x0407 || codepoint == 0x0490;
}

bool IsUpperForSort(std::uint32_t codepoint) {
  return IsAsciiUpper(codepoint) || IsCyrillicUpper(codepoint);
}

std::uint32_t ToLowerCodepoint(std::uint32_t codepoint) {
  if (IsAsciiUpper(codepoint)) {
    return codepoint + 32;
  }
  if (codepoint >= 0x0410 && codepoint <= 0x042F) {
    return codepoint + 32;
  }

  switch (codepoint) {
    case 0x0401: return 0x0451;  // Ё -> ё
    case 0x0404: return 0x0454;  // Є -> є
    case 0x0406: return 0x0456;  // І -> і
    case 0x0407: return 0x0457;  // Ї -> ї
    case 0x0490: return 0x0491;  // Ґ -> ґ
    default: return codepoint;
  }
}

bool IsCyrillicLower(std::uint32_t codepoint) {
  return (codepoint >= 0x0430 && codepoint <= 0x044F) ||
      codepoint == 0x0451 || codepoint == 0x0454 || codepoint == 0x0456 ||
      codepoint == 0x0457 || codepoint == 0x0491;
}

std::uint32_t ToUpperCodepoint(std::uint32_t codepoint) {
  if (IsAsciiLower(codepoint)) {
    return codepoint - 32;
  }
  if (codepoint >= 0x0430 && codepoint <= 0x044F) {
    return codepoint - 32;
  }

  switch (codepoint) {
    case 0x0451: return 0x0401;  // ё -> Ё
    case 0x0454: return 0x0404;  // є -> Є
    case 0x0456: return 0x0406;  // і -> І
    case 0x0457: return 0x0407;  // ї -> Ї
    case 0x0491: return 0x0490;  // ґ -> Ґ
    default: return codepoint;
  }
}

std::uint32_t ToggleCodepointCase(std::uint32_t codepoint) {
  if (IsAsciiLower(codepoint) || IsCyrillicLower(codepoint)) {
    return ToUpperCodepoint(codepoint);
  }
  if (IsAsciiUpper(codepoint) || IsCyrillicUpper(codepoint)) {
    return ToLowerCodepoint(codepoint);
  }
  return codepoint;
}

bool IsLetterOrDigitCodepoint(std::uint32_t codepoint) {
  return codepoint == '_' ||
      (codepoint >= '0' && codepoint <= '9') ||
      IsAsciiLower(codepoint) || IsAsciiUpper(codepoint) ||
      IsCyrillicLower(codepoint) || IsCyrillicUpper(codepoint);
}


bool ContainsUkrainianLetter(const std::string& text) {
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    const std::uint32_t lower = ToLowerCodepoint(codepoint);
    if (lower == 0x0491 || lower == 0x0454 || lower == 0x0456 || lower == 0x0457) {
      return true;
    }
  }
  return false;
}

bool ContainsCyrillicLetter(const std::string& text) {
  for (std::uint32_t codepoint : DecodeUtf8(text)) {
    if (IsCyrillicLower(ToLowerCodepoint(codepoint))) {
      return true;
    }
  }
  return false;
}

SortLanguage ResolveAutoSortLanguage(SortLanguage language, const std::string& text) {
  if (language != SortLanguage::Auto) {
    return language;
  }
  if (ContainsUkrainianLetter(text)) {
    return SortLanguage::Ukrainian;
  }
  if (ContainsCyrillicLetter(text)) {
    return SortLanguage::Russian;
  }
  return SortLanguage::English;
}

int RussianLetterWeight(std::uint32_t lower) {
  switch (lower) {
    case 0x0430: return 1;   // а
    case 0x0431: return 2;   // б
    case 0x0432: return 3;   // в
    case 0x0433: return 4;   // г
    case 0x0434: return 5;   // д
    case 0x0435: return 6;   // е
    case 0x0451: return 7;   // ё
    case 0x0436: return 8;   // ж
    case 0x0437: return 9;   // з
    case 0x0438: return 10;  // и
    case 0x0439: return 11;  // й
    case 0x043A: return 12;  // к
    case 0x043B: return 13;  // л
    case 0x043C: return 14;  // м
    case 0x043D: return 15;  // н
    case 0x043E: return 16;  // о
    case 0x043F: return 17;  // п
    case 0x0440: return 18;  // р
    case 0x0441: return 19;  // с
    case 0x0442: return 20;  // т
    case 0x0443: return 21;  // у
    case 0x0444: return 22;  // ф
    case 0x0445: return 23;  // х
    case 0x0446: return 24;  // ц
    case 0x0447: return 25;  // ч
    case 0x0448: return 26;  // ш
    case 0x0449: return 27;  // щ
    case 0x044A: return 28;  // ъ
    case 0x044B: return 29;  // ы
    case 0x044C: return 30;  // ь
    case 0x044D: return 31;  // э
    case 0x044E: return 32;  // ю
    case 0x044F: return 33;  // я
    default: return 0;
  }
}

int UkrainianLetterWeight(std::uint32_t lower) {
  switch (lower) {
    case 0x0430: return 1;   // а
    case 0x0431: return 2;   // б
    case 0x0432: return 3;   // в
    case 0x0433: return 4;   // г
    case 0x0491: return 5;   // ґ
    case 0x0434: return 6;   // д
    case 0x0435: return 7;   // е
    case 0x0454: return 8;   // є
    case 0x0436: return 9;   // ж
    case 0x0437: return 10;  // з
    case 0x0438: return 11;  // и
    case 0x0456: return 12;  // і
    case 0x0457: return 13;  // ї
    case 0x0439: return 14;  // й
    case 0x043A: return 15;  // к
    case 0x043B: return 16;  // л
    case 0x043C: return 17;  // м
    case 0x043D: return 18;  // н
    case 0x043E: return 19;  // о
    case 0x043F: return 20;  // п
    case 0x0440: return 21;  // р
    case 0x0441: return 22;  // с
    case 0x0442: return 23;  // т
    case 0x0443: return 24;  // у
    case 0x0444: return 25;  // ф
    case 0x0445: return 26;  // х
    case 0x0446: return 27;  // ц
    case 0x0447: return 28;  // ч
    case 0x0448: return 29;  // ш
    case 0x0449: return 30;  // щ
    case 0x044C: return 31;  // ь
    case 0x044E: return 32;  // ю
    case 0x044F: return 33;  // я
    default: return 0;
  }
}

int EnglishLetterWeight(std::uint32_t lower) {
  if (lower >= 'a' && lower <= 'z') {
    return static_cast<int>(lower - 'a') + 1;
  }
  return 0;
}

SortToken MakeSortToken(std::uint32_t codepoint, SortLanguage language, bool case_sensitive) {
  const std::uint32_t lower = ToLowerCodepoint(codepoint);
  if (language == SortLanguage::Binary) {
    const std::uint32_t binary_codepoint = case_sensitive ? codepoint : lower;
    return {1, static_cast<int>(binary_codepoint), 0, binary_codepoint};
  }

  const int case_weight = case_sensitive && IsUpperForSort(codepoint) ? 0 :
      (case_sensitive ? 1 : 0);

  if (language == SortLanguage::Russian) {
    const int weight = RussianLetterWeight(lower);
    if (weight > 0) {
      return {1, weight, case_weight, lower};
    }
  }

  if (language == SortLanguage::Ukrainian) {
    const int weight = UkrainianLetterWeight(lower);
    if (weight > 0) {
      return {1, weight, case_weight, lower};
    }
  }

  if (language == SortLanguage::English) {
    const int weight = EnglishLetterWeight(lower);
    if (weight > 0) {
      return {1, weight, case_weight, lower};
    }
  }

  return {2, static_cast<int>(lower), case_weight, lower};
}

std::vector<SortToken> BuildSortKey(
    const std::string& value,
    SortLanguage language,
    bool case_sensitive) {
  std::vector<SortToken> key;
  for (std::uint32_t codepoint : DecodeUtf8(value)) {
    key.push_back(MakeSortToken(codepoint, language, case_sensitive));
  }
  return key;
}

bool SortKeyLess(const std::vector<SortToken>& left, const std::vector<SortToken>& right) {
  const std::size_t common_size = std::min(left.size(), right.size());
  for (std::size_t i = 0; i < common_size; ++i) {
    const SortToken& l = left[i];
    const SortToken& r = right[i];
    if (l.group != r.group) {
      return l.group < r.group;
    }
    if (l.weight != r.weight) {
      return l.weight < r.weight;
    }
    if (l.case_weight != r.case_weight) {
      return l.case_weight < r.case_weight;
    }
    if (l.codepoint != r.codepoint) {
      return l.codepoint < r.codepoint;
    }
  }
  return left.size() < right.size();
}

std::string SortKeyToString(const std::vector<SortToken>& key) {
  std::string result;
  for (const SortToken& token : key) {
    result += std::to_string(token.group);
    result += ':';
    result += std::to_string(token.weight);
    result += ':';
    result += std::to_string(token.case_weight);
    result += ':';
    result += std::to_string(token.codepoint);
    result += ';';
  }
  return result;
}
