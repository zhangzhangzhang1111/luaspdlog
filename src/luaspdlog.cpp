#include "luaspdlog.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <spdlog/spdlog.h>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <map>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int luaspdlog_dispatch(lua_State *L);

namespace {
constexpr const char *logger_mt = "luaspdlog.logger";
constexpr const char *state_mt = "luaspdlog.state";
using Level = spdlog::level::level_enum;

// 参数校验使用 C++ 异常；先展开所有 RAII 对象，再在最外层触发 Lua 错误。
[[noreturn]] void fail(const std::string &s) { throw std::runtime_error(s); }
std::string string_arg(lua_State *L, int i) {
    if (lua_type(L, i) != LUA_TSTRING) fail("expected string at argument " + std::to_string(i));
    size_t n = 0;
    const char *p = lua_tolstring(L, i, &n);
    return std::string(p, n);
}
std::string text_arg(lua_State *L, int i) {
    auto s = string_arg(L, i);
    if (s.find('\0') != std::string::npos) fail("embedded NUL is not allowed in names, paths or patterns");
    return s;
}
bool is_nil(lua_State *L, int i) { return lua_isnoneornil(L, i) != 0; }
double number_arg(lua_State *L, int i, double lo, double hi) {
    if (lua_type(L, i) != LUA_TNUMBER) fail("expected number at argument " + std::to_string(i));
    double n = static_cast<double>(lua_tonumber(L, i));
    if (!std::isfinite(n) || n < lo || n > hi || std::floor(n) != n) fail("integer out of range");
    return n;
}
Level level_arg(lua_State *L, int i) {
    if (lua_type(L, i) == LUA_TNUMBER) return static_cast<Level>(static_cast<int>(number_arg(L, i, 0, 6)));
    auto s = string_arg(L, i);
    if (s == "warn") s = "warning";
    if (s == "err") s = "error";
    const char *names[] = {"trace", "debug", "info", "warning", "error", "critical", "off"};
    for (int j = 0; j < 7; ++j) if (s == names[j]) return static_cast<Level>(j);
    fail("unknown log level: " + s);
}
int absolute(lua_State *L, int i) { return i > 0 ? i : lua_gettop(L) + i + 1; }
void raw_field(lua_State *L, int t, const char *key) {
    t = absolute(L, t);
    // 配置读取不触发 __index，避免用户代码的 Lua 错误跨越 C++ RAII 栈帧。
    lua_pushstring(L, key);
    lua_rawget(L, t);
}
std::string field_string(lua_State *L, int t, const char *key, const std::string &fallback) {
    raw_field(L, t, key);
    auto value = is_nil(L, -1) ? fallback : text_arg(L, -1);
    lua_pop(L, 1);
    return value;
}
double field_number(lua_State *L, int t, const char *key, double fallback, double lo, double hi) {
    raw_field(L, t, key);
    auto value = is_nil(L, -1) ? fallback : number_arg(L, -1, lo, hi);
    lua_pop(L, 1);
    return value;
}
bool field_bool(lua_State *L, int t, const char *key, bool fallback) {
    raw_field(L, t, key);
    if (!is_nil(L, -1) && lua_type(L, -1) != LUA_TBOOLEAN) fail(std::string(key) + " must be boolean");
    bool value = is_nil(L, -1) ? fallback : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}
Level field_level(lua_State *L, int t, const char *key, Level fallback) {
    raw_field(L, t, key);
    auto value = is_nil(L, -1) ? fallback : level_arg(L, -1);
    lua_pop(L, 1);
    return value;
}
struct Errors {
    std::mutex mutex;
    std::string message;
};
spdlog::filename_t native_filename(const std::string &filename) {
#ifdef _WIN32
    // Windows 使用宽字符文件 API，使 UTF-8 路径不依赖进程活动代码页。
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename.c_str(), -1, nullptr, 0);
    if (n == 0) fail("filename is not valid UTF-8");
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename.c_str(), -1, &wide[0], n);
    wide.resize(static_cast<size_t>(n - 1));
    return wide;
