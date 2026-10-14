#pragma once

// RmlUi system and file interfaces for the launcher (ADR 0010 §2, §3.4).
// Time is host time handed in by IOverlay::update: it drives UI animation only.

#include <cstdint>
#include <cstdio>
#include <string>

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

namespace mpt::ui {

class HostSystemInterface final : public Rml::SystemInterface {
 public:
  void set_host_time(std::uint64_t ns);
  double GetElapsedTime() override;
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

 private:
  std::uint64_t start_ns_ = 0;
  std::uint64_t now_ns_ = 0;
  bool started_ = false;
};

// Opens UTF-8 paths correctly on every platform (RmlUi's default uses fopen).
class Utf8FileInterface final : public Rml::FileInterface {
 public:
  Rml::FileHandle Open(const Rml::String& path) override;
  void Close(Rml::FileHandle file) override;
  size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
  bool Seek(Rml::FileHandle file, long offset, int origin) override;
  size_t Tell(Rml::FileHandle file) override;
};

}  // namespace mpt::ui
