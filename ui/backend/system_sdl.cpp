#include "system_sdl.h"

#include <filesystem>

namespace mpt::ui {

void HostSystemInterface::set_host_time(std::uint64_t ns) {
  if (!started_) {
    start_ns_ = ns;
    started_ = true;
  }
  now_ns_ = ns;
}

double HostSystemInterface::GetElapsedTime() {
  return started_ ? static_cast<double>(now_ns_ - start_ns_) / 1e9 : 0.0;
}

bool HostSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
  const char* level = nullptr;
  switch (type) {
    case Rml::Log::LT_ERROR:
    case Rml::Log::LT_ASSERT: level = "error"; break;
    case Rml::Log::LT_WARNING: level = "warning"; break;
    default: break;
  }
  if (level) std::fprintf(stderr, "ui: rmlui %s: %s\n", level, message.c_str());
  return true;
}

Rml::FileHandle Utf8FileInterface::Open(const Rml::String& path) {
  const std::filesystem::path p(std::u8string(path.begin(), path.end()));
#ifdef _WIN32
  FILE* file = nullptr;
  if (_wfopen_s(&file, p.c_str(), L"rb") != 0) file = nullptr;
#else
  FILE* file = std::fopen(p.c_str(), "rb");
#endif
  return reinterpret_cast<Rml::FileHandle>(file);
}

void Utf8FileInterface::Close(Rml::FileHandle file) { std::fclose(reinterpret_cast<FILE*>(file)); }

size_t Utf8FileInterface::Read(void* buffer, size_t size, Rml::FileHandle file) {
  return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
}

bool Utf8FileInterface::Seek(Rml::FileHandle file, long offset, int origin) {
  return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
}

size_t Utf8FileInterface::Tell(Rml::FileHandle file) {
  const long at = std::ftell(reinterpret_cast<FILE*>(file));
  return at < 0 ? 0 : static_cast<size_t>(at);
}

}  // namespace mpt::ui
