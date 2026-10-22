# luaspdlog v0.1.0

首次发布：将 spdlog 1.17.0 封装为 Lua 原生模块，支持 Lua 5.1–5.5 和 LuaJIT 2.1。

- 同步/异步、多输出目标、彩色终端、普通/滚动/每日文件、等级、pattern、回溯缓冲。
- Windows x86/x86_64/ARM64、macOS x86_64/ARM64、Linux glibc/musl x86_64/ARM64：每个 Lua ABI 单独打包，均运行集成测试、多 Lua 状态并发测试及安装后加载验证。
- Android 四种 ABI、iOS ARM64 设备及 ARM64/x86_64 模拟器：Lua 5.1–5.5 静态 SDK，完成交叉编译和宿主链接检查，未进行真机执行验证。移动端暂不发布 LuaJIT SDK。
- Linux 地址/未定义行为检测；包内附匹配运行时、许可证和构建 manifest。

下载时同时匹配操作系统、CPU、C 运行库和 Lua ABI。Linux glibc 包在 Ubuntu 22.04 构建（glibc ≥2.35）；musl 包在 Alpine 3.22 构建；macOS 最低 13；Windows 需要兼容的 MSVC 运行库。自定义 Lua 数值类型或其他 C API 编译选项请重新构建。

`SHA256SUMS` 用于校验附件；`BUILD-MANIFEST.json` 记录全部组合；`source-with-deps.tar.gz` 包含固定依赖源码，可离线编译。

异步 `flush()` 只提交刷新命令；退出前调用 `shutdown()` 等待队列排空。消息字符串内的花括号原样输出，格式化请使用 `string.format`。详细用法见仓库 README 与 docs/API.md。
