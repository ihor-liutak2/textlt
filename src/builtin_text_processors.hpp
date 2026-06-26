#pragma once

#include "text_parser_manager.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace textlt {

struct BuiltinTextProcessorResult {
  bool success = false;
  std::string text;
  std::string error;
};

std::vector<TextParserDefinition> CreateBuiltinTextProcessors();
bool IsBuiltinTextProcessor(const std::string& processor_id);
BuiltinTextProcessorResult ApplyBuiltinTextProcessor(
    const TextParserDefinition& definition,
    const std::string& input_text,
    const std::unordered_map<std::string, std::string>& params);

}  // namespace textlt
