#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "luaspdlog.h"

/** @brief 验证公共 C 头文件与静态 SDK 可被宿主链接；移动端仅编译链接。 */
int main(void) {
    lua_State *L = luaL_newstate();
    if (!L) return 1;
    luaL_openlibs(L);
    luaopen_luaspdlog(L);
    lua_setglobal(L, "spdlog");
    int status = luaL_dostring(L, "local l=spdlog.create('embedded',{type='null'}); l:info('ok'); spdlog.shutdown()");
    lua_close(L);
    return status != 0;
}
