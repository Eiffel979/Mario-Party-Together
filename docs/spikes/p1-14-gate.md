# P1-14 opening gate (G-a to G-e)

**Owner:** runtime-gc-wii
**Dates:** 2026-10-08 to 2026-10-09. About one working day of the 5-day time-box was used.
**Inputs:** ADR 0007 §6, ADR 0008, ADR 0009, `docs/spikes/s1-gx-backend.md` §8, Phase 1 plan (P1-14, R17).
**Pins:** aurora `514339438178ef2bed1b14e5149d90ece0c6e0cc`, Dawn `13abc3bc8ea2d3c2050f9e77a12d012108ceee24`.
**Machine:** Windows 11 Pro 10.0.26200, Ryzen 5 3600 (12 threads), 16 GB, Radeon RX 5700 (driver 32.0.21045.1000). Visual Studio 18 (MSVC 14.51), clang-cl 22.1.3, CMake 4.3.1, Ninja 1.13.2, Python 3.13, the VS-bundled vcpkg (2026-07-27) at the repo's baseline `9e593bb`.
**Data:** synthetic GX display lists and textures only, generated in code. No game dump and no game file was used.

## 1. Verdict

| Check | Windows | Linux |
|---|---|---|
| **G-a** Dawn (pinned, from source) and aurora GX build with Clang from a clean cache, no unhashed download | **Pass** | **Not run.** No Linux machine (no WSL, no Docker). Needs CI. |
| **G-b** Headless device and EFB → `cpu_rgba8` readback | **Pass** on D3D12, Vulkan, D3D11 and Dawn's Null adapter | **Not run.** Needs CI (Vulkan, or Dawn's Null adapter for the null lane). |
| **G-c** Null backend links without Dawn and matches the real backend's GX-side state | **Pass**, after one fix to the stub table (§5) | Not run. Expected to follow from Windows, because the front end is the same code. |
| **G-d** Synthetic scenes render | **6 of 8 features correct.** Partial `GXCopyTex` clear is wrong (known, upstream `9c0bf66`). Z textures fail (expected, patch 7). Neither is a fallback trigger. | Not run. |
| **G-e** Two runs give identical images in synchronous-pipeline mode | **Pass** (D3D12 and Vulkan). Without patch 4 the images are wrong (D5 confirmed). | Not run. |

**Recommendation: proceed with option (a).** Fallback (c) is not triggered. Everything that can be checked on Windows passed, and the two rendering defects are patches 7 and 7b, which ADR 0007 §6 already lists.

**The Linux half of G-a and G-b is still open.** ADR 0007 §6 asks for both OSes. Section 9 gives the exact Linux steps. They should run as the first CI job of P1-14 (P1-21 owns CI). Nothing found on Windows points to a Linux problem: Dawn's Linux build uses the same CMake and `DEPS`, and Party Board already ships aurora on Linux with Clang. If the Linux leg fails, about 4 days of the time-box remain to decide on (c).

## 2. What was built, and how it avoids `aurora_core`

aurora's own `CMakeLists.txt` is **not used**:
- It always builds `aurora_core`, which ADR 0007 rejects.
- Its `vendor` Dawn provider downloads `github.com/google/dawn/archive/<ref>.tar.gz` with **no `URL_HASH`**, so it fails the "no unhashed download" rule.
- It fetches imgui, Freetype (from `files.twilitrealm.dev`), abseil and Tracy through FetchContent.

Instead, a throwaway harness in the session scratchpad (outside the repo, not committed) builds three things straight from the pin's source lists:

| Target | Sources (aurora at the pin) | Role in production (ADR 0007 §6) |
|---|---|---|
| front end | `lib/dolphin/gx/*.cpp` (19 files), `lib/gx/fifo.cpp`, `lib/gx/command_processor.cpp`, `lib/dolphin/mtx/*.c` (6 files) | Game module |
| renderer | `lib/gfx/*.cpp` (13 files), `lib/gx/{gx,pipeline,shader,shader_info}.cpp`, `lib/webgpu/{gpu,gpu_cache}.cpp` | Host-side renderer library |
| null stubs | aurora's `tests/gx_test_stubs.cpp` plus two real functions (§5) | Null backend |

