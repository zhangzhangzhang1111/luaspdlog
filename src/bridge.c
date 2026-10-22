#include "luaspdlog.h"
#include <lua.h>
#include <lauxlib.h>

/** @brief C++ 实现返回负值时，错误字符串已经位于 Lua 栈顶。 */
int luaspdlog_open_impl(lua_State *L);

/**
 * @brief 在纯 C 栈帧内转换 Lua 错误，避免 longjmp 穿过 C++ 异常处理帧。
 * @param L upvalue 1 为模块上下文，upvalue 2 为保存回调指针的 userdata。
 * @return 成功时返回 Lua 结果数；失败时调用 lua_error。
 */
int luaspdlog_dispatch(lua_State *L) {
    lua_CFunction *callback = (lua_CFunction *)lua_touserdata(L, lua_upvalueindex(2));
    int results = (*callback)(L);
    return results < 0 ? lua_error(L) : results;
}

LUASPDLOG_API int luaopen_luaspdlog(lua_State *L) {
#if LUA_VERSION_NUM >= 502
    luaL_checkversion(L);
#endif
    int results = luaspdlog_open_impl(L);
    return results < 0 ? lua_error(L) : results;
}
