# Getting started

## Requirements

- A C++23 compiler: GCC 15+, Clang 21+, MSVC 14.51+, or AppleClang 17+.
- CMake 4+ and Ninja.
- Qt 6.8.x (prebuilt, via [aqtinstall](https://github.com/miurahr/aqtinstall))
  — only needed if you're building the desktop GUI shell (the default).
- Git (vcpkg is vendored as a submodule and bootstraps itself automatically —
  nothing else to install for it).

## Clone

```sh
git clone --recurse-submodules https://github.com/iainchesworth/cubeulator
cd cubeulator
```

If you already cloned without `--recurse-submodules`:

```sh
git submodule update --init --depth 1 deps/vcpkg
```

## First build

Install Qt locally with the pinned helper script, then configure and build:

```sh
# Windows
pwsh ci/install-qt.ps1
cmake --preset windows-msvc-debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# Linux/macOS
./ci/install-qt.sh
cmake --preset linux-gcc-debug -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

Just want the core library, no Qt/GUI?

```sh
cmake --preset linux-gcc-debug -DCUBEULATOR_BUILD_APP=OFF
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

See [Building & packaging](building.md) for every preset (including
Android/iOS, which are currently build-only) and packaging options.
