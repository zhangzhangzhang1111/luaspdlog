#!/usr/bin/env python3
"""检查发布矩阵完整性，生成离线源码包与 SHA256 清单。"""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import zipfile
from build import ROOT, VERSIONS, source

DESKTOP = ["linux-glibc-x86_64", "linux-glibc-arm64", "linux-musl-x86_64", "linux-musl-arm64",
           "macos-x86_64", "macos-arm64", "windows-x86_64", "windows-x86", "windows-arm64"]
MOBILE = ["android-arm64", "android-armv7", "android-x86_64", "android-x86", "ios-arm64",
          "ios-simulator-arm64", "ios-simulator-x86_64"]


def main():
    """任何目标缺失、验证标识错误或文件名不符时拒绝公开 Release。"""
    out = ROOT / "dist"
    expected = {(t, v) for t in DESKTOP for v in VERSIONS}
    expected |= {(t, v) for t in MOBILE for v in VERSIONS if v != "luajit"}
    found = set()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    manifests = []
    for path in out.glob("*.zip"):
        with zipfile.ZipFile(path) as archive:
            names = [n for n in archive.namelist() if n.endswith("/manifest.json")]
            if len(names) != 1:
                raise RuntimeError("expected one manifest in " + path.name)
            manifest = json.loads(archive.read(names[0]))
        key = (manifest["target"], manifest["lua"])
        if key in found or key not in expected or manifest["commit"] != commit:
            raise RuntimeError("unexpected, duplicate, or stale artifact: " + path.name)
        if key[0] in DESKTOP and manifest["validation"] != "integration + concurrent multistate tests passed":
            raise RuntimeError("artifact was not tested: " + path.name)
        found.add(key)
        manifests.append(manifest)
    if found != expected:
        raise RuntimeError("missing release artifacts: " + str(sorted(expected - found)))
    (out / "BUILD-MANIFEST.json").write_text(json.dumps(manifests, indent=2) + "\n", encoding="utf-8")
    # git archive 只收录当前提交，排除缓存、构建目录及任何工作区临时文件。
    tracked = subprocess.check_output(["git", "archive", "--format=tar", "--prefix=luaspdlog-0.1.0/", "HEAD"], cwd=ROOT)
    with tarfile.open(out / "luaspdlog-0.1.0-source-with-deps.tar.gz", "w:gz") as tar:
        with tarfile.open(fileobj=io.BytesIO(tracked)) as src:
            for member in src:
                tar.addfile(member, src.extractfile(member) if member.isfile() else None)
        for version in VERSIONS:
            directory = source(version)
            tar.add(directory, arcname="luaspdlog-0.1.0/dependencies/" + directory.name)
    assets = sorted(p for p in out.iterdir() if p.is_file() and p.name != "SHA256SUMS")
    (out / "SHA256SUMS").write_text("".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in assets), encoding="utf-8")
    print("Verified", len(found), "binary packages and generated source bundle/checksums")


if __name__ == "__main__":
    main()
