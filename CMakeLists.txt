cmake_minimum_required(VERSION 3.14)
project(textlt VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

enable_testing()

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

if(MSVC)
  # Match the static vcpkg triplet used in CI so the executable and bundled
  # dependencies do not mix `/MT` and `/MD` runtimes.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    COMMAND
      ${CMAKE_COMMAND}
      -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-v5-hidden-cursor.patch
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
)

# Keep FTXUI's Windows workaround for cursor-position reports. Without it,
# cursor replies can be interleaved with mouse events in Microsoft terminals,
# corrupting clicks and leaking response text into the editor. The small FTXUI
# patch above initializes the native cursor as hidden without disabling this
# workaround; focused text inputs still opt into a visible cursor explicitly.
if(WIN32)
  set(FTXUI_MICROSOFT_TERMINAL_FALLBACK ON CACHE BOOL
      "Use FTXUI's Microsoft Terminal compatibility path" FORCE)
endif()
FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.16
)
FetchContent_MakeAvailable(pugixml)

# Remote/network support uses the external curl executable at runtime.
# No curl development library is required for building TextLT.

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

# Embedded localhost HTTP server for the persistent Piper service.
FetchContent_Declare(civetweb
  GIT_REPOSITORY https://github.com/civetweb/civetweb.git
  GIT_TAG v1.16
  PATCH_COMMAND
    ${CMAKE_COMMAND}
      -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/civetweb-v1.16-default-init-return.patch
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
)

FetchContent_GetProperties(civetweb)
if(NOT civetweb_POPULATED)
  FetchContent_Populate(civetweb)
endif()

add_library(textlt_civetweb STATIC
  ${civetweb_SOURCE_DIR}/src/civetweb.c
)

