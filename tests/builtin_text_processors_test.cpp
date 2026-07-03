#include "builtin_text_processors.hpp"

#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const textlt::TextParserDefinition& FindProcessor(const std::string& id) {
  static const std::vector<textlt::TextParserDefinition> processors = textlt::CreateBuiltinTextProcessors();
  for (const auto& processor : processors) {
    if (processor.id == id || processor.builtin_id == id) {
      return processor;
    }
  }
  assert(false && "Missing built-in processor");
  return processors.front();
}

std::string Apply(
    const std::string& id,
    const std::string& input,
    const std::unordered_map<std::string, std::string>& params = {}) {
  const textlt::BuiltinTextProcessorResult result =
      textlt::ApplyBuiltinTextProcessor(FindProcessor(id), input, params);
  assert(result.success && result.error.c_str());
  return result.text;
}

void TestPunctuationPack() {
  assert(Apply("builtin_punctuation_normalize_ellipsis", "Так...") == "Так…");
  assert(Apply("builtin_punctuation_normalize_dashes", "а -- б") == "а — б");
  assert(Apply("builtin_punctuation_fix_spaces", "Привіт ,світе !") == "Привіт, світе!");
  assert(Apply("builtin_punctuation_normalize_ukrainian_apostrophe", "п'ять м`яч") == "п’ять м’яч");
  assert(Apply("builtin_punctuation_normalize_dialogue_dashes", "- Привіт\n– Так") == "— Привіт\n— Так");
}

void TestTablesPack() {
  assert(Apply("builtin_table_normalize_semicolon_rows", " A ;  B  ; C \nD;E; F") == "A; B; C\nD; E; F");
  assert(Apply("builtin_table_tabs_to_semicolon_rows", "A\tB\tC") == "A; B; C");
  assert(Apply("builtin_table_multispace_to_semicolon_rows", "A  B   C", {{"min_spaces", "2"}}) == "A; B; C");
  assert(Apply("builtin_table_extract_column", "A; B; C\nD; E; F", {{"column", "2"}}) == "B\nE");
  assert(Apply("builtin_table_remove_column", "A; B; C\nD; E; F", {{"column", "2"}}) == "A; C\nD; F");
  assert(Apply("builtin_table_remove_duplicate_rows", "A; B\n A ; B \nC; D") == "A; B\nC; D");
}

void TestMarkdownPack() {
  assert(Apply("builtin_markdown_add_heading", "Title", {{"level", "2"}}) == "## Title");
  assert(Apply("builtin_markdown_remove_headings", "## Title") == "Title");
  assert(Apply("builtin_markdown_bullet_list", "one\ntwo") == "- one\n- two");
  assert(Apply("builtin_markdown_numbered_list", "one\ntwo") == "1. one\n2. two");
  assert(Apply("builtin_markdown_renumber_list", "4. one\n9. two") == "1. one\n2. two");
  assert(Apply("builtin_markdown_quote_block", "one\ntwo") == "> one\n> two");
  assert(Apply("builtin_markdown_bold", "word") == "**word**");
  assert(Apply("builtin_markdown_italic", "word") == "*word*");
  assert(Apply("builtin_markdown_strip_formatting", "## **Title**\n- [Link](https://example.com)") == "Title\nLink");
  const std::string headings = Apply("builtin_markdown_extract_headings", "# A\nText\n## B");
  assert(headings.find("Line 1: H1 A") != std::string::npos);
  assert(headings.find("Line 3: H2 B") != std::string::npos);
  assert(Apply("builtin_markdown_generate_toc", "# A\nText\n## B") == "- A\n  - B\n");
}

void TestAnalysisPack2() {
  const std::string mixed = Apply("builtin_analysis_mixed_cyrillic_latin", "Тext Cloud Сloud");
  assert(mixed.find("Тext") != std::string::npos);
  assert(mixed.find("Сloud") != std::string::npos);

  const std::string frequency = Apply("builtin_analysis_word_frequency", "one two two три три три", {{"top_count", "2"}, {"min_length", "2"}});
  assert(frequency.find("три: 3") != std::string::npos);
  assert(frequency.find("two: 2") != std::string::npos);

  const std::string long_sentence = Apply("builtin_analysis_long_sentences", "Short. This sentence is very long indeed.", {{"max_sentence_length", "10"}});
  assert(long_sentence.find("Total: 1") != std::string::npos);

  const std::string repeated = Apply("builtin_analysis_repeated_words", "This is is repeated.");
  assert(repeated.find("is") != std::string::npos);

  const std::string quotes = Apply("builtin_analysis_unbalanced_quotes", "«one» «two");
  assert(quotes.find("unbalanced") != std::string::npos);

  const std::string brackets = Apply("builtin_analysis_unbalanced_brackets", "one (two]");
  assert(brackets.find("Total issues:") != std::string::npos);

  const std::string suspicious = Apply("builtin_analysis_suspicious_punctuation", "Hi ,there!!");
  assert(suspicious.find("space before punctuation") != std::string::npos);
  assert(suspicious.find("repeated punctuation") != std::string::npos);
}

}  // namespace

int main() {
  TestPunctuationPack();
  TestTablesPack();
  TestMarkdownPack();
  TestAnalysisPack2();
  return 0;
}
