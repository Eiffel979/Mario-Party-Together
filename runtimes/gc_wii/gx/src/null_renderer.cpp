// Null renderer (MPT_GX_BACKEND=null): the front end's command processor parses every FIFO byte, and
// the renderer entry points it calls are aurora's no-op stub table (patched tests/gx_test_stubs.cpp,
// patch 0014). No GPU, no Dawn library.
#include "mpt/gx/renderer.h"

#include "gx/fifo.hpp"
#include "gx/gx.hpp"

#include <atomic>

namespace mpt::gx {
namespace {

std::atomic<bool> g_live{false};

class NullRenderer final : public Renderer {
 public:
  ~NullRenderer() override { g_live.store(false); }
  void begin_frame() override {}
  void end_frame(FrameImage* out) override {
    aurora::gx::fifo::drain();
    if (out != nullptr) {
      *out = FrameImage{};
    }
  }
  void reset_for_load_state() override { aurora::gx::reset_caches_for_load_state(); }
  std::string_view description() const override { return "null"; }
};

}  // namespace

std::unique_ptr<Renderer> create_renderer(const RendererConfig&, std::string& error) {
  if (g_live.exchange(true)) {
    error = "A GX renderer already exists in this process.";
    return nullptr;
  }
  return std::make_unique<NullRenderer>();
}

bool renderer_draws() { return false; }

}  // namespace mpt::gx
