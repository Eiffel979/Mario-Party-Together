// Dawn renderer (MPT_GX_BACKEND=dawn): aurora's WebGPU renderer on a headless Dawn device, and the
// host shim that replaces aurora_core (ADR 0007 section 6). Presentation only: nothing here is
// game-visible, and no address from here is stored in game memory.
//
// The shim provides the symbols aurora's lib/gx, lib/gfx and lib/webgpu take from aurora_core:
// g_config, log_internal, the window queries (there is no window), the surface descriptor helper
// (unused without a surface) and vi::configured_fb_size (always the logical size).
#include "mpt/gx/renderer.h"

#include <aurora/aurora.h>
#include <dolphin/gx.h>

#include "dawn/BackendBinding.hpp"
#include "dolphin/vi/vi_internal.hpp"
#include "gfx/common.hpp"
#include "gx/fifo.hpp"
#include "gx/gx.hpp"
#include "internal.hpp"
#include "webgpu/gpu.hpp"
#include "window.hpp"

#include <fmt/format.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace {
uint32_t g_logicalW = 640;
uint32_t g_logicalH = 480;
uint32_t g_scale = 1;
}  // namespace

namespace aurora {
AuroraConfig g_config{};
void log_internal(AuroraLogLevel level, const char* module, const char* message, unsigned int len) noexcept {
  if (level >= LOG_WARNING) {
    std::fprintf(stderr, "[gx %d] %s: %.*s\n", static_cast<int>(level), module, static_cast<int>(len), message);
  }
}
}  // namespace aurora

auto fmt::formatter<AuroraLogLevel>::format(AuroraLogLevel level, format_context& ctx) const
    -> format_context::iterator {
  return fmt::format_to(ctx.out(), "{}", static_cast<int>(level));
}

namespace aurora::window {
SurfaceLock::SurfaceLock() noexcept {}
SurfaceLock::~SurfaceLock() {}
AuroraWindowSize get_window_size() {
  const uint32_t w = g_logicalW * g_scale;
  const uint32_t h = g_logicalH * g_scale;
  return {w, h, w, h, w, h, 1.0f};
}
SDL_Window* get_sdl_window() { return nullptr; }
bool is_presentable() noexcept { return false; }
bool push_custom_event(CustomEvent) { return false; }
void set_frame_buffer_aspect_fit(bool) {}
}  // namespace aurora::window

namespace aurora::webgpu::utils {
std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window*) { return nullptr; }
}  // namespace aurora::webgpu::utils

namespace aurora::vi {
Vec2<uint32_t> configured_fb_size() noexcept { return {g_logicalW, g_logicalH}; }
void configure(const GXRenderModeObj*) noexcept {}
}  // namespace aurora::vi

namespace aurora::gfx {
extern bool g_synchronousPipelines;  // patch 4
}

namespace mpt::gx {
namespace {

std::atomic<bool> g_live{false};

AuroraBackend to_aurora(GpuApi api) {
  switch (api) {
    case GpuApi::d3d12: return BACKEND_D3D12;
    case GpuApi::vulkan: return BACKEND_VULKAN;
    case GpuApi::d3d11: return BACKEND_D3D11;
    case GpuApi::dawn_null: return BACKEND_NULL;
    case GpuApi::automatic: break;
  }
  return BACKEND_AUTO;
}

const char* backend_name(wgpu::BackendType type) {
  switch (type) {
    case wgpu::BackendType::D3D12: return "D3D12";
    case wgpu::BackendType::D3D11: return "D3D11";
    case wgpu::BackendType::Vulkan: return "Vulkan";
    case wgpu::BackendType::Null: return "Null";
    case wgpu::BackendType::Metal: return "Metal";
    default: return "other";
  }
}

class DawnRenderer final : public Renderer {
 public:
  DawnRenderer() = default;
  ~DawnRenderer() override {
    if (initialized_) {
      aurora::gfx::gpu_synchronize();
      readback_ = {};
      aurora::gfx::shutdown();
      aurora::webgpu::shutdown();
    }
    if (!temp_cache_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(temp_cache_, ec);
    }
    g_live.store(false);
  }