#else
    return filename;
#endif
}
struct Record {
    std::shared_ptr<spdlog::logger> logger;
    // async_logger 仅弱引用线程池，句柄必须延长线程池寿命。
    std::shared_ptr<spdlog::details::thread_pool> pool;
    std::shared_ptr<Errors> errors;
};
struct State {
    std::map<std::string, std::shared_ptr<Record>> registry;
    std::vector<std::weak_ptr<Record>> records;
    std::shared_ptr<Record> default_logger;
    std::shared_ptr<spdlog::details::thread_pool> pool;
    size_t queue_size = 8192;
    size_t workers = 1;
    void shutdown() {
        for (auto &weak : records) if (auto r = weak.lock()) {
            if (r->logger) r->logger->flush();
            r->logger.reset();
            r->pool.reset();
        }
        registry.clear();
        default_logger.reset();
        // 销毁线程池会等待排队日志写完；后台线程从不访问 Lua。
        pool.reset();
        records.clear();
    }
    ~State() { try { shutdown(); } catch (...) {} }
};
template<class T> struct Box { T *value; };
template<class T> Box<T> *box(lua_State *L, int i, const char *mt) {
    if (lua_type(L, i) != LUA_TUSERDATA || !lua_getmetatable(L, i)) fail("invalid userdata");
    luaL_getmetatable(L, mt);
    bool match = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    if (!match) fail("invalid userdata type");
    return static_cast<Box<T> *>(lua_touserdata(L, i));
}
State &state(lua_State *L) {
    auto *b = box<State>(L, lua_upvalueindex(1), state_mt);
    if (!b->value) fail("module has been finalized");
    return *b->value;
}
std::shared_ptr<Record> &record(lua_State *L, int i = 1) {
    auto *b = box<std::shared_ptr<Record>>(L, i, logger_mt);
    if (!b->value || !(*b->value)->logger) fail("logger is closed");
    return *b->value;
}
void push_record(lua_State *L, const std::shared_ptr<Record> &r) {
    auto *b = static_cast<Box<std::shared_ptr<Record>> *>(lua_newuserdata(L, sizeof(Box<std::shared_ptr<Record>>)));
    b->value = nullptr;
    luaL_getmetatable(L, logger_mt);
    lua_setmetatable(L, -2);
    b->value = new std::shared_ptr<Record>(r);
}
// __gc 可被重复调用；清空指针避免重复析构。普通 API 不暴露元表。
int logger_gc(lua_State *L) {
    auto *b = box<std::shared_ptr<Record>>(L, 1, logger_mt);
    delete b->value;
    b->value = nullptr;
    return 0;
}
int state_gc(lua_State *L) {
    auto *b = box<State>(L, 1, state_mt);
    delete b->value;
    b->value = nullptr;
    return 0;
}
spdlog::sink_ptr make_sink(lua_State *L, int i, bool nested = true) {
    if (!lua_istable(L, i)) fail("sink configuration must be a table");
    int t = absolute(L, i);
    auto type = field_string(L, t, "type", "stdout");
    spdlog::sink_ptr sink;
    if (type == "stdout" || type == "stderr") {
        bool color = field_bool(L, t, "color", true);
        if (type == "stdout") {
            if (color) sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            else sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
        } else {
            if (color) sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            else sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        }
    } else if (type == "null") sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    else {
        auto filename = field_string(L, t, "filename", "");
        if (filename.empty()) fail("file sink requires filename");
        auto path = native_filename(filename);
        bool truncate = field_bool(L, t, "truncate", false);
        if (type == "file") sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, truncate);
        else if (type == "rotating") {
            auto limit = std::min(1099511627776.0, static_cast<double>(std::numeric_limits<size_t>::max()));
            auto size = static_cast<size_t>(field_number(L, t, "max_size", 10485760, 1, limit));
            auto files = static_cast<size_t>(field_number(L, t, "max_files", 3, 1, 200000));
            sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, size, files, field_bool(L, t, "rotate_on_open", false));
        } else if (type == "daily") {
            int h = static_cast<int>(field_number(L, t, "hour", 0, 0, 23));
            int m = static_cast<int>(field_number(L, t, "minute", 0, 0, 59));
            auto files = static_cast<uint16_t>(field_number(L, t, "max_files", 0, 0, 65535));
            sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(path, h, m, truncate, files);
        } else fail("unknown sink type: " + type);
    }
    // 顶层 level 只控制日志器，避免单目标配置阻止后续下调日志等级。
    sink->set_level(nested ? field_level(L, t, "level", spdlog::level::trace) : spdlog::level::trace);
    return sink;
}
int create(lua_State *L) {
    auto &s = state(L);
    auto name = text_arg(L, 1);
    if (name.empty()) fail("logger name must not be empty");
    if (s.registry.count(name)) fail("logger already exists: " + name);
    if (!is_nil(L, 2) && !lua_istable(L, 2)) fail("options must be a table");
    if (is_nil(L, 2)) { lua_settop(L, 2); lua_newtable(L); lua_replace(L, 2); }
    bool async = field_bool(L, 2, "async", false);
    auto overflow = field_string(L, 2, "overflow", "block");
    if (overflow != "block" && overflow != "overrun_oldest") fail("invalid overflow policy");
    auto level = field_level(L, 2, "level", spdlog::level::info);
    auto flush = field_level(L, 2, "flush_on", spdlog::level::off);
    auto pattern = field_string(L, 2, "pattern", "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    std::vector<spdlog::sink_ptr> sinks;
    raw_field(L, 2, "sinks");
    if (lua_isnil(L, -1)) sinks.push_back(make_sink(L, 2, false));
    else {
        if (!lua_istable(L, -1)) fail("sinks must be a nonempty array");
        int t = absolute(L, -1);
        // 最多 64 个目标，防止错误配置无界分配。
        for (int j = 1; j <= 65; ++j) {
            lua_rawgeti(L, t, j);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
            if (j == 65) fail("at most 64 sinks are allowed");
            sinks.push_back(make_sink(L, -1));
            lua_pop(L, 1);
        }
        if (sinks.empty()) fail("sinks must be a nonempty array");
    }
    lua_pop(L, 1);
    auto r = std::make_shared<Record>();
    r->errors = std::make_shared<Errors>();
    if (async) {
        if (!s.pool) s.pool = std::make_shared<spdlog::details::thread_pool>(s.queue_size, s.workers);
        r->pool = s.pool;
        auto policy = overflow == "block" ? spdlog::async_overflow_policy::block : spdlog::async_overflow_policy::overrun_oldest;
        r->logger = std::make_shared<spdlog::async_logger>(name, sinks.begin(), sinks.end(), s.pool, policy);
    } else r->logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    r->logger->set_level(level);
    r->logger->flush_on(flush);
    r->logger->set_pattern(pattern);
    auto errors = r->errors;
    r->logger->set_error_handler([errors](const std::string &msg) {
        std::lock_guard<std::mutex> lock(errors->mutex);
        errors->message = msg;
    });
    s.registry.emplace(name, r);
    s.records.erase(std::remove_if(s.records.begin(), s.records.end(), [](const auto &w) { return w.expired(); }), s.records.end());
    s.records.push_back(r);
    if (!s.default_logger) s.default_logger = r;
    push_record(L, r);
    return 1;
}
int get(lua_State *L) {
    auto &s = state(L);
    auto it = s.registry.find(text_arg(L, 1));
    if (it == s.registry.end() || !it->second->logger) lua_pushnil(L);
    else push_record(L, it->second);
    return 1;
}
int drop(lua_State *L) {
    auto &s = state(L);
    lua_pushboolean(L, s.registry.erase(text_arg(L, 1)) != 0);
    return 1;
}
int drop_all(lua_State *L) { state(L).registry.clear(); return 0; }
int set_default(lua_State *L) { state(L).default_logger = record(L); return 0; }
int get_default(lua_State *L) {
    auto r = state(L).default_logger;
    if (r && r->logger) push_record(L, r); else lua_pushnil(L);
    return 1;
}
int init_pool(lua_State *L) {
    auto &s = state(L);
    if (s.pool) fail("thread pool already initialized; call shutdown first");
    s.queue_size = static_cast<size_t>(number_arg(L, 1, 1, 1048576));
    s.workers = is_nil(L, 2) ? 1 : static_cast<size_t>(number_arg(L, 2, 1, 64));
    s.pool = std::make_shared<spdlog::details::thread_pool>(s.queue_size, s.workers);
    return 0;
}
int shutdown(lua_State *L) { state(L).shutdown(); return 0; }
void write(lua_State *L, const std::shared_ptr<Record> &r, Level level, int i) {
    size_t size = 0;
    if (lua_type(L, i) != LUA_TSTRING) fail("log message must be a string");
    const char *message = lua_tolstring(L, i, &size);
    // 显式使用 string_view 重载，保留 NUL，并防止消息内花括号被当作 fmt 格式串。
    r->logger->log(level, spdlog::string_view_t(message, size));
}
int log_message(lua_State *L) { auto r = record(L); auto level = level_arg(L, 2); write(L, r, level, 3); return 0; }
template<int N> int log_level(lua_State *L) { write(L, record(L), static_cast<Level>(N), 2); return 0; }
template<int N> int default_level(lua_State *L) {
    auto r = state(L).default_logger;
    if (!r || !r->logger) fail("no default logger; call create or set_default first");
    write(L, r, static_cast<Level>(N), 1);
    return 0;
}
int default_log(lua_State *L) {
    auto r = state(L).default_logger;
    if (!r || !r->logger) fail("no default logger");
    auto level = level_arg(L, 1);
    write(L, r, level, 2);
    return 0;
}
int set_level(lua_State *L) { record(L)->logger->set_level(level_arg(L, 2)); return 0; }
int get_level(lua_State *L) { lua_pushinteger(L, record(L)->logger->level()); return 1; }
int should_log(lua_State *L) { lua_pushboolean(L, record(L)->logger->should_log(level_arg(L, 2))); return 1; }
int set_pattern(lua_State *L) {
    auto r = record(L);
    auto pattern = text_arg(L, 2);
    auto zone = is_nil(L, 3) ? std::string("local") : text_arg(L, 3);
    if (zone != "local" && zone != "utc") fail("timezone must be local or utc");
    r->logger->set_pattern(pattern, zone == "utc" ? spdlog::pattern_time_type::utc : spdlog::pattern_time_type::local);
    return 0;
}
int flush(lua_State *L) { record(L)->logger->flush(); return 0; }
int flush_on(lua_State *L) { record(L)->logger->flush_on(level_arg(L, 2)); return 0; }
int name(lua_State *L) { auto r = record(L); auto &n = r->logger->name(); lua_pushlstring(L, n.data(), n.size()); return 1; }
int close(lua_State *L) {
    auto *b = box<std::shared_ptr<Record>>(L, 1, logger_mt);
    if (b->value && (*b->value)->logger) {
        (*b->value)->logger->flush();
        (*b->value)->logger.reset();
        (*b->value)->pool.reset();
    }
    return 0;
}
int last_error(lua_State *L) {
    auto r = record(L);
    std::string message;
    {
        std::lock_guard<std::mutex> lock(r->errors->mutex);
        message = r->errors->message;
        if (lua_toboolean(L, 2)) r->errors->message.clear();
    }
    if (message.empty()) lua_pushnil(L); else lua_pushlstring(L, message.data(), message.size());
    return 1;
}
int enable_backtrace(lua_State *L) { record(L)->logger->enable_backtrace(static_cast<size_t>(number_arg(L, 2, 1, 1048576))); return 0; }
int disable_backtrace(lua_State *L) { record(L)->logger->disable_backtrace(); return 0; }
int dump_backtrace(lua_State *L) { record(L)->logger->dump_backtrace(); return 0; }
// 只返回错误标记；lua_error 始终在 bridge.c 中执行，C++ 栈帧已全部返回。
template<int (*F)(lua_State *)> int safe(lua_State *L) {
    try { return F(L); }
    catch (const std::exception &e) { lua_pushstring(L, e.what()); }
    catch (...) { lua_pushliteral(L, "unknown C++ error"); }
    return -1;
}
const luaL_Reg methods[] = {
    {"log", safe<log_message>}, {"trace", safe<log_level<0>>}, {"debug", safe<log_level<1>>},
    {"info", safe<log_level<2>>}, {"warn", safe<log_level<3>>}, {"error", safe<log_level<4>>},
    {"critical", safe<log_level<5>>}, {"set_level", safe<set_level>}, {"level", safe<get_level>},
    {"should_log", safe<should_log>}, {"set_pattern", safe<set_pattern>}, {"flush", safe<flush>},
    {"flush_on", safe<flush_on>}, {"name", safe<name>}, {"close", safe<close>},
    {"last_error", safe<last_error>}, {"enable_backtrace", safe<enable_backtrace>},
    {"disable_backtrace", safe<disable_backtrace>}, {"dump_backtrace", safe<dump_backtrace>}, {nullptr, nullptr}
};
const luaL_Reg functions[] = {
    {"create", safe<create>}, {"get", safe<get>}, {"drop", safe<drop>}, {"drop_all", safe<drop_all>},
    {"set_default", safe<set_default>}, {"get_default", safe<get_default>},
    {"init_thread_pool", safe<init_pool>}, {"shutdown", safe<shutdown>}, {"log", safe<default_log>},
    {"trace", safe<default_level<0>>}, {"debug", safe<default_level<1>>}, {"info", safe<default_level<2>>},
    {"warn", safe<default_level<3>>}, {"error", safe<default_level<4>>}, {"critical", safe<default_level<5>>}, {nullptr, nullptr}
};
void push_callback(lua_State *L, lua_CFunction function, int context = 0) {
    if (context) lua_pushvalue(L, context); else lua_pushnil(L);
    auto *callback = static_cast<lua_CFunction *>(lua_newuserdata(L, sizeof(lua_CFunction)));
    *callback = function;
    lua_pushcclosure(L, luaspdlog_dispatch, 2);
}
void register_functions(lua_State *L, const luaL_Reg *entries) {
    for (; entries->name; ++entries) { push_callback(L, entries->func); lua_setfield(L, -2, entries->name); }
}
int open(lua_State *L) {
    luaL_newmetatable(L, logger_mt);
    push_callback(L, safe<logger_gc>); lua_setfield(L, -2, "__gc");
    push_callback(L, safe<close>); lua_setfield(L, -2, "__close");
    lua_pushliteral(L, "luaspdlog.logger"); lua_setfield(L, -2, "__metatable");
    lua_newtable(L); register_functions(L, methods); lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    luaL_newmetatable(L, state_mt);
    push_callback(L, safe<state_gc>); lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);
    auto *b = static_cast<Box<State> *>(lua_newuserdata(L, sizeof(Box<State>)));
    b->value = nullptr;
    luaL_getmetatable(L, state_mt); lua_setmetatable(L, -2);
    b->value = new State();
    int context = lua_gettop(L);
    lua_newtable(L);
    for (auto *f = functions; f->name; ++f) {
        push_callback(L, f->func, context); lua_setfield(L, -2, f->name);
    }
    lua_pushliteral(L, "0.1.0"); lua_setfield(L, -2, "_VERSION");
    lua_pushliteral(L, "1.17.0"); lua_setfield(L, -2, "_SPDLOG_VERSION");
    lua_pushliteral(L, LUA_VERSION); lua_setfield(L, -2, "_LUA_VERSION");
    lua_newtable(L);
    const char *levels[] = {"trace", "debug", "info", "warn", "error", "critical", "off"};
    for (int i = 0; i < 7; ++i) { lua_pushinteger(L, i); lua_setfield(L, -2, levels[i]); }
    lua_setfield(L, -2, "level");
    return 1;
}
} // namespace

extern "C" int luaspdlog_open_impl(lua_State *L) { return safe<open>(L); }
