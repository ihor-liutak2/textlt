#include "c_family_lexer.hpp"

#include <unordered_set>

#include "lexer_common.hpp"

namespace textlt::lexers {

#include "c_family/keyword_tables.cpp"
#include "c_family/standalone_system_lexers.cpp"
#include "c_family/js_ts_json_utils.cpp"
#include "c_family/jsx_lexer.cpp"
#include "c_family/c_like_dispatch.cpp"

} // namespace textlt::lexers
