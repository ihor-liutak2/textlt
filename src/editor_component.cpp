#include "editor_component.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "editor_utils.hpp"
#include "syntax_highlighter.hpp"
#include "text_transformer.hpp"

namespace textlt {
#include "editor_component/common_editor_utils.cpp"
#include "editor_component/document_state.cpp"
#include "editor_component/editing_actions.cpp"
#include "editor_component/search_replace.cpp"
#include "editor_component/bracket_selection_helpers.cpp"
#include "editor_component/cursor_navigation.cpp"
#include "editor_component/history_line_actions.cpp"

} // namespace textlt
