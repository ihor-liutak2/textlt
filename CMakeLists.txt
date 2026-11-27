cmake_minimum_required(VERSION 3.14)

# CMP0091 must be set before enabling C/C++ so CMAKE_MSVC_RUNTIME_LIBRARY is
# honored by Visual Studio generators.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(textlt VERSION 1.1.1 LANGUAGES C CXX)

include(GNUInstallDirs)

enable_testing()

# Use extraction-time timestamps for URL-based FetchContent downloads.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
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
  GIT_TAG v7.0.3
  # Preserve Ctrl+H as a distinct event so it remains configurable instead of
  # being normalized to Backspace by FTXUI's terminal input parser.
  PATCH_COMMAND
    ${CMAKE_COMMAND}
      -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-v7-shortcuts.patch
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
    COMMAND
    ${CMAKE_COMMAND}
      -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-v7-alt-utf8.patch
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
    COMMAND
    ${CMAKE_COMMAND}
      -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/ftxui-v7-paste.patch
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
)

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
# Sourced from the vendored tarball: www.lua.org is intermittently unreachable,
# which previously broke release builds at configure time.
FetchContent_Declare(lua55
  URL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.5.1.tar.gz
  URL_HASH SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
)
FetchContent_MakeAvailable(lua55)

set(LUA55_SRC_DIR ${lua55_SOURCE_DIR}/src)

add_library(lua55 STATIC
  ${LUA55_SRC_DIR}/lapi.c
  ${LUA55_SRC_DIR}/lauxlib.c
  ${LUA55_SRC_DIR}/lbaselib.c
  ${LUA55_SRC_DIR}/lcode.c
  ${LUA55_SRC_DIR}/lctype.c
  ${LUA55_SRC_DIR}/ldebug.c
  ${LUA55_SRC_DIR}/ldo.c
  ${LUA55_SRC_DIR}/ldump.c
  ${LUA55_SRC_DIR}/lfunc.c
  ${LUA55_SRC_DIR}/lgc.c
  ${LUA55_SRC_DIR}/llex.c
  ${LUA55_SRC_DIR}/lmathlib.c
  ${LUA55_SRC_DIR}/lmem.c
  ${LUA55_SRC_DIR}/lobject.c
  ${LUA55_SRC_DIR}/lopcodes.c
  ${LUA55_SRC_DIR}/lparser.c
  ${LUA55_SRC_DIR}/lstate.c
  ${LUA55_SRC_DIR}/lstring.c
  ${LUA55_SRC_DIR}/lstrlib.c
  ${LUA55_SRC_DIR}/ltable.c
  ${LUA55_SRC_DIR}/ltablib.c
  ${LUA55_SRC_DIR}/ltm.c
  ${LUA55_SRC_DIR}/lundump.c
  ${LUA55_SRC_DIR}/lutf8lib.c
  ${LUA55_SRC_DIR}/lvm.c
  ${LUA55_SRC_DIR}/lzio.c
)

target_include_directories(lua55 PUBLIC ${LUA55_SRC_DIR})

if(UNIX)
  target_link_libraries(lua55 PUBLIC m)
endif()

message(STATUS "TextLT: embedded Lua 5.5.1 enabled")

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
if(CMAKE_VERSION VERSION_LESS 3.18)
  # SOURCE_SUBDIR, used below to populate without adding CivetWeb's own CMake
  # project, was added in CMake 3.18. Keep the legacy path for older CMake.
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
else()
  FetchContent_Declare(civetweb
    GIT_REPOSITORY https://github.com/civetweb/civetweb.git
    GIT_TAG v1.16
    SOURCE_SUBDIR textlt-populate-only
    PATCH_COMMAND
      ${CMAKE_COMMAND}
        -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/civetweb-v1.16-default-init-return.patch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch_once.cmake
  )
  FetchContent_MakeAvailable(civetweb)
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



add_subdirectory(connect)

