# runtimes/gc_wii/gx: the GX layer

aurora's GX front end, MTX and WebGPU renderer at a pinned commit, built without `aurora_core`, on a headless Dawn device (ADR 0007 §6, P1-14). The sources are not committed: the locks pin them, and they are fetched and built on your machine. What the repository does hold from aurora is the patch set in `patches/aurora/`, whose context and moved lines are aurora code (MIT). aurora's copyright and license notice is in `patches/aurora/LICENSE-aurora`, as MIT requires.

| File | What it is |
|---|---|
| `aurora.lock` | aurora pinned by commit and tree (MIT). |
| `dawn.lock` | Dawn and the 8 `DEPS` entries our build uses, pinned by commit and tree (lock v2). The other 73 `DEPS` entries are listed in `skipped_deps` with a reason. DXC is skipped: D3D12 uses FXC. |
| `patches/aurora/*.patch` | Our patch set against the pin, applied in order (file number = apply order) to a copy in the build tree. Each header names its ADR 0007 §6 patch number, which differs from the apply order. |
| `patches/aurora/LICENSE-aurora` | aurora's MIT notice, for the aurora code the patches contain. |
| `tools/fetch_sources.cpp` | `mpt_fetch_sources`, which fetches and verifies a lock with asset-import's resolver (commit, tree, and Dawn's `DEPS` cross-check). |
| `tools/build_dawn.cmake` | The Dawn recipe. It builds a shared `webgpu_dawn` into an install prefix, or only the generated WebGPU headers. |
| `tools/gen_dawn_lock.py` | Regenerates `dawn.lock` from a Dawn checkout when the pin is bumped. |

## Targets

- **`mpt_gx_frontend`** is game code. It holds the GX API, FIFO, command processor and MTX, plus the SDK entry points aurora lacks. It is built under ADR 0008's flags, and its math is redirected to the SDK's `mpt_*` functions (`include/mpt_gx/math_redirect.h`).
- **`mpt_gx_renderer_null`** is the null renderer. It always builds, needs no GPU, and links no Dawn library.
- **`mpt_gx_renderer_dawn`** is aurora's renderer on Dawn plus the host shim, with `cpu_rgba8` readback. It only builds with `MPT_GX_BACKEND=dawn`.
- **`mpt::gx`** is whichever of the two renderers `MPT_GX_BACKEND` selects (`null` by default).

## Building it (developer machine)

Keep paths short on Windows: MSVC tools fail on paths over 260 characters. Run the commands from a VS x64 developer prompt.

1. Configure once with any preset. This builds `mpt_fetch_sources` in the root project.
2. Fetch aurora:

   ```
   <build>/runtimes/gc_wii/gx/tools/mpt_fetch_sources --lock runtimes/gc_wii/gx/aurora.lock --cache external/sources
   ```

3. Build Dawn into `external/dawn` (on the reference machine: about 7 min to fetch, 2 min to configure, 15 min to build):

   ```
   cmake -DMPT_FETCH_TOOL=<build>/runtimes/gc_wii/gx/tools/mpt_fetch_sources -DMPT_SOURCE_CACHE=external/sources -DMPT_DAWN_PREFIX=external/dawn -P runtimes/gc_wii/gx/tools/build_dawn.cmake
   ```

   For the null renderer only, add `-DMPT_DAWN_HEADERS_ONLY=ON`. That takes seconds and needs no compile.
4. Configure with `windows-clang-debug` or `windows-clang-release` (Linux: `CC=clang CXX=clang++`), adding `-DMPT_GX_BACKEND=dawn` to render.

With `MPT_GX_BACKEND=dawn`, a missing aurora checkout or Dawn prefix fails the configure. With the default `null` renderer, it is a warning and the GX layer is skipped; `MPT_REQUIRE_GX=ON` turns that warning into an error too.

## Tests

All tests use our own synthetic display lists and textures.

| Test | Checks |
|---|---|
| `gx_scenes_null`, `gx_null_repeatable` | Every scene runs on the null renderer, and two processes give the same GX-side trail. |
| `gx_scenes_dawn`, `gx_null_vs_dawn` | G-c: `__gx` and the FIFO stream are identical null against real, frame by frame. |
| `gx_dawn_repeatable` | G-e: two processes give identical images. |
| `gx_dawn_probes` | G-d: probe colours match `tests/golden_probes.txt` (reference machine, D3D12) within a tolerance. |
| `gx_mtx_golden` | aurora's MTX matches golden hashes, including the `PSVEC` reciprocal-square-root hard cases. |
| `gx_frontend_no_host_math` | The front end leaves no host libm transcendental or `fma` undefined. |

`MPT_GX_TEST_API` (`automatic`, `d3d12`, `vulkan`, `d3d11`, `dawn_null`) and `MPT_GX_TEST_SOFTWARE_ADAPTER` (for lavapipe) select the device. The Linux leg runs in `.github/workflows/gx-linux-gate.yml`.

## Caches are user data

The renderer writes `pipeline_cache.db` and `dawn_cache.db` to `RendererConfig::cache_dir`. Their contents come from game TEV state. The shell passes its user cache directory, and tests pass a folder in the build tree. Never commit them: the compliance scanner rejects them.
