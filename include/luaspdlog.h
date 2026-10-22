#ifndef LUASPDLOG_H
#define LUASPDLOG_H

typedef struct lua_State lua_State;

#if defined(_WIN32) && !defined(LUASPDLOG_STATIC)
#define LUASPDLOG_API __declspec(dllexport)
#elif defined(__GNUC__)
#define LUASPDLOG_API __attribute__((visibility("default")))
#else
#define LUASPDLOG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 将 spdlog 模块压入 Lua 栈，供 require 或 luaL_requiref 使用。
 * @param L 当前 Lua 状态；必须与构建时的 Lua ABI 一致。
 * @return 返回一个模块表。静态链接的宿主可通过此入口注册模块。
 * @note 每个 Lua 状态独立保存默认日志器及异步线程池。
 */
LUASPDLOG_API int luaopen_luaspdlog(lua_State *L);
#ifdef __cplusplus
}
#endif
#endif