The **host shim** that replaces `aurora_core` needed **13 symbols**:
- `aurora::g_config`, `aurora::log_internal` and `fmt::formatter<AuroraLogLevel>::format`;
- in `aurora::window`: `SurfaceLock` (constructor and destructor), `get_window_size`, `get_sdl_window` (returns null), `is_presentable`, `push_custom_event` and `set_frame_buffer_aspect_fit`;
- `aurora::webgpu::utils::SetupWindowAndGetSurfaceDescriptor` (returns null);
- `aurora::vi::configured_fb_size` (always the logical 640×480) and `aurora::vi::configure`.

S1 estimated about 20. `OSPanic` and `OSCachedToPhysical` were not needed by these sources.

**Patches applied to the pin** for the gate (42 lines added, 4 removed, in 4 files):
- **Patch 3 (headless):** in `webgpu::initialize`, no surface is created when there is no SDL window. The adapter is requested without `compatibleSurface`. The EFB, its resolve texture and its depth texture are created directly at `fb_width`×`fb_height` in `RGBA8Unorm`, so the readback bytes are already R,G,B,A (ADR 0002 rule 7).
- **Patch 4 (synchronous pipelines):** a `gfx::g_synchronousPipelines` flag. When it is set, no compile thread starts and every pipeline is built when it is first used, with no per-frame cap.
- **Test hook (new, small):** `fifo::g_processObserver`, called with every buffer handed to the command processor. G-c hashes the FIFO stream with it. P1-21's CI test needs it too, so it joins the patch set as item 8.

**Readback** is our own code, about 40 lines in the shim. In the `gfx::end_frame` callback on aurora's render worker, it copies `present_source()` into a `MapRead` buffer, submits, maps synchronously with `WaitAny`, and copies the bytes out. The main thread then waits with `gfx::gpu_synchronize()`.

## 3. G-a: pinned source build on Windows (pass)

**Pinning.**
- Dawn was fetched by commit (`git fetch --depth 1 origin 13abc3bc…`) from `dawn.googlesource.com`.
- `tools/fetch_dawn_dependencies.py` at that commit fetches 19 `DEPS` entries. **All 19 are pinned by full 40-hex commit hash.** A listing script parsed `DEPS` and confirmed it.
- That script ignores git errors (`capture_output`, no return-code checks). So after fetching, every dependency's `HEAD` was checked against its `DEPS` hash and for a clean tree: 19/19 matched, 0 dirty. The production recipe must keep this check.
- aurora was fetched by commit.
- Every other library came from vcpkg at the repo's baseline (SHA-512-verified downloads).
- **No FetchContent ran. There was no unhashed download.**

**Dawn configure flags** (clang-cl, Ninja, Release):
`-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED -DDAWN_ENABLE_INSTALL=ON -DDAWN_FETCH_DEPENDENCIES=OFF -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_BENCHMARKS=OFF -DDAWN_BUILD_PROTOBUF=OFF -DDAWN_USE_GLFW=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_D3D11=ON -DDAWN_ENABLE_D3D12=ON -DDAWN_ENABLE_VULKAN=ON -DDAWN_ENABLE_NULL=ON -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF -DABSL_MSVC_STATIC_RUNTIME=OFF -DDAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON`

**`DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON` is required.** Without it, Dawn loads `vulkan-1.dll` and `d3dcompiler_47.dll` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, which fails with Windows error 87 for a bare file name. The result is that no Vulkan adapter is found and D3D12 device creation fails. Dawn's own answer is to copy `d3dcompiler_47.dll` from the Windows SDK next to the binary. We do not want to redistribute it. With the flag, both DLLs load from System32.

**Measurements:**

| Step | Time | Disk |
|---|---|---|
| Dawn fetch (one commit) | 6 s | |
| Dawn `DEPS` fetch (19 repositories, shallow) | 83–96 s | Dawn sources total **970 MB**, including 205 MB of `.git` and 153 MB of DirectXShaderCompiler (fetched but not built) |
| Dawn configure | 1 min 36 s (first run), 2 min 50 s (second run) | |
| Dawn build, `webgpu_dawn` target, 1105 steps, `-j12` | **13 min 4 s** (first clean build), **16 min 58 s** (second clean build, after the system-load flag) | Build tree **204 MB**. 3,426 compiler warnings, 0 errors. |
| Dawn install | 2 s | **15 MB** (`webgpu_dawn.dll` 14 MB plus headers and CMake config) |
| vcpkg: abseil, fmt, zlib, libpng, sqlite3, tracy, xxhash, zstd (SDL3 was already cached) | 8 min 19 s (abseil alone 4.3 min) | 552 MB `vcpkg_installed` |
| aurora front end, renderer and MTX, plus the harness (56 compile and link steps) | 45 s build, 73 s configure | 5 MB aurora source, 27 MB build |

