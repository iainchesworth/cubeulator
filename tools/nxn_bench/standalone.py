#!/usr/bin/env python3
"""Builds cube_nxn_bench as a single executable for a target CMake does not build it for.

The N x N library and the 3x3 solvers need nothing but the standard library and
the header-only part of {fmt}, so the benchmark can be compiled straight from
its sources: no OpenCV, no Qt, no vcpkg build. That is how it is taken to a
phone (an arm64 Android executable, to `adb push` and run) and to a browser's
engine (WebAssembly, run under Node), where "how long does a person wait for a
5x5" is the question and the answer is not the development machine's.

    standalone.py wasm    --out DIR --fmt-include DIR   # needs EMSDK (emcc, node)
    standalone.py android --out DIR --fmt-include DIR   # needs ANDROID_NDK_HOME
    standalone.py native  --out DIR --fmt-include DIR   # needs c++ (g++ 16 or clang 23)

--fmt-include is a directory holding fmt/format.h, for example the include
directory of a vcpkg installed tree. Then run what it prints, e.g.

    node DIR/nxn_bench.js time 2 9 10
    adb push DIR/nxn_bench /data/local/tmp/ && adb shell /data/local/tmp/nxn_bench time 2 9 10

docs/performance.md, "Cubes of any size", is what the numbers are for.
"""
import argparse
import os
import pathlib
import platform
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = sorted((ROOT / "src/lib/src/nxn").glob("*.cpp")) + sorted(
    (ROOT / "src/lib/src/solver").glob("*.cpp")
) + [ROOT / "tools/nxn_bench/main.cpp"]
# The same budget the library's own build gives the two files that build lookup
# tables in constant expressions (src/lib/CMakeLists.txt).
COMMON = [
    "-O2",
    "-std=c++23",
    "-DNDEBUG",
    "-DFMT_HEADER_ONLY",
    "-fconstexpr-steps=10000000",
    f"-I{ROOT / 'src/lib/include'}",
    f"-I{ROOT / 'src/lib/src/solver'}",
    f"-I{ROOT / 'src/lib/variants/profiling-tracy_disabled'}",
]


def find_tool(name, hints):
    """The first of `hints` (directories) holding `name`, else PATH."""
    # On Windows an SDK's tool is a .exe, or a .cmd/.bat that starts the real
    # one; the file with no extension beside them is a shell script.
    suffixes = [".exe", ".cmd", ".bat", ""] if platform.system() == "Windows" else [""]
    for hint in hints:
        for suffix in suffixes:
            candidate = pathlib.Path(hint) / f"{name}{suffix}"
            if candidate.exists():
                return str(candidate)
    found = shutil.which(name)
    if not found:
        sys.exit(f"cannot find {name}")
    return found


def build_wasm(out, fmt):
    emsdk = os.environ.get("EMSDK")
    if not emsdk:
        sys.exit("EMSDK is not set: point it at the Emscripten SDK the Qt wasm kit needs (docs/web.md)")
    emcc = find_tool("emcc", [pathlib.Path(emsdk) / "upstream/emscripten"])
    command = [emcc, *COMMON, "-isystem", fmt, "-sALLOW_MEMORY_GROWTH=1", "-sSTACK_SIZE=8388608",
               "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sNODERAWFS=1", "-o", str(out / "nxn_bench.js"),
               *map(str, SOURCES)]
    subprocess.run(command, check=True)
    return "node " + str(out / "nxn_bench.js") + " time 2 9 10"


def build_android(out, fmt):
    ndk = os.environ.get("ANDROID_NDK_HOME")
    if not ndk:
        sys.exit("ANDROID_NDK_HOME is not set")
    host = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}[platform.system()]
    clang = find_tool("aarch64-linux-android24-clang++",
                      [pathlib.Path(ndk) / "toolchains/llvm/prebuilt" / host / "bin"])
    command = [clang, *COMMON, "-isystem", fmt, "-fPIE", "-pie", "-static-libstdc++", "-o",
               str(out / "nxn_bench"), *map(str, SOURCES)]
    subprocess.run(command, check=True)
    return f"adb push {out / 'nxn_bench'} /data/local/tmp/ && adb shell /data/local/tmp/nxn_bench time 2 9 10"


def build_native(out, fmt):
    compiler = os.environ.get("CXX") or find_tool("c++", [])
    command = [compiler, *COMMON, "-isystem", fmt, "-o", str(out / "nxn_bench"), *map(str, SOURCES)]
    subprocess.run(command, check=True)
    return str(out / "nxn_bench") + " time 2 9 10"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", choices=["wasm", "android", "native"])
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--fmt-include", required=True, help="a directory holding fmt/format.h")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    run = {"wasm": build_wasm, "android": build_android, "native": build_native}[args.target](
        args.out, args.fmt_include)
    print(f"built; run it with:\n  {run}")


if __name__ == "__main__":
    main()
