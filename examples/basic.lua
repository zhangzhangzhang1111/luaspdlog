local spdlog = require("luaspdlog")
local logger = spdlog.create("app", {
  level = "debug",
  sinks = {
    {type = "stdout", color = true},
    {type = "rotating", filename = "logs/app.log", max_size = 10 * 1024 * 1024, max_files = 3}
  }
})
logger:info("你好，Lua + spdlog！")
logger:debug(string.format("Lua 版本：%s，计数：%d", _VERSION, 42))
logger:warn("花括号 {原样输出}，不会解释为格式占位符")
spdlog.shutdown()