**Total cold start: about 30 minutes and about 1.8 GB.** It happens once and is cached, like the other pins.

**Path-length finding.** The first vcpkg attempt failed in abseil with `cl.exe` error C1083 ("Cannot open compiler generated file"), because the buildtree path under the scratchpad was longer than 260 characters. MSVC does not support long paths, even with `LongPathsEnabled=1`. A short `--x-buildtrees-root` fixed it. clang-cl and Ninja had no trouble with the same long paths for Dawn and aurora. Git needs `core.longpaths=true` to check out Dawn, whose Tint test files have names over 200 characters. **The P1-14 recipe must set `core.longpaths` for every Dawn fetch and keep vcpkg buildtrees on a short path.**

**Compiler.** Dawn and aurora both build cleanly with clang-cl, and the front end builds with `-ffp-contract=off` (ADR 0008 §4). aurora warns only about unhandled enum values in `GXBump.cpp` (`-Wswitch`).

## 4. G-b: headless device and readback on Windows (pass)

- With no SDL window and no surface, the harness created a device and rendered all scenes on **D3D12, Vulkan, D3D11 and Dawn's Null adapter**. Every run exited 0, with a 640×480 EFB at scale 1 and 1280×960 at scale 2.
- `cpu_rgba8`: the mapped bytes are written unchanged as an `RGBA` PNG, and the colours are correct (§6). So the byte order is R,G,B,A with stride = width.
- The Null adapter ("CPU" adapter type, which must be allowed explicitly) creates a device and runs the whole pipeline. Its readback is all clear colour (no rasterisation). It is a usable headless lane for CI machines without a GPU, and it gives the same GX-side hashes (§5).
- Startup with a cold pipeline cache is 1.1 to 3.7 s.
- Per frame with synchronous readback, measured over 300 frames of the lighting scene (58 KB of FIFO per frame): **about 6.9 ms at scale 1** and **about 19.5 ms at scale 3** (1920×1440). That fits E7 without the one-frame-delayed readback.
- **D3D12 shader compiler.** aurora enables Dawn's `use_dxc` toggle on Windows. `dxcompiler.dll` is not on this machine, and the Windows SDK here does not ship it. Dawn compiled the shaders anyway, without an error, so it presumably fell back to FXC. The gate did not verify which compiler ran. P1-14 must choose between `DAWN_USE_BUILT_DXC=ON` (DXC is already in the fetched sources but adds a large LLVM-based build) and FXC on purpose.

## 5. G-c: null backend against real backend (pass, after a fix)

**Method.**
- Every piece of memory the GX front end can see (texture objects, texel data, TLUTs, display lists, copy destinations, light objects) lives in one arena reserved at a **fixed virtual address**. This matters because the FIFO stream carries host pointers (S1 D10).
- Per frame, the harness records the xxh3 of the `__gx` shadow registers and the xxh3 of every byte the command processor receives.
- Each run covers 9 scenes × 3 frames.

**First result: fail.** aurora's `tests/gx_test_stubs.cpp` stubs `comp_type_size` and `comp_cnt_count` to return 0. With those, the command processor cannot size vertices: it misparses the first draw ("unsupported primitive type 248") and aborts. Both functions are pure CPU code in the renderer's `lib/gx/gx.cpp`. **The null backend needs their real bodies.** With them, the stub table works. In P1-14 these two functions should move to the front end, so null and real share one copy.

**After the fix: identical.** These runs produced the same per-frame `__gx` and stream hashes for all 27 frames:
- null (two separate processes);
- D3D12, Vulkan, D3D11 and Dawn Null;
- D3D12 at **internal scale 2**;
- D3D12 in **asynchronous-pipeline mode**.

So neither the renderer, the backend, the internal scale nor pipeline timing changes any GX-side state the game can see. `gate_null.exe` imports no `webgpu_dawn.dll` (checked with `dumpbin /dependents`). Dawn's headers are still needed at compile time, because the front end's headers include `webgpu_cpp.h`.

## 6. G-d: synthetic scenes (6 of 8 features correct, 2 known patches)

All images were checked by eye from the D3D12 readback. D3D12 and Vulkan gave byte-identical images on this GPU, which other GPUs and drivers will not guarantee.

