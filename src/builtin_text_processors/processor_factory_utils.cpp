std::string TrimTrailingSpacesFromLine(std::string line) {
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  return line;
}


std::string TrimLeadingSpacesFromLine(std::string line) {
  std::size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    line.erase(0, start);
  }
  return line;
}

std::string TrimSpacesFromLine(std::string line) {
  line = TrimTrailingSpacesFromLine(std::move(line));
  return TrimLeadingSpacesFromLine(std::move(line));
}

bool IsHorizontalSpaceCodepoint(std::uint32_t codepoint) {
  return codepoint == ' ' || codepoint == '\t' || codepoint == 0x00A0;
}

bool IsZeroWidthCodepoint(std::uint32_t codepoint) {
  return codepoint == 0xFEFF || codepoint == 0x200B || codepoint == 0x200C ||
      codepoint == 0x200D || codepoint == 0x2060;
}

bool IsAsciiControlCodepoint(std::uint32_t codepoint) {
  return (codepoint < 0x20 || codepoint == 0x7F) &&
      codepoint != '\n' && codepoint != '\r' && codepoint != '\t';
}

bool IsLowercaseLetterForJoin(std::uint32_t codepoint) {
  return IsAsciiLower(codepoint) || IsCyrillicLower(codepoint);
}

bool StartsWithLowercaseLetter(const std::string& line) {
  for (std::uint32_t codepoint : DecodeUtf8(line)) {
    if (IsHorizontalSpaceCodepoint(codepoint)) {
      continue;
    }
    return IsLowercaseLetterForJoin(codepoint);
  }
  return false;
}

BuiltinTextProcessorResult SuccessResult(std::string text) {
  BuiltinTextProcessorResult result;
  result.success = true;
  result.text = std::move(text);
  return result;
}

BuiltinTextProcessorResult ReportResult(std::string report) {
  BuiltinTextProcessorResult result;
  result.success = true;
  result.text = std::move(report);
  return result;
}

transform::CursorState FullTextCursor(const std::vector<std::string>& lines) {
  if (lines.empty()) {
    return {};
  }

  return {lines.back().size(), lines.size() - 1};
}

transform::SelectionState FullTextSelection(const std::vector<std::string>& lines) {
  transform::SelectionState selection;
  selection.active = !lines.empty();
  selection.anchor_x = 0;
  selection.anchor_y = 0;
  return selection;
}

TextParserParam IntegerParam(
    std::string id,
    std::string label,
    std::string default_value,
    std::string description) {
  TextParserParam param;
  param.id = std::move(id);
  param.label = std::move(label);
  param.type = "integer";
  param.default_value = std::move(default_value);
  param.description = std::move(description);
  return param;
}

TextParserParam BooleanParam(
    std::string id,
    std::string label,
    std::string default_value,
    std::string description) {
  TextParserParam param;
  param.id = std::move(id);
  param.label = std::move(label);
  param.type = "boolean";
  param.default_value = std::move(default_value);
  param.description = std::move(description);
  return param;
}

TextParserParam TextParam(
    std::string id,
    std::string label,
    std::string default_value,
    std::string description) {
  TextParserParam param;
  param.id = std::move(id);
  param.label = std::move(label);
  param.type = "text";
  param.default_value = std::move(default_value);
  param.description = std::move(description);
  return param;
}

TextParserDefinition MakeBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    TextParserScope scope,
    std::string group,
    TextParserOutput output,
    std::vector<TextParserParam> params = {}) {
  TextParserDefinition definition;
  definition.id = std::move(id);
  definition.name = std::move(name);
  definition.scope = scope;
  definition.description = std::move(description);
  definition.group = std::move(group);
  definition.engine = TextParserEngine::Builtin;
  definition.output = output;
  definition.builtin_id = definition.id;
  definition.locked = true;
  definition.repeat_default = 1;
  definition.params = std::move(params);
  return definition;
}

TextParserDefinition BuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Code,
      "Code",
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition GroupedBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::string group,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Code,
      std::move(group),
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition TextBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::string group,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Text,
      std::move(group),
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition ParagraphBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::string group,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Paragraph,
      std::move(group),
      TextParserOutput::ReplaceText,
      std::move(params));
}

TextParserDefinition AnalysisDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Text,
      "Analysis",
      TextParserOutput::Report,
      std::move(params));
}

TextParserDefinition ReportBuiltinDefinition(
    std::string id,
    std::string name,
    std::string description,
    std::string group,
    std::vector<TextParserParam> params = {}) {
  return MakeBuiltinDefinition(
      std::move(id),
      std::move(name),
      std::move(description),
      TextParserScope::Text,
      std::move(group),
      TextParserOutput::Report,
      std::move(params));
}

BuiltinTextProcessorResult RunTransformer(
    const std::string& input_text,
    const std::function<transform::TransformResult(
        std::vector<std::string>&,
        transform::CursorState,
        transform::SelectionState)>& callback) {
  BuiltinTextProcessorResult result;
  std::vector<std::string> lines = SplitLines(input_text);
  const bool had_final_newline = !input_text.empty() && input_text.back() == '\n';
  const transform::CursorState cursor = FullTextCursor(lines);
  const transform::SelectionState selection = FullTextSelection(lines);
  const transform::TransformResult transform_result = callback(lines, cursor, selection);

  result.success = true;
  if (!transform_result.changed) {
    result.text = input_text;
    return result;
  }

  result.text = JoinLines(lines);
  if (had_final_newline) {
    result.text += '\n';
  }
  return result;
}
