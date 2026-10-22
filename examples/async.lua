local spdlog = require("luaspdlog")
spdlog.init_thread_pool(8192, 1)
local logger = spdlog.create("worker", {
  type = "file", filename = "logs/async.log", async = true, overflow = "block"
})
for i = 1, 10000 do logger:info("事件 " .. i) end
spdlog.shutdown() -- 等待后台日志写完；flush 本身只入队。