  bool init(const RendererConfig& config, std::string& error) {
    if (config.logical_width == 0 || config.logical_height == 0 || config.internal_scale == 0 ||
        config.internal_scale > 8) {
      error = "Invalid GX renderer size or internal scale.";
      return false;
    }
    g_logicalW = config.logical_width;
    g_logicalH = config.logical_height;
    g_scale = config.internal_scale;
    cache_dir_ = config.cache_dir;
    if (cache_dir_.empty()) {
      // aurora always opens its caches under cachePath; never let that be the working directory.
      std::error_code ec;
      temp_cache_ = std::filesystem::temp_directory_path(ec) /
                    ("mpt-gx-cache-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
      std::filesystem::create_directories(temp_cache_, ec);
      cache_dir_ = temp_cache_.string();
    }
    aurora::g_config = AuroraConfig{};
    aurora::g_config.appName = "Mario Party Together";
    aurora::g_config.cachePath = cache_dir_.c_str();
    aurora::g_config.resourcesPath = "";
    aurora::g_config.msaa = 1;
    aurora::g_config.maxTextureAnisotropy = 16;
    aurora::g_config.logLevel = LOG_WARNING;
    aurora::g_config.allowCpuAdapter = config.api == GpuApi::dawn_null;
    // Synchronous pipelines in play and in tests (ADR 0007, P1-14 gate amendment): the asynchronous
    // mode skips draws while pipelines compile.
    aurora::gfx::g_synchronousPipelines = true;

    bool ok = false;
    if (config.api == GpuApi::automatic) {
#ifdef _WIN32
      for (const AuroraBackend b : {BACKEND_D3D12, BACKEND_VULKAN}) {
#else
      for (const AuroraBackend b : {BACKEND_VULKAN}) {
#endif
        if (aurora::webgpu::initialize(b, false)) {
          ok = true;
          break;
        }
      }
    } else {
      ok = aurora::webgpu::initialize(to_aurora(config.api), aurora::g_config.allowCpuAdapter);
    }
    if (!ok) {
      error = "Could not create a headless Dawn device for the GX renderer.";
      return false;
    }
    aurora::gfx::initialize();
    initialized_ = true;
    const auto type = aurora::webgpu::g_backendType;
    description_ = fmt::format("dawn {} {}x{}", backend_name(type), aurora::webgpu::g_frameBuffer.size.width,
                               aurora::webgpu::g_frameBuffer.size.height);
    if (type == wgpu::BackendType::D3D12) {
      // Patch 9: aurora no longer enables use_dxc, and Dawn's default is FXC.
      description_ += " (shaders: FXC)";
    }
    return true;
  }

  void begin_frame() override {
    if (!aurora::gfx::begin_frame()) {
      // Only fails if no staging buffer could be mapped, which cannot happen after gpu_synchronize.
      aurora::gfx::gpu_synchronize();
      if (!aurora::gfx::begin_frame()) {
        std::fprintf(stderr, "gx: begin_frame failed\n");
        std::abort();
      }
    }
  }

  void end_frame(FrameImage* out) override {
    aurora::gx::fifo::drain();
    aurora::gfx::finish();
    const bool wantImage = out != nullptr;
    aurora::gfx::end_frame([this, wantImage](wgpu::CommandEncoder& encoder) {
      // On aurora's render worker: copy the EFB into a MapRead buffer, submit, map synchronously.
      const auto& src = aurora::webgpu::present_source();
      const uint32_t w = src.size.width;
      const uint32_t h = src.size.height;
      const uint32_t rowBytes = w * 4;
      const uint32_t paddedRow = (rowBytes + 255u) & ~255u;
      const uint64_t size = static_cast<uint64_t>(paddedRow) * h;
      if (wantImage) {
        if (!readback_ || readback_size_ != size) {
          const wgpu::BufferDescriptor desc{
              .label = "EFB readback",
              .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
              .size = size,
          };
          readback_ = aurora::webgpu::g_device.CreateBuffer(&desc);
          readback_size_ = size;
        }
        const wgpu::TexelCopyTextureInfo srcInfo{.texture = src.texture};
        const wgpu::TexelCopyBufferInfo dstInfo{
            .layout = {.offset = 0, .bytesPerRow = paddedRow, .rowsPerImage = h},
            .buffer = readback_,
        };
        const wgpu::Extent3D extent{w, h, 1};
        encoder.CopyTextureToBuffer(&srcInfo, &dstInfo, &extent);
      }
      const wgpu::CommandBufferDescriptor cmdDesc{.label = "GX frame"};
      const auto cmd = encoder.Finish(&cmdDesc);
      aurora::webgpu::g_queue.Submit(1, &cmd);
      aurora::gfx::after_submit();
      if (!wantImage) {
        return;
      }
      bool mapped = false;
      const auto future = readback_.MapAsync(
          wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly,
          [&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { mapped = status == wgpu::MapAsyncStatus::Success; });
      aurora::webgpu::g_instance.WaitAny(future, UINT64_MAX);
      std::lock_guard lock{mutex_};
      frame_.width = w;
      frame_.height = h;
      frame_.rgba.assign(static_cast<size_t>(rowBytes) * h, 0);
      if (mapped) {
        const auto* p = static_cast<const uint8_t*>(readback_.GetConstMappedRange(0, size));
        for (uint32_t y = 0; y < h; ++y) {
          std::memcpy(frame_.rgba.data() + static_cast<size_t>(y) * rowBytes, p + static_cast<size_t>(y) * paddedRow,
                      rowBytes);
        }
        readback_.Unmap();
      }
    });
    aurora::gfx::gpu_synchronize();
    if (out != nullptr) {
      std::lock_guard lock{mutex_};
      *out = std::move(frame_);
      frame_ = FrameImage{};
    }
  }

  void reset_for_load_state() override {
    aurora::gfx::gpu_synchronize();
    aurora::gx::reset_caches_for_load_state();
    aurora::gfx::clear_caches();
  }

  std::string_view description() const override { return description_; }

 private:
  bool initialized_ = false;
  std::string cache_dir_;
  std::filesystem::path temp_cache_;
  std::string description_;
  std::mutex mutex_;
  FrameImage frame_;
  wgpu::Buffer readback_;
  uint64_t readback_size_ = 0;
};

}  // namespace

std::unique_ptr<Renderer> create_renderer(const RendererConfig& config, std::string& error) {
  if (g_live.exchange(true)) {
    error = "A GX renderer already exists in this process.";
    return nullptr;
  }
  auto renderer = std::make_unique<DawnRenderer>();
  if (!renderer->init(config, error)) {
    return nullptr;
  }
  return renderer;
}

bool renderer_draws() { return true; }

}  // namespace mpt::gx
