--- spdlog 的 Lua 原生模块 API 描述。
-- 此文件供 LDoc 和编辑器读取，运行时应加载对应 ABI 的原生模块。
-- @module luaspdlog
---@meta
---@alias luaspdlog.Level 0|1|2|3|4|5|6|'trace'|'debug'|'info'|'warn'|'warning'|'error'|'err'|'critical'|'off'

---@class luaspdlog.Sink
---@field type? 'stdout'|'stderr'|'file'|'rotating'|'daily'|'null' 输出目标，默认 stdout。
---@field level? luaspdlog.Level 目标自身的过滤等级。
---@field color? boolean 终端颜色，默认 true。
---@field filename? string 文件目标必填，使用 UTF-8 路径。
---@field truncate? boolean 是否截断文件，默认 false。
---@field max_size? integer 滚动文件的最大字节数。
---@field max_files? integer 保留文件数，daily 的 0 表示不限。
---@field rotate_on_open? boolean 打开时是否滚动。
---@field hour? integer 每日滚动小时，0–23。
---@field minute? integer 每日滚动分钟，0–59。

---@class luaspdlog.Options: luaspdlog.Sink
---@field sinks? luaspdlog.Sink[] 1–64 个连续的目标配置。
---@field pattern? string spdlog 的输出格式。
---@field flush_on? luaspdlog.Level 自动刷新等级。
---@field async? boolean 是否使用当前模块实例的异步线程池。
---@field overflow? 'block'|'overrun_oldest' 队满时等待或覆盖旧消息。

--- 日志器句柄；close 和 shutdown 后不可继续写入。
-- @type Logger
---@class luaspdlog.Logger
local Logger = {}

--- 写入指定等级的原始字符串，保留 NUL 及花括号。
-- @param level 日志等级。
-- @param message 消息字符串。
---@param level luaspdlog.Level
---@param message string
function Logger:log(level, message) end
--- 写入 trace 消息。
---@param message string
function Logger:trace(message) end
--- 写入 debug 消息。
---@param message string
function Logger:debug(message) end
--- 写入 info 消息。
---@param message string
function Logger:info(message) end
--- 写入 warn 消息。
---@param message string
function Logger:warn(message) end
--- 写入 error 消息。
---@param message string
function Logger:error(message) end
--- 写入 critical 消息。
---@param message string
function Logger:critical(message) end
--- 获取日志器名字。
-- @return 名字。
---@return string
function Logger:name() end
--- 设置日志等级。
---@param level luaspdlog.Level
function Logger:set_level(level) end
--- 获取数值形式的日志等级。
---@return integer
function Logger:level() end
--- 检查日志器是否允许该等级，不包含 sink 的等级过滤。
---@param level luaspdlog.Level
---@return boolean
function Logger:should_log(level) end
--- 设置全部输出目标的格式和时区。
---@param pattern string
---@param timezone? 'local'|'utc'
function Logger:set_pattern(pattern, timezone) end
--- 同步刷新；异步日志器仅将刷新命令入队。
function Logger:flush() end
--- 设置自动刷新阈值。
---@param level luaspdlog.Level
function Logger:flush_on(level) end
--- 启用回溯缓冲，包含被等级过滤的消息。
---@param size integer
function Logger:enable_backtrace(size) end
--- 停用回溯缓冲。
function Logger:disable_backtrace() end
--- 输出回溯缓冲。
function Logger:dump_backtrace() end
--- 返回后台/写入错误；clear 为 true 时同时清空。
---@param clear? boolean
---@return string?
function Logger:last_error(clear) end
--- 关闭日志器，所有共享句柄失效；可重复调用。
function Logger:close() end

---@class luaspdlog.Module
---@field _VERSION string
---@field _SPDLOG_VERSION string
---@field _LUA_VERSION string
---@field level table<string, integer>
local M = {}

--- 创建并注册日志器；首个日志器成为默认日志器。
-- @param name 非空且唯一的名字。
-- @param[opt] options 日志器及目标配置。
-- @return 日志器句柄。
---@param name string
---@param options? luaspdlog.Options
---@return luaspdlog.Logger
function M.create(name, options) end
--- 按名字查找日志器。
---@param name string
---@return luaspdlog.Logger?
function M.get(name) end
--- 从注册表移除，现有句柄继续有效。
---@param name string
---@return boolean
function M.drop(name) end
--- 清空注册表，现有句柄继续有效。
function M.drop_all() end
--- 设置当前模块的默认日志器。
---@param logger luaspdlog.Logger
function M.set_default(logger) end
--- 取得默认日志器。
---@return luaspdlog.Logger?
function M.get_default() end
--- 创建独立线程池，初始化后需 shutdown 才能重建。
---@param queue_size integer
---@param workers? integer 默认 1。
function M.init_thread_pool(queue_size, workers) end
--- 关闭全部日志器，等待后台日志写完；之后可重新创建。
function M.shutdown() end
--- 使用默认日志器输出。
---@param level luaspdlog.Level
---@param message string
function M.log(level, message) end
--- 默认日志器输出 trace。
---@param message string
function M.trace(message) end
--- 默认日志器输出 debug。
---@param message string
function M.debug(message) end
--- 默认日志器输出 info。
---@param message string
function M.info(message) end
--- 默认日志器输出 warn。
---@param message string
function M.warn(message) end
--- 默认日志器输出 error。
---@param message string
function M.error(message) end
--- 默认日志器输出 critical。
---@param message string
function M.critical(message) end
return M
