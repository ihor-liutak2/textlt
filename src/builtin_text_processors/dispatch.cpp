bool IsBuiltinTextProcessor(const std::string& processor_id) {
  const std::vector<TextParserDefinition> processors = CreateBuiltinTextProcessors();
  return std::any_of(processors.begin(), processors.end(), [&](const TextParserDefinition& processor) {
    return processor.id == processor_id || processor.builtin_id == processor_id;
  });
}

BuiltinTextProcessorResult ApplyBuiltinTextProcessor(
    const TextParserDefinition& definition,
    const std::string& input_text,
    const std::unordered_map<std::string, std::string>& params) {
  if (definition.builtin_id == "builtin_indent_lines") {
    std::size_t indent_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "indent_width", 1, 64, indent_width, error)) {
      return {false, {}, error};
    }
    return RunTransformer(input_text, [indent_width](
        std::vector<std::string>& lines,
        transform::CursorState cursor,
        transform::SelectionState selection) {
      return transform::IndentLines(lines, cursor, selection, indent_width);
    });
  }

  if (definition.builtin_id == "builtin_outdent_lines") {
    std::size_t indent_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "indent_width", 1, 64, indent_width, error)) {
      return {false, {}, error};
    }
    return RunTransformer(input_text, [indent_width](
        std::vector<std::string>& lines,
        transform::CursorState cursor,
        transform::SelectionState selection) {
      return transform::OutdentLines(lines, cursor, selection, indent_width);
    });
  }

  if (definition.builtin_id == "builtin_convert_4_spaces_to_2") {
    return RunTransformer(input_text, [](std::vector<std::string>& lines,
                                         transform::CursorState cursor,
                                         transform::SelectionState selection) {
      return transform::Convert4To2Spaces(lines, cursor, selection);
    });
  }

  if (definition.builtin_id == "builtin_convert_2_spaces_to_4") {
    return RunTransformer(input_text, [](std::vector<std::string>& lines,
                                         transform::CursorState cursor,
                                         transform::SelectionState selection) {
      return transform::Convert2To4Spaces(lines, cursor, selection);
    });
  }

  if (definition.builtin_id == "builtin_toggle_case") {
    return RunTransformer(input_text, [](std::vector<std::string>& lines,
                                         transform::CursorState cursor,
                                         transform::SelectionState selection) {
      return transform::ToggleCase(lines, cursor, selection);
    });
  }

  if (definition.builtin_id == "builtin_change_case") {
    CaseMode mode = CaseMode::Toggle;
    std::string error;
    if (!ParseCaseMode(params, "mode", CaseMode::Toggle, mode, error)) {
      return {false, {}, error};
    }
    return ChangeCase(input_text, mode);
  }

  if (definition.builtin_id == "builtin_trim_trailing_spaces") {
    return TrimTrailingSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_ensure_final_newline") {
    return EnsureFinalNewline(input_text);
  }

  if (definition.builtin_id == "builtin_remove_final_extra_blank_lines") {
    return RemoveFinalExtraBlankLines(input_text);
  }

  if (definition.builtin_id == "builtin_remove_empty_lines") {
    return RemoveEmptyLines(input_text);
  }

  if (definition.builtin_id == "builtin_collapse_empty_lines") {
    std::size_t max_blank_lines = 1;
    std::string error;
    if (!ParsePositiveSize(params, "max_blank_lines", 0, 20, max_blank_lines, error)) {
      return {false, {}, error};
    }
    return CollapseEmptyLines(input_text, max_blank_lines);
  }

  if (definition.builtin_id == "builtin_tabs_to_spaces") {
    std::size_t tab_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "tab_width", 1, 16, tab_width, error)) {
      return {false, {}, error};
    }
    return ConvertTabsToSpaces(input_text, tab_width);
  }

  if (definition.builtin_id == "builtin_spaces_to_tabs") {
    std::size_t tab_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "tab_width", 1, 16, tab_width, error)) {
      return {false, {}, error};
    }
    return ConvertLeadingSpacesToTabs(input_text, tab_width);
  }

  if (definition.builtin_id == "builtin_sort_lines_az" ||
      definition.builtin_id == "builtin_sort_lines_za") {
    SortLanguage language = SortLanguage::Auto;
    bool case_sensitive = false;
    std::string error;
    if (!ParseSortLanguage(params, "language", SortLanguage::Auto, language, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "case_sensitive", false, case_sensitive, error)) {
      return {false, {}, error};
    }
    return SortLines(
        input_text,
        definition.builtin_id == "builtin_sort_lines_za",
        case_sensitive,
        language);
  }

  if (definition.builtin_id == "builtin_unique_lines") {
    SortLanguage language = SortLanguage::Auto;
    bool case_sensitive = true;
    bool trim_before_compare = false;
    std::string error;
    if (!ParseSortLanguage(params, "language", SortLanguage::Auto, language, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "case_sensitive", true, case_sensitive, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "trim_before_compare", false, trim_before_compare, error)) {
      return {false, {}, error};
    }
    return UniqueLines(input_text, case_sensitive, trim_before_compare, language);
  }

  if (definition.builtin_id == "builtin_add_line_prefix") {
    bool only_non_empty = true;
    std::string error;
    if (!ParseBoolParam(params, "only_non_empty", true, only_non_empty, error)) {
      return {false, {}, error};
    }
    return AddLinePrefix(input_text, GetParam(params, "prefix", "// "), only_non_empty);
  }

  if (definition.builtin_id == "builtin_remove_line_prefix") {
    return RemoveLinePrefix(input_text, GetParam(params, "prefix", "// "));
  }

  if (definition.builtin_id == "builtin_text_cleanup_trim_lines") {
    return TrimLines(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_trim_document") {
    return TrimDocument(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_normalize_spaces") {
    return NormalizeSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_duplicate_spaces") {
    return RemoveDuplicateSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_empty_lines") {
    return RemoveEmptyLines(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_collapse_empty_lines") {
    std::size_t max_blank_lines = 1;
    std::string error;
    if (!ParsePositiveSize(params, "max_blank_lines", 0, 20, max_blank_lines, error)) {
      return {false, {}, error};
    }
    return CollapseEmptyLines(input_text, max_blank_lines);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_invisible_chars") {
    return RemoveInvisibleCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_zero_width_chars") {
    return RemoveZeroWidthCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_replace_nbsp") {
    return ReplaceNonBreakingSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_soft_hyphens") {
    return RemoveSoftHyphens(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_bom") {
    return RemoveBom(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_control_chars") {
    return RemoveControlCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_normalize_line_endings") {
    std::string ending = ToLowerAscii(GetParam(params, "ending", "lf"));
    if (ending == "lf" || ending == "unix") {
      return NormalizeLineEndings(input_text, "\n");
    }
    if (ending == "crlf" || ending == "windows") {
      return NormalizeLineEndings(input_text, "\r\n");
    }
    return {false, {}, "Parameter ending must be lf or crlf."};
  }

  if (definition.builtin_id == "builtin_text_cleanup_normalize_tabs") {
    std::size_t tab_width = 4;
    std::string error;
    if (!ParsePositiveSize(params, "tab_width", 1, 16, tab_width, error)) {
      return {false, {}, error};
    }
    return ConvertTabsToSpaces(input_text, tab_width);
  }

  if (definition.builtin_id == "builtin_text_cleanup_remove_page_numbers") {
    std::size_t max_number_digits = 4;
    std::string error;
    if (!ParsePositiveSize(params, "max_number_digits", 1, 20, max_number_digits, error)) {
      return {false, {}, error};
    }
    return RemovePageNumbers(input_text, max_number_digits);
  }

  if (definition.builtin_id == "builtin_text_cleanup_fix_ocr_hyphenation") {
    return FixOcrHyphenation(input_text);
  }

  if (definition.builtin_id == "builtin_text_cleanup_join_broken_lines") {
    bool keep_paragraph_breaks = true;
    std::string error;
    if (!ParseBoolParam(params, "keep_paragraph_breaks", true, keep_paragraph_breaks, error)) {
      return {false, {}, error};
    }
    return JoinBrokenLines(input_text, keep_paragraph_breaks);
  }

  if (definition.builtin_id == "builtin_paragraph_trim") {
    return TrimParagraphs(input_text);
  }

  if (definition.builtin_id == "builtin_paragraph_normalize_spaces") {
    return NormalizeParagraphSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_paragraph_remove_duplicates") {
    return RemoveDuplicateParagraphs(input_text);
  }

  if (definition.builtin_id == "builtin_paragraph_collapse_gaps") {
    std::size_t max_blank_lines = 1;
    std::string error;
    if (!ParsePositiveSize(params, "max_blank_lines", 0, 20, max_blank_lines, error)) {
      return {false, {}, error};
    }
    return CollapseParagraphGaps(input_text, max_blank_lines);
  }

  if (definition.builtin_id == "builtin_paragraph_unwrap") {
    return UnwrapParagraphs(input_text);
  }

  if (definition.builtin_id == "builtin_paragraph_wrap") {
    std::size_t width = 80;
    std::string error;
    if (!ParsePositiveSize(params, "width", 20, 1000, width, error)) {
      return {false, {}, error};
    }
    return WrapParagraphs(input_text, width);
  }

  if (definition.builtin_id == "builtin_paragraph_split_sentences") {
    return SplitSentencesToLines(input_text);
  }

  if (definition.builtin_id == "builtin_paragraph_merge_short") {
    std::size_t min_chars = 160;
    std::string error;
    if (!ParsePositiveSize(params, "min_chars", 1, 10000, min_chars, error)) {
      return {false, {}, error};
    }
    return MergeShortParagraphs(input_text, min_chars);
  }

  if (definition.builtin_id == "builtin_analysis_text_statistics") {
    return AnalyzeTextStatistics(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_long_lines") {
    std::size_t max_line_length = 120;
    std::string error;
    if (!ParsePositiveSize(params, "max_line_length", 1, 100000, max_line_length, error)) {
      return {false, {}, error};
    }
    return AnalyzeLongLines(input_text, max_line_length);
  }

  if (definition.builtin_id == "builtin_analysis_duplicate_lines") {
    SortLanguage language = SortLanguage::Auto;
    bool case_sensitive = false;
    bool trim_before_compare = true;
    std::string error;
    if (!ParseSortLanguage(params, "language", SortLanguage::Auto, language, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "case_sensitive", false, case_sensitive, error)) {
      return {false, {}, error};
    }
    if (!ParseBoolParam(params, "trim_before_compare", true, trim_before_compare, error)) {
      return {false, {}, error};
    }
    return AnalyzeDuplicateLines(input_text, case_sensitive, trim_before_compare, language);
  }

  if (definition.builtin_id == "builtin_analysis_invisible_chars") {
    return AnalyzeInvisibleCharacters(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_line_endings") {
    return AnalyzeLineEndings(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_character_inventory") {
    return AnalyzeCharacterInventory(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_normalize_quotes") {
    return NormalizeQuotes(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_normalize_dashes") {
    return NormalizeDashes(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_normalize_ellipsis") {
    return NormalizeEllipsis(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_fix_spaces") {
    return FixPunctuationSpaces(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_normalize_ukrainian_apostrophe") {
    return NormalizeUkrainianApostrophe(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_normalize_dialogue_dashes") {
    return NormalizeDialogueDashes(input_text);
  }

  if (definition.builtin_id == "builtin_punctuation_normalize_ukrainian") {
    return NormalizeUkrainianPunctuation(input_text);
  }

  if (definition.builtin_id == "builtin_table_normalize_semicolon_rows" ||
      definition.builtin_id == "builtin_table_trim_cells") {
    return NormalizeSemicolonRows(input_text);
  }

  if (definition.builtin_id == "builtin_table_tabs_to_semicolon_rows") {
    return TabsToSemicolonRows(input_text);
  }

  if (definition.builtin_id == "builtin_table_multispace_to_semicolon_rows") {
    std::size_t min_spaces = 2;
    std::string error;
    if (!ParsePositiveSize(params, "min_spaces", 2, 32, min_spaces, error)) {
      return {false, {}, error};
    }
    return MultiSpaceColumnsToSemicolonRows(input_text, min_spaces);
  }

  if (definition.builtin_id == "builtin_table_remove_empty_rows") {
    return RemoveEmptyTableRows(input_text);
  }

  if (definition.builtin_id == "builtin_table_remove_duplicate_rows") {
    return RemoveDuplicateTableRows(input_text);
  }

  if (definition.builtin_id == "builtin_table_extract_column") {
    std::size_t column = 1;
    std::string error;
    if (!ParsePositiveSize(params, "column", 1, 10000, column, error)) {
      return {false, {}, error};
    }
    return ExtractTableColumn(input_text, column);
  }

  if (definition.builtin_id == "builtin_table_remove_column") {
    std::size_t column = 1;
    std::string error;
    if (!ParsePositiveSize(params, "column", 1, 10000, column, error)) {
      return {false, {}, error};
    }
    return RemoveTableColumn(input_text, column);
  }

  if (definition.builtin_id == "builtin_markdown_add_heading") {
    std::size_t level = 2;
    std::string error;
    if (!ParsePositiveSize(params, "level", 1, 6, level, error)) {
      return {false, {}, error};
    }
    return MarkdownAddHeading(input_text, level);
  }

  if (definition.builtin_id == "builtin_markdown_remove_headings") {
    return MarkdownRemoveHeadings(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_bullet_list") {
    return MarkdownBulletList(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_numbered_list") {
    return MarkdownNumberedList(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_renumber_list") {
    return MarkdownRenumberList(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_quote_block") {
    return MarkdownQuoteBlock(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_bold") {
    return MarkdownWrap(input_text, "**");
  }

  if (definition.builtin_id == "builtin_markdown_italic") {
    return MarkdownWrap(input_text, "*");
  }

  if (definition.builtin_id == "builtin_markdown_strip_formatting") {
    return MarkdownStripFormatting(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_extract_headings") {
    return MarkdownExtractHeadings(input_text);
  }

  if (definition.builtin_id == "builtin_markdown_generate_toc") {
    return MarkdownGenerateToc(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_mixed_cyrillic_latin") {
    return AnalyzeMixedCyrillicLatin(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_word_frequency") {
    std::size_t top_count = 30;
    std::size_t min_length = 2;
    std::string error;
    if (!ParsePositiveSize(params, "top_count", 1, 1000, top_count, error)) {
      return {false, {}, error};
    }
    if (!ParsePositiveSize(params, "min_length", 1, 1000, min_length, error)) {
      return {false, {}, error};
    }
    return AnalyzeWordFrequency(input_text, top_count, min_length);
  }

  if (definition.builtin_id == "builtin_analysis_long_sentences") {
    std::size_t max_sentence_length = 180;
    std::string error;
    if (!ParsePositiveSize(params, "max_sentence_length", 1, 100000, max_sentence_length, error)) {
      return {false, {}, error};
    }
    return AnalyzeLongSentences(input_text, max_sentence_length);
  }

  if (definition.builtin_id == "builtin_analysis_repeated_words") {
    return AnalyzeRepeatedWords(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_unbalanced_quotes") {
    return AnalyzeUnbalancedQuotes(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_unbalanced_brackets") {
    return AnalyzeUnbalancedBrackets(input_text);
  }

  if (definition.builtin_id == "builtin_analysis_suspicious_punctuation") {
    return AnalyzeSuspiciousPunctuation(input_text);
  }

  return {false, {}, "Unknown built-in text processor: " + definition.builtin_id};
}

}  // namespace textlt