| Scene | Result |
|---|---|
| Clear and quads (flat and Gouraud) | Correct colours, positions and orientation (GX top-left origin). |
| TEV konst, swap table, S10 | **Correct.** Konst gives orange. Swap table R↔B turns a red vertex colour blue. S10 `C0 = (-128, 300, 64)` clamps to (0, 255, 64), and `RASC + C0` gives the expected ramp. The ramp shows visible 8-bit banding steps, which should be compared with Dolphin when goldens are recorded. |
| Fog (perspective linear) | **Correct.** The fog increases with depth. Two of the five test quads were placed off-screen by the scene itself, not by the renderer. |
| Lights: spot with distance attenuation; specular | **Correct** (spot cone with falloff; specular highlight). |
| CI8 + RGB565 TLUT, repeat wrap | **Correct.** |
| Runtime display list, called 4× with different matrices | **Correct.** |
| `GXCopyTex` re-sampled as a texture | **Partly wrong.** Both copies re-sample correctly. But the partial-rectangle copy with `clear = GX_TRUE` clears the **whole EFB**, not just its 80×60 source rectangle. That is the known pin bug fixed upstream in `9c0bf66` (patch 7b, m427). |
| Indirect warp; indirect tile | Warp is correct. Tile renders a tiled pattern that looks plausible, but needs a reference image (Dolphin) before golden recording. |
| Z texture (`GXSetZTexture` REPLACE, Z8) | **Fails, as expected.** The Z texture is ignored, so the green bar covers the whole width instead of only the far half. Patch 7. |

## 7. G-e: repeatability (pass)

