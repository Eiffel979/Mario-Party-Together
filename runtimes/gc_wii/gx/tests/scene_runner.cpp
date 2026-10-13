// Runs every synthetic scene for N frames on the linked renderer and writes, per frame:
//   <scene> frame=<n> gx=<xxh3 of __gx> stream=<xxh3 of every buffer the command processor saw> bytes=<n>
//   img=<xxh3 of the cpu_rgba8 readback, 0 for null> <w>x<h>
// and, for the last frame of each scene, the probe colours: probe <scene> <x> <y> <r> <g> <b> <a>.
//
//   mpt_gx_scenes[_null] --out <dir> [--frames N] [--scale S] [--api automatic|d3d12|vulkan|d3d11|dawn_null]
//                        [--allow-software-adapter] [--png] [--check-probes <golden file> [--tolerance T]]
//
// The GX-side columns (gx, stream, bytes) must be identical between the null and dawn renderers (G-c),
// and between runs. The img column must be identical between two dawn runs on one machine (G-e).
#include "scenes.hpp"

#include "mpt/gx/renderer.h"

#include <dolphin/gx.h>

#include "dolphin/gx/__gx.h"
#include "gx/command_processor.hpp"

#include <xxhash.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>

#ifdef MPT_GX_TEST_PNG
#include <png.h>
#endif

namespace fs = std::filesystem;

namespace {
XXH3_state_t* g_stream = nullptr;
uint64_t g_streamBytes = 0;

void observe(const uint8_t* data, uint32_t size, bool bigEndian) {
  const uint8_t be = bigEndian ? 1 : 0;
  XXH3_64bits_update(g_stream, &be, 1);
  XXH3_64bits_update(g_stream, data, size);
  g_streamBytes += size;
}

mpt::gx::GpuApi parse_api(const std::string& s) {
  if (s == "d3d12") return mpt::gx::GpuApi::d3d12;
  if (s == "vulkan") return mpt::gx::GpuApi::vulkan;
  if (s == "d3d11") return mpt::gx::GpuApi::d3d11;
  if (s == "dawn_null") return mpt::gx::GpuApi::dawn_null;
  return mpt::gx::GpuApi::automatic;
}

#ifdef MPT_GX_TEST_PNG
void write_png(const fs::path& path, const mpt::gx::FrameImage& f) {
  png_image img{};
  img.version = PNG_IMAGE_VERSION;
  img.width = f.width;
  img.height = f.height;
  img.format = PNG_FORMAT_RGBA;
  png_image_write_to_file(&img, path.string().c_str(), 0, f.rgba.data(), static_cast<png_int_32>(f.width * 4), nullptr);
}
#endif

using ProbeKey = std::tuple<std::string, int, int>;
struct Rgba {
  int r, g, b, a;
};
}  // namespace

