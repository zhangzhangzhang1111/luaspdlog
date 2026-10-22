# 第三方依赖

| 项目 | 固定版本 / 提交 | 来源及许可证 |
|---|---|---|
| spdlog | 1.17.0 / `79524ddd08a4ec981b7fea76afd08ee05f83755d` | https://github.com/gabime/spdlog ，MIT，`third_party/spdlog/LICENSE` |
| fmt | spdlog 1.17.0 内附 12.1.0 | https://github.com/fmtlib/fmt ，MIT，`third_party/spdlog/include/spdlog/fmt/bundled/LICENSE` |
| Lua | 5.1.5 / 5.2.4 / 5.3.6 / 5.4.9 / 5.5.1 | https://www.lua.org/ftp/ ，MIT，下载源代码及发布包内许可证 |
| LuaJIT | 2.1 / `a4c064539ea931274d407e1249b4bb575424a0f4` | https://github.com/LuaJIT/LuaJIT ，MIT，源码 `COPYRIGHT` |

Lua/LuaJIT 下载与 SHA256 位于 `scripts/build.py`。spdlog 源压缩包 SHA256 为 `c6dee844e35a54cc80e3ab76aae2310763e0b49c7397549dfa4d4657d9737138`，已内附原始头文件，未修改第三方注释。所有新编写的注释及 API 描述使用中文。
