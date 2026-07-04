#include "builtin_text_processors.hpp"

#include "text_transformer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace textlt {
namespace {

enum class SortLanguage {
  Auto,
  Binary,
  English,
  Russian,
  Ukrainian,
};

enum class CaseMode {
  Toggle,
  Upper,
  Lower,
  Title,
  Sentence,
  Snake,
  Camel,
  Pascal,
  Kebab,
};

struct SortToken {
  int group = 0;
  int weight = 0;
  int case_weight = 0;
  std::uint32_t codepoint = 0;
};

std::string ToLowerAscii(std::string value);


#include "builtin_text_processors/common_text_utils.cpp"
#include "builtin_text_processors/unicode_sort_utils.cpp"
#include "builtin_text_processors/processor_factory_utils.cpp"
#include "builtin_text_processors/line_processors.cpp"
#include "builtin_text_processors/case_processors.cpp"
#include "builtin_text_processors/cleanup_processors.cpp"
#include "builtin_text_processors/paragraph_processors.cpp"
#include "builtin_text_processors/analysis_processors.cpp"
#include "builtin_text_processors/shared_processor_text_utils.cpp"

#include "builtin_text_processors/punctuation_processors.cpp"
#include "builtin_text_processors/table_processors.cpp"
#include "builtin_text_processors/markdown_processors.cpp"
#include "builtin_text_processors/analysis_extra_processors.cpp"
#include "builtin_text_processors/catalog.cpp"
#include "builtin_text_processors/dispatch.cpp"