int main(int argc, char** argv) {
  mpt::gx::RendererConfig config;
  fs::path out = "gx_scenes_out";
  fs::path golden;
  int frames = 3;
  int tolerance = 8;
  bool png = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string{}; };
    if (a == "--out") out = next();
    else if (a == "--frames") frames = std::atoi(next().c_str());
    else if (a == "--scale") config.internal_scale = static_cast<uint32_t>(std::atoi(next().c_str()));
    else if (a == "--api") config.api = parse_api(next());
    else if (a == "--png") png = true;
    else if (a == "--allow-software-adapter") config.allow_software_adapter = true;
    else if (a == "--check-probes") golden = next();
    else if (a == "--tolerance") tolerance = std::atoi(next().c_str());
    else {
      std::fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  std::error_code ec;
  fs::create_directories(out, ec);
  config.cache_dir = (out / "cache").string();
  fs::remove_all(config.cache_dir, ec);
  fs::create_directories(config.cache_dir, ec);

  arena_init();
  std::string error;
  auto renderer = mpt::gx::create_renderer(config, error);
  if (!renderer) {
    std::fprintf(stderr, "renderer: %s\n", error.c_str());
    return 3;
  }
  GXInit(nullptr, 0);
  aurora::gx::fifo::g_processObserver = observe;
  g_stream = XXH3_createState();

  std::ofstream res(out / "results.txt", std::ios::binary | std::ios::trunc);
  res << "# " << renderer->description() << " scale=" << config.internal_scale << " frames=" << frames << "\n";
  std::map<ProbeKey, Rgba> measured;
  for (const Scene& scene : scenes()) {
    for (int f = 0; f < frames; ++f) {
      XXH3_64bits_reset(g_stream);
      g_streamBytes = 0;
      renderer->begin_frame();
      scene.draw();
      mpt::gx::FrameImage image;
      renderer->end_frame(&image);
      const uint64_t stream = XXH3_64bits_digest(g_stream);
      const uint64_t gx = XXH3_64bits(__gx, sizeof(*__gx));
      const uint64_t img = image.rgba.empty() ? 0 : XXH3_64bits(image.rgba.data(), image.rgba.size());
      char line[256];
      std::snprintf(line, sizeof line, "%s frame=%d gx=%016llx stream=%016llx bytes=%llu img=%016llx %ux%u\n",
                    scene.name, f, static_cast<unsigned long long>(gx), static_cast<unsigned long long>(stream),
                    static_cast<unsigned long long>(g_streamBytes), static_cast<unsigned long long>(img),
                    image.width, image.height);
      res << line;
      if (f == frames - 1 && !image.rgba.empty()) {
#ifdef MPT_GX_TEST_PNG
        if (png) write_png(out / (std::string(scene.name) + ".png"), image);
#endif
        for (const auto& [x, y] : scene.probes) {
          const uint32_t px = static_cast<uint32_t>(x) * config.internal_scale + config.internal_scale / 2;
          const uint32_t py = static_cast<uint32_t>(y) * config.internal_scale + config.internal_scale / 2;
          const uint8_t* p = &image.rgba[(static_cast<size_t>(py) * image.width + px) * 4];
          measured[{scene.name, x, y}] = {p[0], p[1], p[2], p[3]};
          res << "probe " << scene.name << " " << x << " " << y << " " << int(p[0]) << " " << int(p[1]) << " "
              << int(p[2]) << " " << int(p[3]) << "\n";
        }
      }
    }
  }
  res.close();
  renderer.reset();
  (void)png;

  if (golden.empty()) {
    return 0;
  }
  // Golden probe check (G-d with a tolerance: GPU output is not bit-identical across GPUs and drivers).
  std::ifstream in(golden);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", golden.string().c_str());
    return 4;
  }
  int failures = 0;
  int checked = 0;
  std::string text;
  while (std::getline(in, text)) {
    if (text.empty() || text[0] == '#') continue;
    std::istringstream ls(text);
    std::string tag, name;
    int x, y;
    Rgba want{};
    if (!(ls >> tag >> name >> x >> y >> want.r >> want.g >> want.b >> want.a) || tag != "probe") continue;
    const auto it = measured.find({name, x, y});
    if (it == measured.end()) {
      std::fprintf(stderr, "probe %s %d %d: not measured\n", name.c_str(), x, y);
      ++failures;
      continue;
    }
    ++checked;
    const Rgba& got = it->second;
    if (std::abs(got.r - want.r) > tolerance || std::abs(got.g - want.g) > tolerance ||
        std::abs(got.b - want.b) > tolerance || std::abs(got.a - want.a) > tolerance) {
      std::fprintf(stderr, "probe %s (%d,%d): got %d %d %d %d, want %d %d %d %d (tolerance %d)\n", name.c_str(), x, y,
                   got.r, got.g, got.b, got.a, want.r, want.g, want.b, want.a, tolerance);
      ++failures;
    }
  }
  std::printf("%d probes checked, %d failed\n", checked, failures);
  return failures == 0 && checked > 0 ? 0 : 1;
}
