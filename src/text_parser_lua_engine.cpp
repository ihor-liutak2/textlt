#include "text_parser_lua_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace textlt {
namespace {

struct LuaMemoryTracker {
  std::size_t used_bytes = 0;
  std::size_t max_bytes = 0;
  bool allocation_failed = false;
};

struct LuaRunContext {
  std::chrono::steady_clock::time_point started_at;
  int timeout_ms = 0;
};

void* LimitedLuaAllocator(void* user_data, void* ptr, std::size_t old_size,
                          std::size_t new_size) {
  auto* tracker = static_cast<LuaMemoryTracker*>(user_data);

  if (new_size == 0) {
    if (ptr != nullptr) {
      tracker->used_bytes -= std::min(tracker->used_bytes, old_size);
      std::free(ptr);
    }
    return nullptr;
  }

  const std::size_t used_without_old =
      tracker->used_bytes - std::min(tracker->used_bytes, old_size);

  if (new_size > tracker->max_bytes ||
      used_without_old > tracker->max_bytes - new_size) {
    tracker->allocation_failed = true;
    return nullptr;
  }

  void* new_ptr = std::realloc(ptr, new_size);
  if (new_ptr == nullptr) {
    tracker->allocation_failed = true;
    return nullptr;
  }

  tracker->used_bytes = used_without_old + new_size;
  return new_ptr;
}

void TimeoutHook(lua_State* state, lua_Debug*) {
  auto** context_ptr = static_cast<LuaRunContext**>(lua_getextraspace(state));
  if (context_ptr == nullptr || *context_ptr == nullptr) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - (*context_ptr)->started_at)
                              .count();

  if (elapsed_ms > (*context_ptr)->timeout_ms) {
    luaL_error(state, "Lua parser timeout");
  }
}

void RequireLibrary(lua_State* state, const char* name, lua_CFunction open_func) {
  luaL_requiref(state, name, open_func, 1);
  lua_pop(state, 1);
}

void OpenAllowedLibraries(lua_State* state) {
  RequireLibrary(state, LUA_GNAME, luaopen_base);
  RequireLibrary(state, LUA_STRLIBNAME, luaopen_string);
  RequireLibrary(state, LUA_TABLIBNAME, luaopen_table);
  RequireLibrary(state, LUA_MATHLIBNAME, luaopen_math);
  RequireLibrary(state, LUA_UTF8LIBNAME, luaopen_utf8);
}

void CopyGlobalToTable(lua_State* state, int table_index, const char* name) {
  table_index = lua_absindex(state, table_index);
  lua_getglobal(state, name);
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_setfield(state, table_index, name);
}

void PushSandboxEnvironment(lua_State* state) {
  lua_newtable(state);
  const int env_index = lua_gettop(state);

  CopyGlobalToTable(state, env_index, "string");
  CopyGlobalToTable(state, env_index, "table");
  CopyGlobalToTable(state, env_index, "math");
  CopyGlobalToTable(state, env_index, "utf8");

  CopyGlobalToTable(state, env_index, "assert");
  CopyGlobalToTable(state, env_index, "error");
  CopyGlobalToTable(state, env_index, "ipairs");
  CopyGlobalToTable(state, env_index, "next");
  CopyGlobalToTable(state, env_index, "pairs");
  CopyGlobalToTable(state, env_index, "pcall");
  CopyGlobalToTable(state, env_index, "select");
  CopyGlobalToTable(state, env_index, "tonumber");
  CopyGlobalToTable(state, env_index, "tostring");
  CopyGlobalToTable(state, env_index, "type");

  lua_pushliteral(state, "Lua parser sandbox");
  lua_setfield(state, env_index, "_VERSION");
}

void PushParamsTable(lua_State* state,
                     const std::unordered_map<std::string, std::string>& params) {
  lua_newtable(state);
  const int table_index = lua_gettop(state);

  for (const auto& item : params) {
    lua_pushlstring(state, item.second.data(), item.second.size());
    lua_setfield(state, table_index, item.first.c_str());
  }
}

std::string ReadLuaError(lua_State* state, const char* fallback_message) {
  const char* message = lua_tostring(state, -1);
  if (message == nullptr || std::strlen(message) == 0) {
    return fallback_message;
  }
  return message;
}

}  // namespace

LuaParserRunResult TextParserLuaEngine::RunScript(
    const std::string& script_text, const std::string& input_text,
    const std::unordered_map<std::string, std::string>& params,
    const LuaParserLimits& limits) const {
  LuaParserRunResult result;

  if (script_text.empty()) {
    result.error = "Lua parser script is empty.";
    return result;
  }

  if (limits.timeout_ms <= 0) {
    result.error = "Lua parser timeout must be greater than zero.";
    return result;
  }

  if (limits.max_memory_bytes == 0 || limits.max_output_bytes == 0) {
    result.error = "Lua parser memory and output limits must be greater than zero.";
    return result;
  }

  LuaMemoryTracker memory_tracker;
  memory_tracker.max_bytes = limits.max_memory_bytes;

  lua_State* state = lua_newstate(LimitedLuaAllocator, &memory_tracker);
  if (state == nullptr) {
    result.error = "Cannot create Lua state.";
    return result;
  }

  LuaRunContext run_context;
  run_context.started_at = std::chrono::steady_clock::now();
  run_context.timeout_ms = limits.timeout_ms;
  *static_cast<LuaRunContext**>(lua_getextraspace(state)) = &run_context;

  const int hook_count = std::max(1, limits.instruction_hook_count);
  lua_sethook(state, TimeoutHook, LUA_MASKCOUNT, hook_count);

  OpenAllowedLibraries(state);
  PushSandboxEnvironment(state);
  const int env_index = lua_gettop(state);

  if (luaL_loadbufferx(state, script_text.data(), script_text.size(),
                       "textlt_parser", "t") != LUA_OK) {
    result.error = ReadLuaError(state, "Cannot load Lua parser script.");
    lua_close(state);
    return result;
  }

  lua_pushvalue(state, env_index);
  if (lua_setupvalue(state, -2, 1) == nullptr) {
    result.error = "Cannot assign Lua parser sandbox environment.";
    lua_close(state);
    return result;
  }

  if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
    result.error = ReadLuaError(state, "Cannot initialize Lua parser script.");
    lua_close(state);
    return result;
  }

  lua_getfield(state, env_index, "transform");
  if (!lua_isfunction(state, -1)) {
    result.error = "Lua parser must define function transform(text, params).";
    lua_close(state);
    return result;
  }

  lua_pushlstring(state, input_text.data(), input_text.size());
  PushParamsTable(state, params);

  if (lua_pcall(state, 2, 1, 0) != LUA_OK) {
    result.error = ReadLuaError(state, "Lua parser execution failed.");
    lua_close(state);
    return result;
  }

  if (!lua_isstring(state, -1)) {
    result.error = "Lua parser transform(text, params) must return text.";
    lua_close(state);
    return result;
  }

  std::size_t output_size = 0;
  const char* output = lua_tolstring(state, -1, &output_size);
  if (output == nullptr) {
    result.error = "Lua parser returned invalid text.";
    lua_close(state);
    return result;
  }

  if (output_size > limits.max_output_bytes) {
    result.error = "Lua parser result is too large.";
    lua_close(state);
    return result;
  }

  result.text.assign(output, output_size);
  result.success = true;

  lua_close(state);
  return result;
}

}  // namespace textlt
