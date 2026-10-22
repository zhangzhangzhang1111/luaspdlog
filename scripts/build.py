#!/usr/bin/env python3
"""下载校验过的 Lua 源码，构建、测试并生成独立 ABI 发布包。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSIONS = {
    "5.1": ("5.1.5", "2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333"),
    "5.2": ("5.2.4", "b9e2e4aad6789b3b63a056d442f7b39f0ecfca3ae0f1fc0ae4e9614401b69f4b"),
    "5.3": ("5.3.6", "fc5fd69bb8736323f026672b1b7235da613d7177e72558893a0bdcd320466d60"),
    "5.4": ("5.4.9", "2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6"),
    "5.5": ("5.5.1", "1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce"),
    "luajit": ("a4c064539ea931274d407e1249b4bb575424a0f4", "15625df3ebfbd1f76371005bffae39b84c65e718e57acc9a9c57863c7c101541"),
}


def run(*cmd, cwd=ROOT, env=None):
    """显示可复现的命令，并在任一步骤失败时中断发布。"""
    print("+", " ".join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), cwd=cwd, check=True, env=env)


def source(version):
    """下载固定版本并校验摘要；缓存损坏时不继续编译。"""
    revision, expected = VERSIONS[version]
    cache = ROOT / ".deps"
    cache.mkdir(exist_ok=True)
    archive = cache / ("lua-" + revision + ".tar.gz")
    if not archive.exists():
        url = ("https://codeload.github.com/LuaJIT/LuaJIT/tar.gz/" + revision
               if version == "luajit" else "https://www.lua.org/ftp/lua-" + revision + ".tar.gz")
        temporary = archive.with_suffix(".download")
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as out:
            shutil.copyfileobj(response, out)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError("SHA256 mismatch: " + str(archive))
    dest = cache / ("src-" + revision)
    if not dest.exists():
        dest.mkdir()
        with tarfile.open(archive) as tar:
            # 拒绝绝对路径、目录穿越及链接，不信任压缩包内的路径。
            for member in tar.getmembers():
                path = Path(member.name)
                if path.is_absolute() or ".." in path.parts or member.issym() or member.islnk():
                    raise RuntimeError("unsafe source archive")
            tar.extractall(dest, filter="data")
    return next(p for p in dest.iterdir() if p.is_dir())


def native_name():
    arch = {"AMD64": "x86_64", "aarch64": "arm64", "i386": "x86", "i686": "x86"}.get(platform.machine(), platform.machine())
    system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[platform.system()]
    if system == "linux":
        system += "-musl" if list(Path("/lib").glob("ld-musl-*.so.1")) else "-glibc"
    return system + "-" + arch


def build(version, args):
    """每个目标、Lua ABI 独立构建目录，禁止误混用二进制。"""
    target = args.target or native_name()
    mobile = target.startswith(("android-", "ios-"))
    if mobile and version == "luajit":
        raise RuntimeError("mobile SDKs currently use PUC Lua; LuaJIT mobile is not advertised")
    src = source(version)
    build_dir = ROOT / "build" / target / version
    package_dir = build_dir / "package"
    definitions = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(package_dir)]
    abi = "51" if version == "luajit" else version.replace(".", "")
    if version == "luajit":
        if os.name == "nt":
            run("cmd", "/c", "msvcbuild.bat", cwd=src / "src")
            runtime = src / "src" / "luajit.exe"
            lib = src / "src" / "lua51.lib"
        else:
            run("make", "-j2", "BUILDMODE=static", cwd=src)
            runtime = src / "src" / "luajit"
            lib = src / "src" / "libluajit.a"
        definitions += ["-DLUA_INCLUDE_DIR=" + str(src / "src"), "-DLUA_LIBRARY=" + str(lib),
                        "-DLUASPDLOG_LUA_EXECUTABLE=" + str(runtime)]
    else:
        definitions += ["-DLUASPDLOG_LUA_SOURCE_DIR=" + str(src), "-DLUASPDLOG_LUA_ABI=" + abi]
    if target.startswith("android-"):
        ndk = os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT")
        if not ndk:
            raise RuntimeError("set ANDROID_NDK_HOME to an installed NDK")
        android_abi = {"arm64": "arm64-v8a", "armv7": "armeabi-v7a", "x86_64": "x86_64", "x86": "x86"}[target[8:]]
        definitions += ["-DCMAKE_TOOLCHAIN_FILE=" + ndk + "/build/cmake/android.toolchain.cmake",
                        "-DANDROID_ABI=" + android_abi, "-DANDROID_PLATFORM=android-21",
                        "-DANDROID_STL=c++_static", "-DLUASPDLOG_STATIC=ON"]
    elif target.startswith("ios-"):
        device = target == "ios-arm64"
        definitions += ["-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=" + ("iphoneos" if device else "iphonesimulator"),
                        "-DCMAKE_OSX_ARCHITECTURES=" + ("x86_64" if target.endswith("x86_64") else "arm64"),
                        "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0", "-DLUASPDLOG_STATIC=ON", "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO"]
    if args.sanitize:
        definitions.append("-DLUASPDLOG_SANITIZE=ON")
    run("cmake", "-S", ROOT, "-B", build_dir, "-G", "Ninja", *definitions, *args.cmake_arg)
    run("cmake", "--build", build_dir, "--parallel", "2")
    if not mobile:
        # Windows 的测试可执行文件需在 PATH 中找到匹配的 Lua DLL。
        env = os.environ.copy()
        env["PATH"] = str(src / "src") + os.pathsep + str(build_dir) + os.pathsep + env.get("PATH", "")
        run("ctest", "--test-dir", build_dir, "--output-on-failure", env=env)
    if args.sanitize:
        return  # 检测器构建不可混入用户发布包。
    if package_dir.exists():
        shutil.rmtree(package_dir)
    run("cmake", "--install", build_dir)
    licenses = package_dir / "share" / "luaspdlog" / "licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    if version == "luajit":
        (package_dir / "bin").mkdir(exist_ok=True)
        shutil.copy2(runtime, package_dir / "bin" / runtime.name)
        if os.name == "nt":
            shutil.copy2(src / "src" / "lua51.dll", package_dir / "bin")
        else:
            (package_dir / "lib").mkdir(exist_ok=True)
            shutil.copy2(lib, package_dir / "lib")
        headers = package_dir / "include" / "lua"
        headers.mkdir(parents=True, exist_ok=True)
        for name in ("lua.h", "luaconf.h", "lauxlib.h", "lualib.h", "luajit.h"):
            shutil.copy2(src / "src" / name, headers)
        shutil.copy2(src / "COPYRIGHT", licenses / "luajit.txt")
        shutil.copytree(src / "src" / "jit", package_dir / "bin" / "lua" / "jit", dirs_exist_ok=True)
    else:
        # 官方 Lua 许可证位于源码头文件；附上全文所在文件，覆盖所有历史版本。
        shutil.copy2(src / "src" / "lua.h", licenses / "lua.txt")
        if (src / "doc" / "readme.html").exists():
            shutil.copy2(src / "doc" / "readme.html", licenses / "lua-readme.html")
    manifest = {
        "project": "luaspdlog", "version": "0.1.0", "target": target,
        "lua": version, "lua_revision": VERSIONS[version][0], "spdlog": "1.17.0",
        "lua_source_sha256": VERSIONS[version][1],
        "validation": "cross-compiled; no device execution" if mobile else "integration + concurrent multistate tests passed",
        "linkage": "static SDK; link C++ runtime and threads" if mobile else "dynamic Lua module",
        "cmake": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0],
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip() if (ROOT / ".git").exists() else "uncommitted",
    }
    (package_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not mobile:
        executable = package_dir / "bin" / (("luajit" if version == "luajit" else "lua") + (".exe" if os.name == "nt" else ""))
        with tempfile.TemporaryDirectory(prefix="luaspdlog-installed-") as tmp:
            run(executable, ROOT / "tests" / "test.lua", package_dir / "lib", cwd=tmp)
    out = ROOT / "dist"
    out.mkdir(exist_ok=True)
    filename = "luaspdlog-0.1.0-" + target + "-" + ("luajit-2.1" if version == "luajit" else "lua-" + version)
    with zipfile.ZipFile(out / (filename + ".zip"), "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package_dir.rglob("*")):
            if path.is_file():
                archive.write(path, Path(filename) / path.relative_to(package_dir))
    print("Packaged:", filename, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lua", choices=[*VERSIONS, "all"], default="all")
    parser.add_argument("--target", help="发布包的目标标识；android/ios 标识会启用交叉编译")
    parser.add_argument("--cmake-arg", action="append", default=[])
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    versions = VERSIONS if args.lua == "all" else [args.lua]
    for version in versions:
        if args.lua == "all" and version == "luajit" and (args.target or "").startswith(("android-", "ios-")):
            continue
        build(version, args)


if __name__ == "__main__":
    main()