add_executable(textlt
  connect/ui/connect_workspace.cpp
  connect/ui/connect_settings.cpp
  connect/ui/connect_profiles.cpp
  connect/ui/network_diagnostics.cpp
  connect/ui/connect_structured.cpp
  connect/ui/download_destination.cpp
  src/app_connect.cpp
  src/connect_content.cpp
  src/modals/modal_connect_settings.cpp
  src/modals/modal_telegram_settings.cpp
  src/modals/modal_irc_settings.cpp
  src/irc/irc_settings.cpp
  src/modals/modal_textlt_config.cpp
  src/app.cpp
  src/notification_manager.cpp
  src/notification_overlay.cpp
  src/app_command_registry.cpp
  src/app_event_dispatcher.cpp
  src/app_commands.cpp
  src/layout_controller.cpp
  src/app_modals.cpp
  src/app_menu_actions.cpp
  src/app_file_actions.cpp
  src/app_document_actions.cpp
  src/app_tts_actions.cpp
  src/app_distraction_actions.cpp
  src/app_workspace.cpp
  src/app_resources.cpp
  src/app_projects.cpp
  src/app_view.cpp
  src/app_notes_actions.cpp
  src/app_actions_actions.cpp
  src/app_find_replace.cpp
  src/app_editor_context_menu.cpp
  src/shortcut_key.cpp
  src/shortcut_registry.cpp
  src/shortcut_store.cpp
  src/custom_processor_builder.cpp
  src/cloud_tts_pipeline.cpp
  src/builtin_text_processors.cpp
  src/distraction_mode_controller.cpp
  src/config_manager.cpp
  src/context_menu_component.cpp
  src/project_store.cpp
  src/ai/ai_backend.cpp
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
  src/editor/clipboard_input.cpp
  src/notes/note_document.cpp
  src/notes/markdown_importer.cpp
  src/notes/note_serializer.cpp
  src/notes/note_repository.cpp
  src/notes/note_session.cpp
  src/notes/notes_context_menu.cpp
  src/notes/notes_workspace_component.cpp
  src/actions/action_model.cpp
  src/actions/action_config_importer.cpp
  src/actions/action_serializer.cpp
  src/actions/action_repository.cpp
  src/actions/actions_workspace_component.cpp
  src/actions/action_snapshot.cpp
  src/actions/actions_context_menu.cpp
  src/find_replace/editor_find_target.cpp
  src/find_replace/notes_find_target.cpp
  src/find_replace/actions_find_target.cpp
  src/find_replace/find_replace_panel.cpp
  src/file_utils.cpp
  src/git_manager.cpp
  src/history_manager.cpp
  src/lexers/c_family_lexer.cpp
  src/lexers/config_script_lexer.cpp
  src/lexers/database_lexer.cpp
  src/lexers/web_lexer.cpp
  src/main.cpp
  src/startup_check.cpp
  src/menu_bar.cpp
  src/modals/assistant_modals.cpp
  src/modals/assistant_download_progress.cpp
  src/modals/assistant_modals_tts.cpp
  src/modals/assistant_modals_server.cpp
  src/modals/help_dialog.cpp
  src/modals/modal_action_editor.cpp
  src/modals/modal_ai_settings.cpp
  src/modals/modal_quick_ai.cpp
  src/modals/modal_git.cpp
  src/modals/modal_git_settings.cpp
  src/modals/modal_git_status_diff_commit.cpp
  src/modals/modal_git_branches_server.cpp
  src/modals/modal_git_remote_tags.cpp
  src/modals/modal_git_confirm.cpp
  src/modals/modal_git_compare.cpp
  src/modals/modal_git_utils.cpp
  src/modals/modal_recent_files.cpp
  src/modals/modal_sidebar_shortcuts.cpp
  src/modals/modal_tts.cpp
  src/modals/modal_view_layout.cpp
  src/modals/modal_distraction_options.cpp
  src/modals/modal_theme_colors.cpp
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
  src/remote/remote_command_runner.cpp
  src/remote/remote_local_provider.cpp
  src/remote/remote_sftp_provider.cpp
  src/remote/remote_ssh_config.cpp
  src/remote/remote_ftps_provider.cpp
  src/remote/remote_provider_factory.cpp
  src/remote/modal_remote_connections.cpp
  src/remote/modal_remote_files.cpp
  src/sidebar_component.cpp
  src/open_files_bar.cpp
  src/syntax_highlighter.cpp
  src/telegram/telegram_http_api.cpp
  src/telegram/telegram_provider.cpp
  src/telegram/telegram_settings_store.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/connect/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${LibArchive_INCLUDE_DIRS}
)

