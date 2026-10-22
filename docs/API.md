# luaspdlog API

## 模块函数

| API | 说明 |
|---|---|
| `create(name [, options]) -> logger` | 名字非空且注册表中唯一；首个创建者成为默认日志器 |
| `get(name) -> logger 或 nil` | 返回新句柄，引用同一个日志器 |
| `drop(name) -> boolean` | 从注册表移除；现有句柄和默认日志器继续有效 |
| `drop_all()` | 清空注册表；现有句柄继续有效 |
| `set_default(logger)` / `get_default()` | 设置/读取当前模块的默认日志器 |
| `trace/debug/info/warn/error/critical(message)` | 用默认日志器输出原始字符串 |
| `log(level, message)` | 指定等级输出 |
| `init_thread_pool(queue_size [, workers=1])` | 建立该模块独立的池；队列 1–1048576，线程 1–64 |
| `shutdown()` | 关闭该模块创建的全部日志器（包含已 drop 的句柄），排空并等待后台线程；可重复调用，之后可创建新日志器 |

模块常量：`_VERSION`、`_SPDLOG_VERSION`、`_LUA_VERSION`。等级表 `level`：trace=0、debug=1、info=2、warn=3、error=4、critical=5、off=6。等级参数接受这些整数或对应字符串，另外接受 `warning` 和 `err`。

每次模块实例保存独立的注册表和线程池，不使用 spdlog 全局注册表。不同 Lua 状态可以使用同名日志器。Lua 状态自身仍须遵循 Lua 的线程约束；不能从多个原生线程同时调用同一个状态。

## 创建选项

```lua
local log = require("luaspdlog")
local logger = log.create("app", {
  type = "rotating", filename = "logs/app.log",
  max_size = 10485760, max_files = 3,
  level = "info", flush_on = "warn",
  pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
  async = true, overflow = "block"
})
```

| 字段 | 默认值及约束 |
|---|---|
| `level` | `info`，日志器过滤等级 |
| `flush_on` | `off`，达到指定等级后刷新 |
| `pattern` | 示例中的时间/名字/等级/消息格式；使用 spdlog pattern 语法 |
| `async` | `false`；异步模式默认延迟建立 8192 队列、1 工作线程的池 |
| `overflow` | `block`（队满等待），或 `overrun_oldest`（覆盖旧消息，可能丢日志） |
| `sinks` | 不提供时当前 options 同时作为单个 sink 的配置；提供时为 1–64 项的连续数组 |

`sinks` 数组中的每个目标支持独立的 `level`（默认 trace）；顶层 `level` 只控制日志器过滤。单目标的 sink 保持 trace，使之后的 `set_level` 能自由调整日志器等级。

| sink `type` | 字段 |
|---|---|
| `stdout`（默认）、`stderr` | `color=true`，控制终端着色 |
| `file` | 必填 `filename`；`truncate=false`，默认追加 |
| `rotating` | 必填 `filename`；`max_size=10485760`（字节，1–1 TiB，且不超过目标的 `size_t` 上限），`max_files=3`（1–200000），`rotate_on_open=false` |
| `daily` | 必填 `filename`；`hour=0`（0–23），`minute=0`（0–59），`truncate=false`，`max_files=0`（0–65535，0 表示不限制保留数量） |
| `null` | 丢弃输出，适合基准和条件禁用 |

文件目录由 spdlog 创建；daily 文件名自动插入日期。日志轮转由写入触发，不启动每日定时器。文件和时间行为遵循 spdlog。Windows 文件 API 使用宽字符串，路径须为有效 UTF-8；模块不改变进程代码页。

## 日志器方法

| API | 说明 |
|---|---|
| `logger:log(level, message)` | 按指定等级输出 |
| `logger:trace/debug/info/warn/error/critical(message)` | 消息须为字符串；支持嵌入 NUL，花括号原样输出 |
| `logger:name() -> string` | 日志器名字 |
| `logger:set_level(level)` / `logger:level() -> integer` | 设置/读取日志等级 |
| `logger:should_log(level) -> boolean` | 检查日志器等级过滤（不包含各 sink 的等级） |
| `logger:set_pattern(pattern [, timezone="local"])` | 所有目标使用该 pattern；时区为 `local` 或 `utc` |
| `logger:flush()` | 同步日志器立即刷新；异步日志器仅将刷新命令入队 |
| `logger:flush_on(level)` | 设置自动刷新阈值 |
| `logger:enable_backtrace(size)` | 保存最近 size 条日志，1–1048576，包含被等级过滤的消息 |
| `logger:disable_backtrace()` | 停用回溯缓冲 |
| `logger:dump_backtrace()` | 输出回溯缓冲内容 |
| `logger:last_error([clear=false]) -> string 或 nil` | 读取 spdlog 后台或写入错误；传 true 同时清空 |
| `logger:close()` | 关闭底层日志器，所有指向同一日志器的句柄失效；可重复调用 |

`close()` 不自动从名字注册表移除；需要重用名字时先 `drop(name)`。异步模式 `close()` 不保证池中命令全部完成，应在退出前 `shutdown()`。Lua 5.4/5.5 支持 `<close>` 局部变量，其语义与 `close()` 一致。

无效参数及初始化失败抛出 Lua 错误，可使用 `pcall` 捕获。spdlog 的写入错误存入 `last_error()`，不会从后台线程触发 Lua 回调或终止进程；需要可靠送达的应用应选择 `block`、单工作线程，刷新并检查错误，然后关闭。`shutdown()` 之后旧句柄不可继续查询错误。进程强制退出、磁盘故障、`overrun_oldest` 或多个工作线程可能导致丢失/乱序；不承诺事务或断电持久性。Lua 内存耗尽属于宿主 Lua C API 的致命分配失败路径。
