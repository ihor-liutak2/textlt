cmake_minimum_required(VERSION 3.14)
project(textlt VERSION 0.1.0 LANGUAGES C CXX)

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

FetchContent_Declare(pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.16
)
FetchContent_MakeAvailable(pugixml)

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

# Portable Lua runtime for configurable text processors.
# Keep Lua embedded in the build so users do not need system Lua dev packages.
FetchContent_Declare(lua54
  URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
  URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
)

FetchContent_GetProperties(lua54)
if(NOT lua54_POPULATED)
  FetchContent_Populate(lua54)
endif()

set(LUA54_SRC_DIR ${lua54_SOURCE_DIR}/src)

add_library(lua54 STATIC
  ${LUA54_SRC_DIR}/lapi.c
  ${LUA54_SRC_DIR}/lauxlib.c
  ${LUA54_SRC_DIR}/lbaselib.c
  ${LUA54_SRC_DIR}/lcode.c
  ${LUA54_SRC_DIR}/lctype.c
  ${LUA54_SRC_DIR}/ldebug.c
  ${LUA54_SRC_DIR}/ldo.c
  ${LUA54_SRC_DIR}/ldump.c
  ${LUA54_SRC_DIR}/lfunc.c
  ${LUA54_SRC_DIR}/lgc.c
  ${LUA54_SRC_DIR}/llex.c
  ${LUA54_SRC_DIR}/lmathlib.c
  ${LUA54_SRC_DIR}/lmem.c
  ${LUA54_SRC_DIR}/lobject.c
  ${LUA54_SRC_DIR}/lopcodes.c
  ${LUA54_SRC_DIR}/lparser.c
  ${LUA54_SRC_DIR}/lstate.c
  ${LUA54_SRC_DIR}/lstring.c
  ${LUA54_SRC_DIR}/lstrlib.c
  ${LUA54_SRC_DIR}/ltable.c
  ${LUA54_SRC_DIR}/ltablib.c
  ${LUA54_SRC_DIR}/ltm.c
  ${LUA54_SRC_DIR}/lundump.c
  ${LUA54_SRC_DIR}/lutf8lib.c
  ${LUA54_SRC_DIR}/lvm.c
  ${LUA54_SRC_DIR}/lzio.c
)

target_include_directories(lua54 PUBLIC ${LUA54_SRC_DIR})

if(UNIX)
  target_link_libraries(lua54 PUBLIC m)
endif()

message(STATUS "TextLT: embedded Lua 5.4.8 enabled")

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
  src/curl_manager.cpp
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
  src/modals/assistant_modals.cpp
  src/modals/assistant_modals_ai.cpp
  src/modals/assistant_modals_tts.cpp
  src/modals/help_dialog.cpp
  src/modals/modal_ai.cpp
  src/modals/modal_git.cpp
  src/modals/modal_git_settings.cpp
  src/modals/modal_git_status_diff_commit.cpp
  src/modals/modal_git_branches_server.cpp
  src/modals/modal_git_remote_tags.cpp
  src/modals/modal_git_confirm.cpp
  src/modals/modal_git_utils.cpp
  src/modals/modal_recent_files.cpp
  src/modals/modal_tts.cpp
  src/modals/modal_window.cpp
  src/modals/theme_dialog.cpp
  src/modals/unsaved_changes_dialog.cpp
  src/modals/modal_search_files.cpp
  src/modals/modal_files.cpp
  src/modals/modal_text_processors.cpp
  src/opened_config.cpp
  src/recent_files.cpp
  src/sidebar_component.cpp
  src/syntax_highlighter.cpp
  src/terminal_flow_control_guard.cpp
  src/text_transformer.cpp
  src/text_importer.cpp
  src/text_parser_lua_engine.cpp
  src/text_parser_manager.cpp
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
  pugixml::pugixml
  CURL::libcurl
  ${LibArchive_LIBRARIES}
  lua54
  Threads::Threads
)

if(WIN32)
  target_link_libraries(textlt PRIVATE shell32 ole32)
endif()

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