target_include_directories(textlt_civetweb PUBLIC
  ${civetweb_SOURCE_DIR}/include
)
target_include_directories(textlt_civetweb PRIVATE
  ${civetweb_SOURCE_DIR}/src
)
target_compile_definitions(textlt_civetweb PRIVATE
  NO_SSL
  NO_CGI
  NO_FILES
  NO_CACHING
)
target_link_libraries(textlt_civetweb PUBLIC Threads::Threads)
if(WIN32)
  target_compile_definitions(textlt_civetweb PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(textlt_civetweb PUBLIC ws2_32)
endif()

add_executable(textlt-piper-server
  tools/piper_server/textlt_piper_server.cpp
)
target_include_directories(textlt-piper-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(textlt-piper-server PRIVATE textlt_civetweb)



add_executable(textlt
  src/app.cpp
  src/app_command_registry.cpp
  src/app_event_dispatcher.cpp
  src/app_commands.cpp
  src/app_find_replace.cpp
  src/layout_controller.cpp
  src/app_modals.cpp
  src/app_menu_actions.cpp
  src/app_file_actions.cpp
  src/app_document_actions.cpp
  src/app_tts_actions.cpp
  src/app_distraction_actions.cpp
  src/app_workspace.cpp
  src/app_resources.cpp
  src/app_view.cpp
  src/shortcut_key.cpp
  src/shortcut_registry.cpp
  src/shortcut_store.cpp
  src/custom_processor_builder.cpp
  src/cloud_tts_pipeline.cpp
  src/builtin_text_processors.cpp
  src/distraction_mode_controller.cpp
  src/config_manager.cpp
  src/curl_manager.cpp
  src/editor_config.cpp
  src/editor_component.cpp
  src/editor_events.cpp
  src/editor_input_controller.cpp
  src/editor_keymap.cpp
  src/editor_mouse.cpp
  src/editor_utils.cpp
  src/editor_viewport.cpp
  src/editor/editor_viewport.cpp
  src/editor/text_buffer.cpp
  src/editor/document_session.cpp
  src/editor/document_session_content.cpp
  src/editor/document_session_cursor.cpp
  src/editor/document_session_editing.cpp
  src/editor/document_session_history.cpp
  src/editor/document_session_selection.cpp
  src/editor/document_session_transform.cpp
  src/editor/document_workspace.cpp
  src/editor/document_file_controller.cpp
  src/editor/clipboard_controller.cpp
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
  src/modals/assistant_download_progress.cpp
  src/modals/assistant_modals_tts.cpp
  src/modals/assistant_modals_server.cpp
  src/modals/help_dialog.cpp
  src/modals/modal_ai.cpp
  src/modals/modal_git.cpp
  src/modals/modal_git_settings.cpp
  src/modals/modal_git_status_diff_commit.cpp
  src/modals/modal_git_branches_server.cpp
  src/modals/modal_git_remote_tags.cpp
  src/modals/modal_git_confirm.cpp
  src/modals/modal_git_compare.cpp
  src/modals/modal_git_utils.cpp
  src/modals/modal_recent_files.cpp
  src/modals/modal_tts.cpp
  src/modals/modal_view_layout.cpp
  src/modals/modal_distraction_options.cpp
  src/modals/modal_window.cpp
  src/modals/theme_dialog.cpp
  src/modals/unsaved_changes_dialog.cpp
  src/modals/modal_search_files.cpp
  src/modals/modal_files.cpp
  src/modals/modal_keyboard_shortcuts.cpp
  src/modals/modal_custom_processor_builder.cpp
  src/modals/modal_text_processors.cpp
  src/piper_manager.cpp
  src/tts_audio_player.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_config_store.cpp
  src/remote/remote_oauth_token_store.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_dropbox_provider.cpp
  src/remote/remote_oauth_flow.cpp
  src/remote/remote_google_drive_provider.cpp
  src/remote/remote_microsoft_drive_provider.cpp
  src/remote/remote_command_runner.cpp
  src/remote/remote_local_provider.cpp
  src/remote/remote_sftp_provider.cpp
  src/remote/modal_remote_connections.cpp
  src/remote/modal_remote_files.cpp
  src/sidebar_component.cpp
  src/syntax_highlighter.cpp
  src/top_bar_row.cpp
  src/bottom_bar_row.cpp
  src/terminal_flow_control_guard.cpp
  src/text_transformer.cpp
  src/text_importer.cpp
  src/text_parser_lua_engine.cpp
  src/text_parser_manager.cpp
  src/theme.cpp
  src/file_manager.cpp
  src/search_in_files.cpp
)

target_include_directories(textlt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${LibArchive_INCLUDE_DIRS}
)

target_link_libraries(textlt PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  pugixml::pugixml
  ${LibArchive_LIBRARIES}
  lua54
  Threads::Threads
)

install(TARGETS textlt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS textlt-piper-server RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/textlt/piper/bin)
if(UNIX)
  install(PROGRAMS
    scripts/textlt-install-runtime-deps.sh
    scripts/textlt-install-piper-server.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

if(WIN32)
  install(FILES
    scripts/textlt-install-runtime-deps.ps1
    DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
install(DIRECTORY themes DESTINATION ${CMAKE_INSTALL_DATADIR}/textlt)
install(DIRECTORY text_processors DESTINATION ${CMAKE_INSTALL_DATADIR}/textlt)

if(WIN32)
  target_compile_definitions(textlt PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(textlt PRIVATE shell32 ole32)
endif()

add_executable(textlt_document_navigation_test
  tests/document_navigation_test.cpp
  src/editor/document_session.cpp
  src/editor/document_session_content.cpp
  src/editor/document_session_cursor.cpp
  src/editor/document_session_history.cpp
  src/editor/document_session_selection.cpp
  src/editor/text_buffer.cpp
  src/editor_utils.cpp
  src/history_manager.cpp
)

add_executable(textlt_text_importer_test
  tests/text_importer_test.cpp
  src/text_importer.cpp
)

add_executable(textlt_keyboard_shortcuts_test
  tests/keyboard_shortcuts_test.cpp
)

add_executable(textlt_syntax_highlighter_test
  tests/syntax_highlighter_test.cpp
  src/syntax_highlighter.cpp
  src/lexers/c_family_lexer.cpp
  src/lexers/config_script_lexer.cpp
  src/lexers/database_lexer.cpp
  src/lexers/web_lexer.cpp
)

add_executable(textlt_builtin_text_processors_test
  tests/builtin_text_processors_test.cpp
  src/builtin_text_processors.cpp
  src/text_transformer.cpp
)

add_executable(textlt_app_command_registry_test
  tests/app_command_registry_test.cpp
  src/app_command_registry.cpp
)

add_executable(textlt_shortcut_registry_test
  tests/shortcut_registry_test.cpp
  src/shortcut_key.cpp
  src/shortcut_registry.cpp
)

add_executable(textlt_custom_processor_builder_test
  tests/custom_processor_builder_test.cpp
  src/custom_processor_builder.cpp
  src/text_parser_manager.cpp
  src/text_parser_lua_engine.cpp
  src/builtin_text_processors.cpp
  src/text_transformer.cpp
)

add_executable(textlt_remote_path_test
  tests/remote_path_test.cpp
  src/remote/remote_connection_config.cpp
)

add_executable(textlt_remote_oauth_token_store_test
  tests/remote_oauth_token_store_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_oauth_token_store.cpp
)

target_include_directories(textlt_keyboard_shortcuts_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_syntax_highlighter_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_builtin_text_processors_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_app_command_registry_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_shortcut_registry_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_custom_processor_builder_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_remote_path_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_remote_oauth_token_store_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)


add_executable(textlt_remote_http_client_test
  tests/remote_http_client_test.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_remote_http_client_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(textlt_no_embedded_curl_test
  tests/no_embedded_curl_test.cpp
)

target_compile_definitions(textlt_no_embedded_curl_test PRIVATE
  TEXTLT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(textlt_remote_dropbox_provider_test
  tests/remote_dropbox_provider_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_dropbox_provider.cpp
  src/remote/remote_oauth_token_store.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_remote_dropbox_provider_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)


add_executable(textlt_remote_google_drive_provider_test
  tests/remote_google_drive_provider_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_google_drive_provider.cpp
  src/remote/remote_oauth_token_store.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_remote_google_drive_provider_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)


add_executable(textlt_remote_microsoft_drive_provider_test
  tests/remote_microsoft_drive_provider_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_microsoft_drive_provider.cpp
  src/remote/remote_oauth_token_store.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_remote_microsoft_drive_provider_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)



add_executable(textlt_tts_pipeline_test
  tests/tts_pipeline_test.cpp
  src/cloud_tts_pipeline.cpp
  src/piper_manager.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_tts_pipeline_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_executable(textlt_piper_manager_test
  tests/piper_manager_test.cpp
  src/piper_manager.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_piper_manager_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_executable(textlt_tts_audio_player_test
  tests/tts_audio_player_test.cpp
  src/tts_audio_player.cpp
)

add_executable(textlt_ui_button_test
  tests/ui_button_test.cpp
)

target_include_directories(textlt_tts_audio_player_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_ui_button_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(textlt_tts_audio_player_test PRIVATE Threads::Threads)
target_link_libraries(textlt_ui_button_test PRIVATE ftxui::component)

target_link_libraries(textlt_keyboard_shortcuts_test PRIVATE ftxui::component)
target_link_libraries(textlt_syntax_highlighter_test PRIVATE ftxui::dom)
target_link_libraries(textlt_shortcut_registry_test PRIVATE ftxui::component)
target_link_libraries(textlt_custom_processor_builder_test PRIVATE lua54)

target_include_directories(textlt_text_importer_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${LibArchive_INCLUDE_DIRS}
)

target_link_libraries(textlt_text_importer_test PRIVATE
  pugixml::pugixml
  ${LibArchive_LIBRARIES}
)

target_include_directories(textlt_document_navigation_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(textlt_document_navigation_test PRIVATE ftxui::screen)

add_test(NAME textlt_document_navigation_test COMMAND textlt_document_navigation_test)
add_test(NAME textlt_text_importer_test COMMAND textlt_text_importer_test)
add_test(NAME textlt_keyboard_shortcuts_test COMMAND textlt_keyboard_shortcuts_test)
add_test(NAME textlt_syntax_highlighter_test COMMAND textlt_syntax_highlighter_test)
add_test(NAME textlt_builtin_text_processors_test COMMAND textlt_builtin_text_processors_test)
add_test(NAME textlt_app_command_registry_test COMMAND textlt_app_command_registry_test)
add_test(NAME textlt_shortcut_registry_test COMMAND textlt_shortcut_registry_test)
add_test(NAME textlt_custom_processor_builder_test COMMAND textlt_custom_processor_builder_test)
add_test(NAME textlt_tts_pipeline_test COMMAND textlt_tts_pipeline_test)
add_test(NAME textlt_piper_manager_test COMMAND textlt_piper_manager_test)
add_test(NAME textlt_tts_audio_player_test COMMAND textlt_tts_audio_player_test)
add_test(NAME textlt_ui_button_test COMMAND textlt_ui_button_test)
add_test(NAME textlt_remote_path_test COMMAND textlt_remote_path_test)
add_test(NAME textlt_remote_oauth_token_store_test COMMAND textlt_remote_oauth_token_store_test)
add_test(NAME textlt_remote_http_client_test COMMAND textlt_remote_http_client_test)
add_test(NAME textlt_no_embedded_curl_test COMMAND textlt_no_embedded_curl_test)
add_test(NAME textlt_remote_dropbox_provider_test COMMAND textlt_remote_dropbox_provider_test)
add_test(NAME textlt_remote_google_drive_provider_test COMMAND textlt_remote_google_drive_provider_test)
add_test(NAME textlt_remote_microsoft_drive_provider_test COMMAND textlt_remote_microsoft_drive_provider_test)
