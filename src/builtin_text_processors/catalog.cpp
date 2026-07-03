std::vector<TextParserDefinition> CreateBuiltinTextProcessors() {
  return {
      GroupedBuiltinDefinition(
          "builtin_indent_lines",
          "Indent lines",
          "Adds spaces to the beginning of each selected line.",
          "Code",
          {IntegerParam(
              "indent_width",
              "Indent width",
              "4",
              "Number of spaces to insert at the beginning of each line.")}),
      GroupedBuiltinDefinition(
          "builtin_outdent_lines",
          "Outdent lines",
          "Removes one indentation level from each selected line.",
          "Code",
          {IntegerParam(
              "indent_width",
              "Indent width",
              "4",
              "Maximum number of spaces to remove from the beginning of each line.")}),
      GroupedBuiltinDefinition(
          "builtin_convert_4_spaces_to_2",
          "Convert 4 spaces to 2",
          "Converts leading indentation blocks of four spaces into two spaces.",
          "Code"),
      GroupedBuiltinDefinition(
          "builtin_convert_2_spaces_to_4",
          "Convert 2 spaces to 4",
          "Converts leading indentation blocks of two spaces into four spaces.",
          "Code"),
      GroupedBuiltinDefinition(
          "builtin_toggle_case",
          "Toggle case",
          "Toggles letter case in the selected text. Supports Ukrainian and Russian letters.",
          "Case"),
      GroupedBuiltinDefinition(
          "builtin_change_case",
          "Change case",
          "Changes selected text case. Identifier modes are applied per line.",
          "Case",
          {TextParam(
              "mode",
              "Mode",
              "toggle",
              "Case mode: toggle, upper, lower, title, sentence, snake, camel, pascal, or kebab.")}),
      GroupedBuiltinDefinition(
          "builtin_trim_trailing_spaces",
          "Trim trailing spaces",
          "Removes spaces and tabs from the end of each line.",
          "Cleanup"),
      GroupedBuiltinDefinition(
          "builtin_ensure_final_newline",
          "Ensure final newline",
          "Adds one newline at the end of non-empty text if it is missing.",
          "Cleanup"),
      GroupedBuiltinDefinition(
          "builtin_remove_final_extra_blank_lines",
          "Remove final extra blank lines",
          "Removes blank lines after the last non-blank line.",
          "Cleanup"),
      GroupedBuiltinDefinition(
          "builtin_remove_empty_lines",
          "Remove empty lines",
          "Removes all empty or whitespace-only lines.",
          "Lines"),
      GroupedBuiltinDefinition(
          "builtin_collapse_empty_lines",
          "Collapse empty lines",
          "Limits consecutive empty lines to the selected count.",
          "Lines",
          {IntegerParam(
              "max_blank_lines",
              "Max blank lines",
              "1",
              "Maximum number of consecutive empty lines to keep.")}),
      GroupedBuiltinDefinition(
          "builtin_tabs_to_spaces",
          "Tabs to spaces",
          "Replaces tab characters with spaces using the selected tab width.",
          "Code",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of columns represented by one tab.")}),
      GroupedBuiltinDefinition(
          "builtin_spaces_to_tabs",
          "Leading spaces to tabs",
          "Converts leading indentation spaces to tabs using the selected tab width.",
          "Code",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of leading spaces represented by one tab.")}),
      GroupedBuiltinDefinition(
          "builtin_sort_lines_az",
          "Sort lines A-Z",
          "Sorts lines in ascending order.",
          "Lines",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Sorting language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "false",
               "Use case-sensitive comparison when sorting lines.")}),
      GroupedBuiltinDefinition(
          "builtin_sort_lines_za",
          "Sort lines Z-A",
          "Sorts lines in descending order.",
          "Lines",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Sorting language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "false",
               "Use case-sensitive comparison when sorting lines.")}),
      GroupedBuiltinDefinition(
          "builtin_unique_lines",
          "Unique lines",
          "Keeps the first occurrence of each line and removes duplicates.",
          "Lines",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Comparison language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "true",
               "Use case-sensitive comparison when detecting duplicates."),
           BooleanParam(
               "trim_before_compare",
               "Trim compare",
               "false",
               "Ignore leading and trailing spaces when comparing lines.")}),
      GroupedBuiltinDefinition(
          "builtin_add_line_prefix",
          "Add line prefix",
          "Adds selected prefix to every line.",
          "Lines",
          {TextParam(
               "prefix",
               "Prefix",
               "// ",
               "Text to insert at the beginning of each line."),
           BooleanParam(
               "only_non_empty",
               "Only non-empty",
               "true",
               "Skip empty or whitespace-only lines.")}),
      GroupedBuiltinDefinition(
          "builtin_remove_line_prefix",
          "Remove line prefix",
          "Removes selected prefix from the beginning of lines where it exists.",
          "Lines",
          {TextParam(
              "prefix",
              "Prefix",
              "// ",
              "Text to remove from the beginning of each line.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_trim_lines",
          "Trim lines",
          "Removes leading and trailing spaces or tabs from every line.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_trim_document",
          "Trim document",
          "Removes blank lines and outer spaces at the beginning and end of the document.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_normalize_spaces",
          "Normalize spaces",
          "Replaces tabs and non-breaking spaces with regular spaces, collapses repeated spaces, and trims lines.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_duplicate_spaces",
          "Remove duplicate spaces",
          "Collapses repeated regular spaces inside each line.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_empty_lines",
          "Remove empty lines",
          "Removes all empty or whitespace-only lines from text.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_collapse_empty_lines",
          "Collapse empty lines",
          "Limits consecutive empty lines to the selected count.",
          "Cleanup",
          {IntegerParam(
              "max_blank_lines",
              "Max blank lines",
              "1",
              "Maximum number of consecutive empty lines to keep.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_invisible_chars",
          "Remove invisible characters",
          "Removes zero-width characters, soft hyphens, BOM, and control characters. Non-breaking spaces become spaces.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_zero_width_chars",
          "Remove zero-width characters",
          "Removes BOM, zero-width spaces, zero-width joiners, and word joiners.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_replace_nbsp",
          "Replace non-breaking spaces",
          "Replaces non-breaking spaces with regular spaces.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_soft_hyphens",
          "Remove soft hyphens",
          "Removes invisible soft hyphen characters.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_bom",
          "Remove BOM",
          "Removes UTF-8 BOM / zero-width no-break space characters.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_control_chars",
          "Remove control characters",
          "Removes ASCII control characters except tabs and line breaks.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_normalize_line_endings",
          "Normalize line endings",
          "Converts line endings to LF or CRLF.",
          "Cleanup",
          {TextParam(
              "ending",
              "Ending",
              "lf",
              "Target line ending: lf or crlf.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_normalize_tabs",
          "Normalize tabs",
          "Replaces tab characters with spaces using the selected tab width.",
          "Cleanup",
          {IntegerParam(
              "tab_width",
              "Tab width",
              "4",
              "Number of columns represented by one tab.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_remove_page_numbers",
          "Remove page numbers",
          "Removes lines that contain only short page numbers.",
          "Cleanup",
          {IntegerParam(
              "max_number_digits",
              "Max number digits",
              "4",
              "Only digit-only lines up to this length are removed.")}),
      TextBuiltinDefinition(
          "builtin_text_cleanup_fix_ocr_hyphenation",
          "Fix OCR hyphenation",
          "Joins words split across lines by a hyphen or soft hyphen.",
          "Cleanup"),
      TextBuiltinDefinition(
          "builtin_text_cleanup_join_broken_lines",
          "Join broken lines",
          "Joins wrapped lines inside paragraphs into normal paragraph lines.",
          "Cleanup",
          {BooleanParam(
              "keep_paragraph_breaks",
              "Keep paragraph breaks",
              "true",
              "Keep blank lines between paragraphs.")}),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_trim",
          "Trim paragraphs",
          "Trims leading and trailing spaces inside paragraph lines and normalizes paragraph separators.",
          "Cleanup"),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_normalize_spaces",
          "Normalize paragraph spaces",
          "Collapses spaces inside paragraphs and turns each paragraph into one clean line.",
          "Cleanup"),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_remove_duplicates",
          "Remove duplicate paragraphs",
          "Keeps the first occurrence of each repeated paragraph.",
          "Cleanup"),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_collapse_gaps",
          "Collapse paragraph gaps",
          "Limits blank lines between paragraphs.",
          "Cleanup",
          {IntegerParam(
              "max_blank_lines",
              "Max blank lines",
              "1",
              "Maximum number of blank lines between paragraphs.")}),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_unwrap",
          "Unwrap paragraphs",
          "Joins wrapped paragraph lines into one line per paragraph.",
          "Paragraphs"),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_wrap",
          "Wrap paragraphs",
          "Wraps paragraphs to the selected line width.",
          "Paragraphs",
          {IntegerParam(
              "width",
              "Width",
              "80",
              "Maximum line width in characters.")}),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_split_sentences",
          "Split sentences to lines",
          "Puts each sentence on a separate line inside paragraphs.",
          "Paragraphs"),
      ParagraphBuiltinDefinition(
          "builtin_paragraph_merge_short",
          "Merge short paragraphs",
          "Merges short paragraphs until they reach the selected minimum length.",
          "Paragraphs",
          {IntegerParam(
              "min_chars",
              "Min characters",
              "160",
              "Merged paragraph target length in characters.")}),
      TextBuiltinDefinition(
          "builtin_punctuation_normalize_quotes",
          "Normalize quotes",
          "Converts straight and curly double quotes to Ukrainian guillemets.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_punctuation_normalize_dashes",
          "Normalize dashes",
          "Converts double hyphens and en dashes to em dashes.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_punctuation_normalize_ellipsis",
          "Normalize ellipsis",
          "Converts three dots to the ellipsis character.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_punctuation_fix_spaces",
          "Fix punctuation spaces",
          "Removes spaces before punctuation, normalizes spaces after punctuation, and spaces em dashes.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_punctuation_normalize_ukrainian_apostrophe",
          "Normalize Ukrainian apostrophe",
          "Normalizes apostrophe-like characters between Cyrillic letters to the typographic apostrophe.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_punctuation_normalize_dialogue_dashes",
          "Normalize dialogue dashes",
          "Turns leading hyphens or en dashes in dialogue lines into em dashes.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_punctuation_normalize_ukrainian",
          "Normalize Ukrainian punctuation",
          "Runs safe quote, dash, ellipsis, apostrophe, dialogue dash, and spacing normalization.",
          "Punctuation"),
      TextBuiltinDefinition(
          "builtin_table_normalize_semicolon_rows",
          "Normalize semicolon rows",
          "Trims cells and formats semicolon-separated table rows as cell; cell; cell.",
          "Tables"),
      TextBuiltinDefinition(
          "builtin_table_trim_cells",
          "Trim table cells",
          "Trims cells in semicolon-separated table rows.",
          "Tables"),
      TextBuiltinDefinition(
          "builtin_table_tabs_to_semicolon_rows",
          "Tabs to semicolon rows",
          "Converts tab-separated rows to semicolon-separated table rows.",
          "Tables"),
      TextBuiltinDefinition(
          "builtin_table_multispace_to_semicolon_rows",
          "Multi-space columns to semicolon rows",
          "Converts columns separated by repeated spaces to semicolon-separated rows.",
          "Tables",
          {IntegerParam(
              "min_spaces",
              "Min spaces",
              "2",
              "Minimum consecutive spaces treated as a column separator.")}),
      TextBuiltinDefinition(
          "builtin_table_remove_empty_rows",
          "Remove empty table rows",
          "Removes semicolon-separated rows where every cell is empty.",
          "Tables"),
      TextBuiltinDefinition(
          "builtin_table_remove_duplicate_rows",
          "Remove duplicate table rows",
          "Normalizes semicolon rows and removes duplicate table rows.",
          "Tables"),
      TextBuiltinDefinition(
          "builtin_table_extract_column",
          "Extract table column",
          "Extracts a one-based column from semicolon-separated table rows.",
          "Tables",
          {IntegerParam(
              "column",
              "Column",
              "1",
              "One-based column number to extract.")}),
      TextBuiltinDefinition(
          "builtin_table_remove_column",
          "Remove table column",
          "Removes a one-based column from semicolon-separated table rows.",
          "Tables",
          {IntegerParam(
              "column",
              "Column",
              "1",
              "One-based column number to remove.")}),
      TextBuiltinDefinition(
          "builtin_markdown_add_heading",
          "Add Markdown heading",
          "Adds a Markdown heading marker to every non-empty line.",
          "Markdown",
          {IntegerParam(
              "level",
              "Level",
              "2",
              "Heading level from 1 to 6.")}),
      TextBuiltinDefinition(
          "builtin_markdown_remove_headings",
          "Remove Markdown headings",
          "Removes Markdown heading markers from selected lines.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_bullet_list",
          "Convert to bullet list",
          "Converts non-empty lines to a Markdown bullet list.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_numbered_list",
          "Convert to numbered list",
          "Converts non-empty lines to a Markdown numbered list.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_renumber_list",
          "Renumber numbered list",
          "Renumbers existing Markdown numbered list items.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_quote_block",
          "Quote block",
          "Converts non-empty lines to a Markdown quote block.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_bold",
          "Bold selection",
          "Wraps selected text in Markdown bold markers.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_italic",
          "Italic selection",
          "Wraps selected text in Markdown italic markers.",
          "Markdown"),
      TextBuiltinDefinition(
          "builtin_markdown_strip_formatting",
          "Strip Markdown formatting",
          "Removes common Markdown headings, list markers, quotes, links, and inline formatting.",
          "Markdown"),
      ReportBuiltinDefinition(
          "builtin_markdown_extract_headings",
          "Extract Markdown headings",
          "Reports Markdown headings and line numbers.",
          "Markdown"),
      ReportBuiltinDefinition(
          "builtin_markdown_generate_toc",
          "Generate Markdown TOC",
          "Builds a plain Markdown table of contents from headings.",
          "Markdown"),
      AnalysisDefinition(
          "builtin_analysis_text_statistics",
          "Text statistics",
          "Counts bytes, characters, words, lines, non-empty lines, and paragraphs."),
      AnalysisDefinition(
          "builtin_analysis_long_lines",
          "Find long lines",
          "Reports lines longer than the selected maximum length.",
          {IntegerParam(
              "max_line_length",
              "Max line length",
              "120",
              "Lines longer than this value will be reported.")}),
      AnalysisDefinition(
          "builtin_analysis_duplicate_lines",
          "Find duplicate lines",
          "Reports duplicate lines and their line numbers.",
          {TextParam(
               "language",
               "Language",
               "auto",
               "Comparison language: auto, english, russian, ukrainian, or binary."),
           BooleanParam(
               "case_sensitive",
               "Case sensitive",
               "false",
               "Use case-sensitive comparison when detecting duplicate lines."),
           BooleanParam(
               "trim_before_compare",
               "Trim compare",
               "true",
               "Ignore leading and trailing spaces when comparing lines.")}),
      AnalysisDefinition(
          "builtin_analysis_invisible_chars",
          "Find invisible characters",
          "Reports tabs, non-breaking spaces, soft hyphens, zero-width characters, and control characters."),
      AnalysisDefinition(
          "builtin_analysis_line_endings",
          "Check line endings",
          "Reports CRLF, LF, CR counts and whether line endings are mixed."),
      AnalysisDefinition(
          "builtin_analysis_character_inventory",
          "Non-ASCII inventory",
          "Reports non-ASCII characters and their counts."),
      AnalysisDefinition(
          "builtin_analysis_mixed_cyrillic_latin",
          "Mixed Cyrillic/Latin report",
          "Reports words that contain both Cyrillic and Latin letters."),
      AnalysisDefinition(
          "builtin_analysis_word_frequency",
          "Word frequency",
          "Reports the most frequent words.",
          {IntegerParam(
               "top_count",
               "Top count",
               "30",
               "Maximum number of words to show."),
           IntegerParam(
               "min_length",
               "Min length",
               "2",
               "Minimum word length in characters.")}),
      AnalysisDefinition(
          "builtin_analysis_long_sentences",
          "Long sentence report",
          "Reports sentences longer than the selected maximum length.",
          {IntegerParam(
              "max_sentence_length",
              "Max sentence length",
              "180",
              "Sentences longer than this value will be reported.")}),
      AnalysisDefinition(
          "builtin_analysis_repeated_words",
          "Repeated words report",
          "Reports adjacent repeated words."),
      AnalysisDefinition(
          "builtin_analysis_unbalanced_quotes",
          "Unbalanced quotes report",
          "Reports likely unbalanced quote characters."),
      AnalysisDefinition(
          "builtin_analysis_unbalanced_brackets",
          "Unbalanced brackets report",
          "Reports likely unbalanced brackets."),
      AnalysisDefinition(
          "builtin_analysis_suspicious_punctuation",
          "Suspicious punctuation report",
          "Reports repeated punctuation, spaces before punctuation, and missing spaces after punctuation."),
  };
}
