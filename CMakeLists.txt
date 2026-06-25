cmake_minimum_required(VERSION 3.14)
project(textlt VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

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

# Download support.
find_package(CURL QUIET)

if(CURL_FOUND)
  message(STATUS "TextLT: libcurl found")
  message(STATUS "TextLT: libcurl version: ${CURL_VERSION_STRING}")
else()
  message(FATAL_ERROR
    "TextLT: libcurl development files were not found.\n"
    "Install on Debian/Ubuntu/MX Linux:\n"
    "  sudo apt install libcurl4-openssl-dev\n"
  )
endif()

# Archive extraction support.
find_package(LibArchive QUIET)

if(LibArchive_FOUND)
  message(STATUS "TextLT: libarchive found")
  message(STATUS "TextLT: libarchive version: ${LibArchive_VERSION}")
else()
  message(FATAL_ERROR
    "TextLT: libarchive development files were not found.\n"
    "Install on Debian/Ubuntu/MX Linux:\n"
    "  sudo apt install libarchive-dev\n"
  )
endif()

# Thread support.
find_package(Threads QUIET)

if(Threads_FOUND)
  message(STATUS "TextLT: Threads support found")
else()
  message(FATAL_ERROR
    "TextLT: thread support was not found.\n"
    "Install compiler/build tools on Debian/Ubuntu/MX Linux:\n"
    "  sudo apt install build-essential\n"
  )
endif()

add_executable(textlt
  src/app.cpp
  src/app_find_replace.cpp
  src/app_modals.cpp
  src/app_menu_actions.cpp
  src/app_file_actions.cpp
  src/app_document_actions.cpp
  src/app_tts_actions.cpp
  src/app_resources.cpp
  src/app_view.cpp
  src/cloud_tts_pipeline.cpp
  src/config_manager.cpp
  src/editor_config.cpp
  src/editor_component.cpp
  src/editor_events.cpp
  src/editor_input_controller.cpp
  src/editor_mouse.cpp
  src/editor_utils.cpp
  src/editor_viewport.cpp
  src/file_utils.cpp
  src/git_manager.cpp
  src/history_manager.cpp
  src/lexers/c_family_lexer.cpp
  src/lexers/config_script_lexer.cpp
  src/lexers/database_lexer.cpp
  src/lexers/web_lexer.cpp
  src/main.cpp
  src/menu_bar.cpp
  src/modals/file_dialog.cpp
  src/modals/assistant_modals.cpp
  src/modals/assistant_modals_ai.cpp
  src/modals/assistant_modals_tts.cpp
  src/modals/help_dialog.cpp
  src/modals/modal_ai.cpp
  src/modals/modal_git.cpp
  src/modals/modal_recent_files.cpp
  src/modals/modal_tts.cpp
  src/modals/modal_window.cpp
  src/modals/path_operation_dialog.cpp
  src/modals/theme_dialog.cpp
  src/modals/unsaved_changes_dialog.cpp
  src/modals/modal_search_files.cpp
  src/opened_config.cpp
  src/recent_files.cpp
  src/sidebar_component.cpp
  src/syntax_highlighter.cpp
  src/terminal_flow_control_guard.cpp
  src/text_transformer.cpp
  src/theme.cpp
  src/document.hpp
  src/document.cpp
  src/document_content.cpp
  src/document_cursor.cpp
  src/document_editing.cpp
  src/document_history.cpp
  src/document_selection.cpp
  src/document_transform.cpp
  src/file_manager.cpp
  src/search_in_files.cpp
)

target_include_directories(textlt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${CURL_INCLUDE_DIRS}
  ${LibArchive_INCLUDE_DIRS}
)

target_link_libraries(textlt PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  CURL::libcurl
  ${LibArchive_LIBRARIES}
  Threads::Threads
)

add_executable(textlt_document_navigation_test
  tests/document_navigation_test.cpp
  src/document_cursor.cpp
  src/document_history.cpp
  src/document_selection.cpp
  src/editor_utils.cpp
  src/history_manager.cpp
)

target_include_directories(textlt_document_navigation_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME textlt_document_navigation_test COMMAND textlt_document_navigation_test)
