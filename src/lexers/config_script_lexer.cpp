#include "config_script_lexer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "lexer_common.hpp"

namespace textlt::lexers {

#include "config_script/common_script_utils.cpp"
#include "config_script/shell_lexer.cpp"
#include "config_script/dynamic_languages_lexer.cpp"
#include "config_script/config_formats_lexer.cpp"
#include "config_script/science_modern_legacy_lexer.cpp"

} // namespace textlt::lexers
