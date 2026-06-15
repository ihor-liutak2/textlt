cmake_minimum_required(VERSION 3.14)
project(textlt VERSION 0.1.0 LANGUAGES CXX)

# Use the C++17 language standard.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch lightweight TUI dependencies.
include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
  PATCH_COMMAND
    ${CMAKE_COMMAND}
      -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-v5-shortcuts.patch
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
)
FetchContent_MakeAvailable(ftxui)

add_executable(textlt
  src/app.cpp
  src/app_view.cpp
  src/config_manager.cpp
  src/editor_config.cpp
  src/editor_component.cpp
  src/editor_keyboard.cpp
  src/editor_mouse.cpp
  src/editor_utils.cpp
  src/editor_viewport.cpp
  src/file_dialog.cpp
  src/file_utils.cpp
  src/git_manager.cpp
  src/help_dialog.cpp
  src/history_manager.cpp
  src/lexers/c_family_lexer.cpp
  src/lexers/config_script_lexer.cpp
  src/lexers/database_lexer.cpp
  src/lexers/web_lexer.cpp
  src/main.cpp
  src/sidebar_component.cpp
  src/syntax_highlighter.cpp
  src/terminal_flow_control_guard.cpp
  src/text_transformer.cpp
  src/theme_dialog.cpp
  src/theme.cpp
)

target_link_libraries(textlt PRIVATE ftxui::screen ftxui::dom ftxui::component)