target_compile_definitions(textlt PRIVATE
  TEXTLT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(textlt PRIVATE
  textlt::connect
  ftxui::screen
  ftxui::dom
  ftxui::component
  pugixml::pugixml
  ${LibArchive_LIBRARIES}
  lua55
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
  target_link_libraries(textlt PRIVATE shell32 ole32 advapi32 user32 iphlpapi)
  if(MINGW)
    target_link_options(textlt PRIVATE -municode)
  endif()
endif()

add_executable(textlt_document_navigation_test
  tests/document_navigation_test.cpp
  src/editor/editor_viewport.cpp
  src/editor/document_session.cpp
  src/editor/document_session_content.cpp
  src/editor/document_session_cursor.cpp
  src/editor/document_session_history.cpp
  src/editor/document_session_selection.cpp
  src/editor/text_buffer.cpp
  src/editor_utils.cpp
  src/history_manager.cpp
)

add_executable(textlt_connect_workspace_layout_test
  tests/connect_workspace_layout_test.cpp
)
target_include_directories(textlt_connect_workspace_layout_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/connect/ui
)

add_executable(textlt_telegram_provider_test
  tests/telegram_provider_test.cpp
  connect/ui/download_destination.cpp
  src/editor_config.cpp
  src/telegram/telegram_provider.cpp
  src/telegram/telegram_settings_store.cpp
)
target_include_directories(textlt_telegram_provider_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/connect/include
  ${CMAKE_CURRENT_SOURCE_DIR}/connect/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(textlt_telegram_provider_test PRIVATE
  Threads::Threads
  ftxui::component
)

add_executable(textlt_editor_context_menu_test
  tests/editor_context_menu_test.cpp
  src/context_menu_component.cpp
  src/editor/editor_viewport.cpp
  src/editor/document_session.cpp
  src/editor/document_session_content.cpp
  src/editor/document_session_cursor.cpp
  src/editor/document_session_history.cpp
  src/editor/document_session_selection.cpp
  src/editor/document_session_transform.cpp
  src/editor/text_buffer.cpp
  src/editor_utils.cpp
  src/history_manager.cpp
  src/text_transformer.cpp
)
target_include_directories(textlt_editor_context_menu_test PRIVATE src)
target_link_libraries(textlt_editor_context_menu_test PRIVATE ftxui::component)

add_executable(textlt_open_files_bar_test
  tests/open_files_bar_test.cpp
  src/open_files_bar.cpp
  src/context_menu_component.cpp
  src/editor/document_workspace.cpp
  src/editor/editor_viewport.cpp
  src/editor/document_session.cpp
  src/editor/document_session_content.cpp
  src/editor/document_session_cursor.cpp
  src/editor/document_session_history.cpp
  src/editor/document_session_selection.cpp
  src/editor/text_buffer.cpp
  src/editor_utils.cpp
  src/history_manager.cpp
  src/theme.cpp
)
target_include_directories(textlt_open_files_bar_test PRIVATE src third_party)
target_link_libraries(textlt_open_files_bar_test PRIVATE ftxui::component)

add_executable(textlt_theme_customization_test
  tests/theme_customization_test.cpp
  src/theme.cpp
)

add_executable(textlt_text_importer_test
  tests/text_importer_test.cpp
  src/text_importer.cpp
)

add_executable(textlt_ai_backend_utils_test
  tests/ai_backend_utils_test.cpp
  src/ai/ai_backend.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

add_executable(textlt_remote_command_runner_streaming_test
  tests/remote_command_runner_streaming_test.cpp
  src/remote/remote_command_runner.cpp
)

add_executable(textlt_document_paragraph_processor_test
  tests/document_paragraph_processor_test.cpp
  src/editor/document_session.cpp
  src/editor/document_session_content.cpp
  src/editor/document_session_cursor.cpp
  src/editor/document_session_editing.cpp
  src/editor/document_session_history.cpp
  src/editor/document_session_selection.cpp
  src/editor/text_buffer.cpp
  src/editor_utils.cpp
  src/history_manager.cpp
)

add_executable(textlt_keyboard_shortcuts_test
  tests/keyboard_shortcuts_test.cpp
)

add_executable(textlt_terminal_alt_utf8_test
  tests/terminal_alt_utf8_test.cpp
  src/editor/clipboard_input.cpp
)

add_executable(textlt_syntax_highlighter_test
  tests/syntax_highlighter_test.cpp
  src/syntax_highlighter.cpp
  src/lexers/c_family_lexer.cpp
  src/lexers/config_script_lexer.cpp
  src/lexers/database_lexer.cpp
  src/lexers/web_lexer.cpp
)

add_executable(textlt_quick_text_panel_test
  tests/quick_text_panel_test.cpp
  src/editor/clipboard_input.cpp
  src/modals/modal_text_processors.cpp
  src/modals/modal_window.cpp
  src/text_parser_manager.cpp
  src/text_parser_lua_engine.cpp
  src/builtin_text_processors.cpp
  src/text_transformer.cpp
  src/app_resources.cpp
  src/editor_config.cpp
  src/theme.cpp
)
target_include_directories(textlt_quick_text_panel_test PRIVATE src third_party)
target_link_libraries(textlt_quick_text_panel_test PRIVATE ftxui::component lua55)
add_test(NAME textlt_quick_text_panel_test COMMAND textlt_quick_text_panel_test)

add_executable(textlt_builtin_text_processors_test
  tests/builtin_text_processors_test.cpp
  src/builtin_text_processors.cpp
  src/text_transformer.cpp
)

add_executable(textlt_app_command_registry_test
  tests/app_command_registry_test.cpp
  src/app_command_registry.cpp
)

add_executable(textlt_note_storage_test
  tests/note_storage_test.cpp
  src/notes/note_document.cpp
  src/notes/note_serializer.cpp
  src/notes/note_repository.cpp
)

add_executable(textlt_action_repository_test
  tests/action_repository_test.cpp
  src/actions/action_model.cpp
  src/actions/action_serializer.cpp
  src/actions/action_repository.cpp
)
add_executable(textlt_action_config_importer_test
  tests/action_config_importer_test.cpp
  src/actions/action_config_importer.cpp
)
add_executable(textlt_actions_workspace_test
  tests/actions_workspace_test.cpp
  src/actions/action_model.cpp
  src/actions/action_serializer.cpp
  src/actions/action_repository.cpp
  src/actions/actions_workspace_component.cpp
  src/actions/action_snapshot.cpp
  src/actions/actions_context_menu.cpp
  src/sidebar_component.cpp
  src/modals/modal_action_editor.cpp
  src/editor/clipboard_input.cpp
  src/modals/modal_window.cpp
  src/theme.cpp
)

add_executable(textlt_note_session_test
  tests/note_session_test.cpp
  src/notes/note_document.cpp
  src/notes/note_session.cpp
)

add_executable(textlt_notes_workspace_actions_test
  tests/notes_workspace_actions_test.cpp
  src/notes/note_document.cpp
  src/notes/note_serializer.cpp
  src/notes/note_repository.cpp
  src/notes/note_session.cpp
  src/notes/notes_context_menu.cpp
  src/notes/notes_workspace_component.cpp
  src/editor/clipboard_input.cpp
  src/editor_utils.cpp
  src/theme.cpp
)

add_executable(textlt_menu_bar_notes_test
  tests/menu_bar_notes_test.cpp
  src/menu_bar.cpp
  src/theme.cpp
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

add_executable(textlt_remote_config_store_test
  tests/remote_config_store_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_config_store.cpp
  src/remote/remote_oauth_token_store.cpp
)

add_executable(textlt_project_store_test
  tests/project_store_test.cpp
  src/project_store.cpp
)

target_include_directories(textlt_keyboard_shortcuts_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_terminal_alt_utf8_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${ftxui_SOURCE_DIR}/src
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

target_include_directories(textlt_remote_config_store_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_project_store_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)


add_executable(textlt_remote_http_client_test
  tests/remote_http_client_test.cpp
  src/remote/remote_http_client.cpp
  src/remote/remote_command_runner.cpp
)

add_executable(textlt_remote_ftps_provider_test
  tests/remote_ftps_provider_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_ftps_provider.cpp
  src/remote/remote_command_runner.cpp
)

add_executable(textlt_remote_ssh_config_test
  tests/remote_ssh_config_test.cpp
  src/remote/remote_ssh_config.cpp
)

target_include_directories(textlt_remote_ssh_config_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(textlt_remote_sftp_provider_test
  tests/remote_sftp_provider_test.cpp
  src/remote/remote_connection_config.cpp
  src/remote/remote_sftp_provider.cpp
  src/remote/remote_ssh_config.cpp
  src/remote/remote_command_runner.cpp
)

target_include_directories(textlt_remote_sftp_provider_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_remote_ftps_provider_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
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

add_executable(textlt_notification_manager_test
  tests/notification_manager_test.cpp
  src/notification_manager.cpp
)
target_include_directories(textlt_notification_manager_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_executable(textlt_notification_overlay_test
  tests/notification_overlay_test.cpp
  src/notification_manager.cpp
  src/notification_overlay.cpp
  src/theme.cpp
)
target_include_directories(textlt_notification_overlay_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(textlt_notification_overlay_test PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
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
target_link_libraries(textlt_terminal_alt_utf8_test PRIVATE ftxui::component)
target_link_libraries(textlt_syntax_highlighter_test PRIVATE ftxui::dom)
target_link_libraries(textlt_shortcut_registry_test PRIVATE ftxui::component)
target_link_libraries(textlt_custom_processor_builder_test PRIVATE lua55)

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

target_include_directories(textlt_theme_customization_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_ai_backend_utils_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_remote_command_runner_streaming_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_note_storage_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_include_directories(textlt_action_repository_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_include_directories(textlt_action_config_importer_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(textlt_actions_workspace_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_note_session_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(textlt_notes_workspace_actions_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_menu_bar_notes_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_include_directories(textlt_document_paragraph_processor_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(textlt_document_navigation_test PRIVATE ftxui::component)
target_link_libraries(textlt_theme_customization_test PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
)
target_link_libraries(textlt_notes_workspace_actions_test PRIVATE
  ftxui::component
  Threads::Threads
)
target_link_libraries(textlt_actions_workspace_test PRIVATE
  ftxui::component
  Threads::Threads
)
target_link_libraries(textlt_menu_bar_notes_test PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
)

target_link_libraries(textlt_ai_backend_utils_test PRIVATE Threads::Threads)
target_link_libraries(textlt_remote_command_runner_streaming_test PRIVATE Threads::Threads)
target_link_libraries(textlt_document_paragraph_processor_test PRIVATE ftxui::screen)

add_test(NAME textlt_document_navigation_test COMMAND textlt_document_navigation_test)
add_test(NAME textlt_connect_workspace_layout_test COMMAND textlt_connect_workspace_layout_test)
add_test(NAME textlt_telegram_provider_test COMMAND textlt_telegram_provider_test)
add_test(NAME textlt_editor_context_menu_test COMMAND textlt_editor_context_menu_test)
add_test(NAME textlt_open_files_bar_test COMMAND textlt_open_files_bar_test)
if(UNIX)
  # Python is an optional test-only dependency; it is never installed or
  # included in release packages.
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME textlt_open_files_bar_pty_test
      COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/open_files_bar_pty_test.py $<TARGET_FILE:textlt>)
    set_tests_properties(textlt_open_files_bar_pty_test PROPERTIES TIMEOUT 60)
    set(pty_scenarios
      editing
      read_only
      compare_exit
      terminal_clipboard
      osc52_multiline_utf8
      failed_native_write
      failed_osc52_fallback
    )
    foreach(scenario IN LISTS pty_scenarios)
      set(test_name textlt_editor_context_menu_pty_${scenario}_test)
      add_test(NAME ${test_name}
        COMMAND ${Python3_EXECUTABLE} -B
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_context_menu_pty_test.py
          $<TARGET_FILE:textlt> --scenario ${scenario})
    endforeach()
    set_tests_properties(
      textlt_editor_context_menu_pty_editing_test
      textlt_editor_context_menu_pty_read_only_test
      PROPERTIES TIMEOUT 30)
    set_tests_properties(textlt_editor_context_menu_pty_compare_exit_test
      PROPERTIES TIMEOUT 35)
    set_tests_properties(
      textlt_editor_context_menu_pty_terminal_clipboard_test
      textlt_editor_context_menu_pty_osc52_multiline_utf8_test
      PROPERTIES TIMEOUT 20)
    add_test(NAME textlt_connect_workspace_pty_test
      COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/connect_workspace_pty_test.py
        $<TARGET_FILE:textlt>)
    set_tests_properties(textlt_connect_workspace_pty_test PROPERTIES TIMEOUT 60)
    add_test(NAME textlt_irc_settings_pty_test
      COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/irc_settings_pty_test.py $<TARGET_FILE:textlt>)
    set_tests_properties(textlt_irc_settings_pty_test PROPERTIES TIMEOUT 60)
    set_tests_properties(textlt_editor_context_menu_pty_failed_native_write_test
      PROPERTIES TIMEOUT 15)
    set_tests_properties(textlt_editor_context_menu_pty_failed_osc52_fallback_test
      PROPERTIES TIMEOUT 20)
  endif()
endif()
add_test(NAME textlt_theme_customization_test COMMAND textlt_theme_customization_test)
add_test(NAME textlt_ai_backend_utils_test COMMAND textlt_ai_backend_utils_test)
add_test(NAME textlt_remote_command_runner_streaming_test
  COMMAND textlt_remote_command_runner_streaming_test)
add_test(NAME textlt_document_paragraph_processor_test COMMAND textlt_document_paragraph_processor_test)
add_test(NAME textlt_text_importer_test COMMAND textlt_text_importer_test)
add_test(NAME textlt_keyboard_shortcuts_test COMMAND textlt_keyboard_shortcuts_test)
add_test(NAME textlt_terminal_alt_utf8_test COMMAND textlt_terminal_alt_utf8_test)
add_test(NAME textlt_syntax_highlighter_test COMMAND textlt_syntax_highlighter_test)
add_test(NAME textlt_builtin_text_processors_test COMMAND textlt_builtin_text_processors_test)
add_test(NAME textlt_app_command_registry_test COMMAND textlt_app_command_registry_test)
add_test(NAME textlt_note_storage_test COMMAND textlt_note_storage_test)
add_test(NAME textlt_action_repository_test COMMAND textlt_action_repository_test)
add_test(NAME textlt_action_config_importer_test COMMAND textlt_action_config_importer_test)
add_test(NAME textlt_actions_workspace_test COMMAND textlt_actions_workspace_test)
add_test(NAME textlt_note_session_test COMMAND textlt_note_session_test)
add_test(NAME textlt_notes_workspace_actions_test COMMAND textlt_notes_workspace_actions_test)
add_test(NAME textlt_menu_bar_notes_test COMMAND textlt_menu_bar_notes_test)
add_test(NAME textlt_shortcut_registry_test COMMAND textlt_shortcut_registry_test)
add_test(NAME textlt_custom_processor_builder_test COMMAND textlt_custom_processor_builder_test)
add_test(NAME textlt_tts_pipeline_test COMMAND textlt_tts_pipeline_test)
add_test(NAME textlt_piper_manager_test COMMAND textlt_piper_manager_test)
add_test(NAME textlt_tts_audio_player_test COMMAND textlt_tts_audio_player_test)
add_test(NAME textlt_ui_button_test COMMAND textlt_ui_button_test)
add_test(NAME textlt_notification_manager_test COMMAND textlt_notification_manager_test)
add_test(NAME textlt_notification_overlay_test COMMAND textlt_notification_overlay_test)
set_tests_properties(textlt_notification_overlay_test PROPERTIES TIMEOUT 5)
add_test(NAME textlt_remote_path_test COMMAND textlt_remote_path_test)
add_test(NAME textlt_remote_oauth_token_store_test COMMAND textlt_remote_oauth_token_store_test)
add_test(NAME textlt_remote_config_store_test COMMAND textlt_remote_config_store_test)
add_test(NAME textlt_project_store_test COMMAND textlt_project_store_test)
add_test(NAME textlt_remote_http_client_test COMMAND textlt_remote_http_client_test)
add_test(NAME textlt_remote_ftps_provider_test COMMAND textlt_remote_ftps_provider_test)
add_test(NAME textlt_remote_ssh_config_test COMMAND textlt_remote_ssh_config_test)
add_test(NAME textlt_remote_sftp_provider_test COMMAND textlt_remote_sftp_provider_test)
add_test(NAME textlt_no_embedded_curl_test COMMAND textlt_no_embedded_curl_test)
add_test(NAME textlt_remote_dropbox_provider_test COMMAND textlt_remote_dropbox_provider_test)

add_test(NAME textlt_startup_check_test COMMAND ${CMAKE_COMMAND}
  "-Dexe=$<TARGET_FILE:textlt>"
  "-Dresources=${CMAKE_CURRENT_SOURCE_DIR}/text_processors"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_check.cmake")
