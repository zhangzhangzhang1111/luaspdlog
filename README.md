# luaspdlog

[![Build](https://github.com/zhangzhangzhang1111/luaspdlog/actions/workflows/build.yml/badge.svg)](https://github.com/zhangzhangzhang1111/luaspdlog/actions/workflows/build.yml)

基于 **spdlog 1.17.0** 的 Lua C++ 扩展。支持 Lua **5.1、5.2、5.3、5.4、5.5** 和 **LuaJIT 2.1**，提供同步/异步日志、多输出目标、日志轮转、回溯缓冲，以及独立 Lua 状态的日志器注册表。

## 使用

从 [Releases](https://github.com/zhangzhangzhang1111/luaspdlog/releases) 下载匹配 **系统、CPU、C 运行库及 Lua ABI** 的 ZIP，解压后将 `lib/luaspdlog.so`（Windows 为 `.dll`）放入 `package.cpath`。不同 Lua 主次版本的二进制不可互换。

```lua
local spdlog = require("luaspdlog")
local logger = spdlog.create("app", {
  level = "debug",
  sinks = {
    {type = "stdout", color = true},
    {type = "rotating", filename = "logs/app.log", max_size = 10485760, max_files = 3}
  }
})
logger:info("你好，Lua！")
logger:debug(string.format("用户=%s 数量=%d", "alice", 42))
spdlog.warn("默认日志器就是首个创建的日志器")
spdlog.shutdown()
```

消息为原始字符串，花括号不触发 fmt 格式化；格式化使用 Lua 的 `string.format`。支持中文、UTF-8 路径和消息内 NUL；名字、路径和 pattern 禁止 NUL。Windows 将 UTF-8 路径转换为宽字符，不改变进程代码页。

## 平台与产物

| 平台 | 架构 | Lua | 产物和验证 |
|---|---|---|---|
| Linux glibc（Ubuntu 22.04 构建） | x86_64、arm64 | 5.1–5.5、LuaJIT 2.1 | `.so` + 解释器；原生集成/多状态测试 |
| Linux musl（Alpine 3.22 构建） | x86_64、arm64 | 5.1–5.5、LuaJIT 2.1 | `.so` + 解释器；容器内原生测试 |
| macOS 13+ | x86_64、arm64 | 5.1–5.5、LuaJIT 2.1 | `.so` + 解释器；原生测试 |
| Windows 10/11、Server | x86、x86_64、arm64 | 5.1–5.5、LuaJIT 2.1 | `.dll` + 匹配的 Lua DLL/解释器；原生测试 |
| Android API 21+ | armeabi-v7a、arm64-v8a、x86、x86_64 | 5.1–5.5 | 静态 SDK；NDK 交叉编译 |
| iOS 13+ | arm64 设备；arm64/x86_64 模拟器 | 5.1–5.5 | 静态 SDK；Xcode 交叉编译 |

上表描述 CI 构建范围，**实际发布成功的组合以 Release 附件和包内 `manifest.json` 为准**。移动端没有宣称真机测试；LuaJIT 移动端 SDK 暂不发布。其他 POSIX 系统可尝试源码编译，但未列入二进制支持承诺。Lua 1.x–5.0 的历史 API 不在支持范围内。

运行库要求：Linux glibc 包要求 glibc ≥2.35 及兼容的 libstdc++，musl 包要求兼容的 musl 及 libstdc++；macOS 使用系统 libc++；Windows 使用 MSVC 运行库（Visual C++ Redistributable 2015–2022 或兼容更新版）。Windows ARM64 的 LuaJIT 使用上游 ARM64 实现。LuaJIT GC64/自定义数值类型、Lua 自定义编译选项和修改后的 C API 需要使用匹配配置重新构建。Windows 的模块应链接宿主正在使用的同名 Lua DLL，不能在宿主里加载第二份独立 Lua 运行时。

发布包包含模块、匹配解释器（桌面）、公共头文件、示例、许可证及 manifest。移动 SDK 包含 `lib/luaspdlog.a`、`lib/liblua.a` 与头文件。每个目标/ABI 独立 ZIP；Release 提供 `SHA256SUMS` 和包含依赖源码的源码包。

## 本地构建

需要 CMake ≥3.20、Ninja、C++17 编译器和 Python ≥3.12。

```sh
python -m pip install cmake ninja
python scripts/build.py --lua 5.4
python scripts/build.py --lua all
```

脚本下载固定版本的官方 Lua 源码并校验 SHA256。spdlog 与 fmt 头文件已随源码提供。构建与测试失败会中止打包。产物位于 `dist/`；运行自带解释器时：

```sh
LUA_CPATH='./lib/?.so;;' ./bin/lua ./share/luaspdlog/examples/basic.lua
# LuaJIT 包：将 ./bin/lua 替换为 ./bin/luajit
```

Windows PowerShell：设置 `$env:LUA_CPATH='.\lib\?.dll;;'`，再运行 `.\bin\lua.exe .\share\luaspdlog\examples\basic.lua`。Lua DLL 位于 `bin/`，应与宿主 EXE 同目录或位于系统 DLL 搜索路径。

已有 Lua 开发包时也可直接构建：

```sh
cmake -S . -B build/system -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLUA_INCLUDE_DIR=/path/to/lua/include -DLUA_LIBRARY=/path/to/liblua.a \
  -DLUASPDLOG_LUA_EXECUTABLE=/path/to/lua
cmake --build build/system
ctest --test-dir build/system --output-on-failure
cmake --install build/system --prefix install
```

Unix 动态模块从宿主解析 Lua 符号；Windows 链接对应的 Lua import library。不要把不同 ABI 的 `LUA_INCLUDE_DIR` 和 `LUA_LIBRARY` 混用。需要完全离线构建时，下载 Release 的 `source-with-deps` 包，直接指定 `LUASPDLOG_LUA_SOURCE_DIR` 指向解压后的对应 Lua 源码。

## 移动端嵌入

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
python scripts/build.py --lua 5.4 --target android-arm64
python scripts/build.py --lua 5.4 --target ios-arm64
python scripts/build.py --lua 5.4 --target ios-simulator-arm64
```

使用 `LUASPDLOG_STATIC` 编译宿主并包含 `luaspdlog.h`；链接 `luaspdlog.a`、匹配的 Lua、C++ 标准库、线程库、数学库及平台需要的 `dl`。静态链接器必须包含 `luaopen_luaspdlog`。Lua 5.2+ 可调用 `luaL_requiref(L, "luaspdlog", luaopen_luaspdlog, 1)`，然后 `lua_pop(L, 1)`；Lua 5.1 将入口注册到 `package.preload`。Android 应写入应用可写目录；iOS 使用沙箱目录，不使用动态加载 `.so` 的方式。iOS 附带的 Lua 将 `os.execute` / `io.popen` 的子进程调用替换为不支持的错误（`ENOSYS`），适配代码位于 `cmake/ios_compat.h`。Android SDK 使用 `c++_static`，宿主请统一 C++ 运行库策略。所有库为 Release，无签名；iOS 由最终应用负责签名。

## API 和发布

完整选项见 [API 文档](docs/API.md)，调用示例见 [examples](examples)。
编辑器的 LuaCATS 类型描述位于 `meta/luaspdlog.lua`；它只供静态分析，不应加入运行时 `package.path`。安装 LDoc 后可在项目根目录运行 `ldoc .` 生成中文 API 文档。

维护者推送 `v*` 标签会触发完整平台构建；所有构建及测试成功后，流水线先创建草稿 Release，上传 ZIP、离线源码包和校验文件，最后公开 Release。手动运行工作流仅生成 Actions artifacts。源码归档包含 spdlog/fmt/Lua/LuaJIT 许可证。

MIT License。spdlog、fmt、Lua、LuaJIT 的原始许可证随各发行包分发，参见 [第三方依赖](THIRD_PARTY.md)。
