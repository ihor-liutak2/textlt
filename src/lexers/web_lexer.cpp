#include "web_lexer.hpp"

#include <algorithm>
#include <unordered_set>

#include "c_family_lexer.hpp"
#include "lexer_common.hpp"

namespace textlt::lexers {

#include "web/common_web_utils.cpp"
#include "web/html_xml_lexer.cpp"
#include "web/css_family_lexer.cpp"
#include "web/sfc_lexer.cpp"
#include "web/php_blade_lexer.cpp"

} // namespace textlt::lexers
