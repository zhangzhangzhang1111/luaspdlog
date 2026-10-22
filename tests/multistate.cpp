extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

/** @brief 并发独立 Lua 状态使用同名日志器，检查注册表隔离及关闭时线程池排空。 */
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int n = 0; n < 4; ++n) threads.emplace_back([&] {
        for (int iteration = 0; iteration < 10; ++iteration) {
            lua_State *L = luaL_newstate();
            if (!L) { ++failures; return; }
            luaL_openlibs(L);
            lua_pushstring(L, argv[1]); lua_setglobal(L, "module_path");
            const char *script =
                "local open=assert(package.loadlib(module_path,'luaopen_luaspdlog')) "
                "local m=open() local l=m.create('same-name',{type='null',async=true}) "
                "for i=1,1000 do l:info('message') end "
                "assert(m.get('same-name'):name()=='same-name') ";
            if (luaL_loadstring(L, script) || lua_pcall(L, 0, 0, 0)) {
                std::fprintf(stderr, "%s\n", lua_tostring(L, -1)); ++failures;
            }
            lua_close(L);
        }
    });
    for (auto &t : threads) t.join();
    return failures == 0 ? 0 : 1;
}
