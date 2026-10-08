# Building

## Requirements
- CMake ≥ 3.25 and Ninja
- A C++20 compiler: MSVC 2022+ on Windows, GCC 13+ or Clang 17+ on Linux
- vcpkg. Point `VCPKG_ROOT` at it. Dependencies (SDL3, nlohmann-json, doctest) are declared in `vcpkg.json` and pinned by the baseline in `vcpkg-configuration.json`.
- Python 3 (compliance checks)
- Linux only: the X11/Wayland/audio dev packages listed in `.github/workflows/ci.yml`

## Build and test
Windows (from a "x64 Native Tools" prompt, or after running `vcvars64.bat`):

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Linux:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

The `ci-*` presets match CI and treat warnings as errors.

Visual Studio 2022+ ships CMake, Ninja and vcpkg. Set `VCPKG_ROOT` to `<VS install>\VC\vcpkg` if you don't have a standalone vcpkg.

## Run
```
build/<preset>/core/mario-party-together            # opens the (empty) shell window
build/<preset>/core/mario-party-together --headless --frames 120
build/<preset>/core/mario-party-together --identify path/to/your/dump.z64
```

## Compliance hook
Enable the pre-commit check once per clone. It rejects dumps, extracted assets and binaries:

```bash
git config core.hooksPath .githooks
```