- Two separate D3D12 processes in synchronous-pipeline mode gave **identical image hashes for all 27 frames**. So did two Vulkan processes. 300 consecutive frames of one scene were all identical.
- **Asynchronous mode (the pin's default) is wrong, not just different.** In the same run, most frames show only the clear colour or the previous scene, because draws are skipped while their pipelines compile. This confirms S1's finding D5: **patch 4 is required** for golden images, and it is enough. Players will see the same effect for a few frames whenever new TEV state appears, unless the dump lane and normal play also use the synchronous mode or a warmed pipeline cache. P1-14 should decide this.

## 8. For architect: what P1-14 needs (gate passed on Windows)

**vcpkg ports** (all built fine at baseline `9e593bb`, triplet `x64-windows-static-md`):

| Port | Why | Note |
|---|---|---|
| `abseil` | `flat_hash_map`, `btree`, `flat_hash_set`, `hash` in front end and renderer | Dawn keeps its own abseil inside the shared `webgpu_dawn.dll`, so the two never meet. That is one more reason to keep Dawn **shared**. |
| `xxhash` | renderer and front-end hashing | |
| `fmt` | logging | |
| `libpng` (pulls `zlib`) | `gfx/png_io.cpp` (texture dumps and replacement) | |
| `sqlite3` | pipeline cache and Dawn blob cache | CMake target `unofficial::sqlite3::sqlite3` |
| `tracy` | **headers only**: `TRACY_ENABLE` is never defined, so the zone macros compile to nothing (ADR 0007 §6) | Do not link `Tracy::TracyClient`; take only its include directory |
| `zstd` | **not needed**: only with `AURORA_CACHE_USE_ZSTD`, which the gate left off | Drop it from the ADR 0007 list unless we want compressed caches |
| `sdl3` (already present) | the renderer's `pipeline_cache.cpp` uses `SDL_IOStream`, so the renderer library links SDL3 | No new features. `vulkan` stays off (fallback (c) only). |

**Not needed:** imgui, Freetype, RmlUi, nod, googletest (for aurora's own tests), and anything from aurora's `extern/CMakeLists.txt`.

**CMake wiring:**
1. **Dawn is an external build, not a subproject.** asset-import or the developer recipe fetches Dawn by commit plus `DEPS` (with the hash check from §3 and `core.longpaths`). It then configures it with the flags in §3, builds `webgpu_dawn`, and installs it into a prefix inside `external/`. The root CMake finds it with `find_package(Dawn CONFIG REQUIRED PATHS <prefix> NO_DEFAULT_PATH)` and links `dawn::webgpu_dawn`. The include directory is `<prefix>/include`, because the imported target does not carry it. Dawn is not added through `add_subdirectory` or FetchContent: it would pull in Dawn's abseil and its compile flags, and add about 17 minutes to every clean configure of our tree.
2. **Do not include aurora's `CMakeLists.txt`.** Our `runtimes/gc_wii/gx/CMakeLists.txt` lists the pin's sources explicitly, in three targets as in §2:
   - the front end plus MTX, compiled into the game module with ADR 0008 flags;
   - the renderer, a host-side library linking Dawn, SDL3, sqlite3, libpng, abseil, xxhash and fmt;
   - the null stubs, which link no Dawn.

   Definitions: `AURORA TARGET_PC AURORA_ENABLE_GX WEBGPU_DAWN NOMINMAX`. Include directories: `<aurora>/include`, `<aurora>/lib`, `<aurora>/lib/dolphin/gx`.
3. **Root options** (architect-owned): something like `MPT_GX_BACKEND=dawn|null` (default `dawn` for developer builds, `null` for CI lanes without a GPU), and `MPT_DAWN_PREFIX`. On Windows, copy `webgpu_dawn.dll` next to the binaries with `$<TARGET_RUNTIME_DLLS:…>`.
4. **Patch set:** apply patches 3 and 4 and the observer hook (item 8) at fetch time. Move `comp_type_size`/`comp_cnt_count` to the front end (or duplicate them in the null stubs). Patches 1, 2, 5, 6, 7 and 7b remain as ADR 0007 lists them.
5. **Toolchain:** the gate built everything with clang-cl, and with `cl` only for vcpkg ports, which is fine (C ABI, `/MD`). The repo's `windows-*` presets currently set `CMAKE_CXX_COMPILER=cl`, so the game module and SDK need a clang-cl preset (ADR 0008 §4).

**For qa-reviewer.** The compliance scanner already rejects `external/`. The gate's own trees were kept in the session scratchpad. The scanner should also reject a `webgpu_dawn.dll` and a `dawn_cache.db` or `pipeline_cache.db` anywhere in the tree, because the renderer writes caches to `g_config.cachePath`.

## 9. Linux steps still to run (CI, first job of P1-14)

The same harness and steps, with these changes:
- `clang`/`clang++` instead of clang-cl;
- `-DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF`: headless needs no surface, but check Dawn configures with them off;
- the Vulkan loader and `libvulkan1` plus Mesa lavapipe on the runner (GitHub-hosted runners have no GPU);
- run G-b on lavapipe (Vulkan) and on Dawn's Null adapter;
- compare G-c's hashes against the Windows trail. The FIFO stream contains arena pointers, so it matches only if the arena is at the same fixed address, which is ADR 0008 §11's requirement anyway.

**Pass criteria:** the same as §3 to §7. Only G-a/G-b failing on Linux would trigger (c).

## 10. Reproducing

1. **Sources.**
   - aurora: `git clone` and `git checkout 514339438178ef2bed1b14e5149d90ece0c6e0cc`.
   - Dawn: `git init`, `git config core.longpaths true`, `git fetch --depth 1 https://dawn.googlesource.com/dawn 13abc3bc8ea2d3c2050f9e77a12d012108ceee24`, `git checkout FETCH_HEAD`.
   - Dawn's dependencies: `python -I tools/fetch_dawn_dependencies.py -d <dawn>` with `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.longpaths GIT_CONFIG_VALUE_0=true`. Then check each of the 19 `DEPS` paths: `HEAD` equals its hash and the tree is clean.
2. **Dawn.** From a VS x64 developer environment with clang-cl on `PATH`: `cmake -S dawn -B build/dawn -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_INSTALL_PREFIX=install/dawn <flags from §3>`, then `cmake --build build/dawn --target webgpu_dawn`, then `cmake --install build/dawn`.
3. **vcpkg.** `vcpkg install --x-buildtrees-root=<short path> --triplet x64-windows-static-md` for the ports in §8.
4. **Harness.** Apply the patches from §2 to aurora. Configure the harness with the vcpkg toolchain, `-DAURORA_DIR` and `-DDAWN_PREFIX`. Then run:
   - `gate_null --out null`;
   - `gate_real --backend d3d12|vulkan|d3d11|null-adapter [--scale N] [--async] --out <dir>`.

   Each run writes a `results.txt` with per-frame `gx`/`stream`/`img` hashes and one PNG per scene. The harness stays in the scratchpad: production GX code belongs to P1-14 in `runtimes/gc_wii/gx/`, owned by the P1-13/P1-14 agent.
