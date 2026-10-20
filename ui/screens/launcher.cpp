// The launcher overlay (P1-23): RmlUi documents bound to the ui/model flows.
//
// Determinism (ADR 0010 §3): this file never sees a runtime, an adapter, a Session, a
// SaveState, a replay or the seed. It asks the shell for start, pause, resume, stop and
// quit through IShellCommands, and reads the session only through SessionStatus. Host
// time is used for animation and key repeat only.

#include "mpt/ui/launcher.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "input_sdl.h"
#include "durable_file.h"
#include "mpt/asset_import/known_dumps.h"
#include "mpt/input/sdl_input.h"
#include "mpt/ui/model/file_browser.h"
#include "mpt/ui/model/import_flow.h"
#include "mpt/ui/model/launcher_flow.h"
#include "mpt/ui/model/library.h"
#include "mpt/ui/model/nav_repeat.h"
#include "mpt/ui/model/seating.h"
#include "render_sdl.h"
#include "system_sdl.h"

namespace mpt::ui {
namespace {

namespace fs = std::filesystem;

// ---- View data bound to the documents ----------------------------------------------

struct TitleRow {
  Rml::String name;
  Rml::String detail;
  Rml::String status;
  bool operator==(const TitleRow&) const = default;
};
struct FileRow {
  Rml::String label;
  Rml::String kind;  // "up", "root", "folder", "file"
  bool operator==(const FileRow&) const = default;
};
struct DeviceRow {
  Rml::String name;
  Rml::String port;
  bool operator==(const DeviceRow&) const = default;
};
struct PortRow {
  Rml::String label;
  Rml::String summary;
  bool seated = false;
  bool operator==(const PortRow&) const = default;
};

struct View {
  std::vector<TitleRow> titles;
  bool has_titles = false;
  Rml::String library_message;

  Rml::String files_location;
  std::vector<FileRow> files;
  Rml::String files_error;

  Rml::String imp_title;    // "Adding a game" / "Checking your game"
  Rml::String imp_phase;    // "busy", "done", "rejected"
  Rml::String imp_file;
  Rml::String imp_stage;
  Rml::String imp_width = "0%";  // progress bar width, e.g. "42%"
  Rml::String imp_heading;
  Rml::String imp_message;

  Rml::String seat_title;
  std::vector<DeviceRow> devices;
  std::vector<PortRow> ports;
  bool seat_ready = false;
  bool seat_starting = false;
  Rml::String seat_note;
  Rml::String seat_status;

  Rml::String pause_title;
};

// ---- The native file dialog's answer arrives on another thread --------------------

struct DialogInbox {
  std::mutex mutex;
  std::optional<std::string> path;
};
DialogInbox& dialog_inbox() {
  static DialogInbox inbox;  // outlives any launcher, so a late callback is harmless
  return inbox;
}

void SDLCALL on_dialog_result(void*, const char* const* files, int) {
  if (!files || !files[0]) return;  // error or cancelled
  DialogInbox& inbox = dialog_inbox();
  const std::lock_guard lock(inbox.mutex);
  inbox.path = files[0];
}

const char* const kDocuments[] = {"library", "add_dump", "files", "import", "seating", "pause"};

const char* document_for(Screen screen) {
  switch (screen) {
    case Screen::library: return "library";
    case Screen::add_dump: return "add_dump";
    case Screen::file_browser: return "files";
    case Screen::import: return "import";
    case Screen::seating: return "seating";
    case Screen::pause: return "pause";
    case Screen::playing: return nullptr;
  }
  return nullptr;
}

fs::path find_asset_root(const fs::path& requested) {
  if (!requested.empty()) return requested;
  if (const char* base = SDL_GetBasePath()) {
    const fs::path next_to_exe = from_utf8(base) / "ui";
    std::error_code ec;
    if (fs::is_regular_file(next_to_exe / "screens" / "library.rml", ec)) return next_to_exe;
  }
  return from_utf8(MPT_UI_SOURCE_DIR);
}

bool is_pause_action(const SDL_Event& e) {
  // input/'s host-reserved binding (Esc, Guide; ADR 0010 §3.2), the same one the shell
  // routes with.
  return input::SdlInput::host_action(e) == input::HostAction::pause;
}

std::optional<NavDir> dpad_dir(Uint8 button) {
  switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return NavDir::up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return NavDir::down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return NavDir::left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return NavDir::right;
    default: return std::nullopt;
  }
}

std::string percent(double fraction) {
  const int p = static_cast<int>(fraction * 100.0 + 0.5);
  return std::to_string(p < 0 ? 0 : (p > 100 ? 100 : p)) + "%";
}

// ---- The launcher ------------------------------------------------------------------

class Launcher final : public shell::IOverlay {
 public:
  Launcher(const shell::UiHost& host, fs::path root)
      : host_(host),
        root_(std::move(root)),
        renderer_(SDL_GetRenderer(host.window)),
        render_(renderer_),
        backend_(host.known_dumps, host.cache_dir, host.recipe),
        import_(backend_),
        seating_(*host.ports) {}

  ~Launcher() override {
    if (context_) Rml::RemoveContext(context_->GetName());
    if (initialised_) Rml::Shutdown();
  }

  bool init() {
    if (!renderer_ || !host_.commands || !host_.ports || !host_.status) {
      std::fprintf(stderr, "ui: the shell did not provide a window, renderer, commands, ports or status\n");
      return false;
    }
    Rml::SetRenderInterface(&render_);
    Rml::SetSystemInterface(&system_);
    Rml::SetFileInterface(&files_);
    if (!Rml::Initialise()) {
      std::fprintf(stderr, "ui: RmlUi did not initialise\n");
      return false;
    }
    initialised_ = true;
    const fs::path font = root_ / "assets" / "fonts" / "LatoLatin-Regular.ttf";
    if (!Rml::LoadFontFace(to_utf8(font), /*fallback_face=*/true)) {
      std::fprintf(stderr, "ui: could not load the font %s\n", shell::display_path(font).c_str());
      return false;
    }
    int w = 0;
    int h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    context_ = Rml::CreateContext("launcher", Rml::Vector2i(w > 0 ? w : 1280, h > 0 ? h : 720));
    if (!context_ || !bind_model()) {
      std::fprintf(stderr, "ui: could not create the launcher context\n");
      return false;
    }
    for (const char* name : kDocuments) {
      const fs::path file = root_ / "screens" / (std::string(name) + ".rml");
      Rml::ElementDocument* doc = context_->LoadDocument(to_utf8(file));
      if (!doc) {
        std::fprintf(stderr, "ui: could not load %s\n", shell::display_path(file).c_str());
        return false;
      }
      docs_[name] = doc;
    }
    std::string warning;
    library_ = Library::load(library_file(), warning);
    if (!warning.empty()) message_ = warning;
    return true;
  }

  // ---- IOverlay ----

  bool hasFocus() const override { return flow_.has_focus(); }

  bool handleEvent(const SDL_Event& e) override {
    if (!hasFocus()) {
      // While the game runs only the host pause action is offered (ADR 0010 §3.2).
      if (is_pause_action(e) && flow_.open_pause()) {
        host_.commands->requestPause();
        nav_.clear();
        return true;
      }
      return false;
    }
    switch (e.type) {
      case SDL_EVENT_KEY_DOWN: on_key(e.key, true); break;
      case SDL_EVENT_KEY_UP: on_key(e.key, false); break;
      case SDL_EVENT_MOUSE_MOTION:
        context_->ProcessMouseMove(static_cast<int>(e.motion.x * mouse_scale_), static_cast<int>(e.motion.y * mouse_scale_),
                                   rml_modifiers(SDL_GetModState()));
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int button = e.button.button == SDL_BUTTON_LEFT ? 0 : (e.button.button == SDL_BUTTON_RIGHT ? 1 : 2);
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          context_->ProcessMouseButtonDown(button, rml_modifiers(SDL_GetModState()));
        } else {
          context_->ProcessMouseButtonUp(button, rml_modifiers(SDL_GetModState()));
        }
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL:
        context_->ProcessMouseWheel(Rml::Vector2f(-e.wheel.x, -e.wheel.y), rml_modifiers(SDL_GetModState()));
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: on_pad_button(e.gbutton, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN); break;
      case SDL_EVENT_GAMEPAD_AXIS_MOTION: on_pad_axis(e.gaxis); break;
      case SDL_EVENT_GAMEPAD_REMOVED: nav_.clear(); break;
      case SDL_EVENT_DROP_FILE:
        if (e.drop.data) on_file_chosen(from_utf8(e.drop.data));
        break;
      default: break;
    }
    return true;
  }

  void update(std::uint64_t now_ns) override {
    now_ns_ = now_ns;
    system_.set_host_time(now_ns);

    const shell::SessionStatus status = host_.status();
    flow_.sync(SessionView{status.running, status.paused, status.start_failures, status.last_error});
    // Phase 1: seats change only while no session runs (the shell also turns input/'s
    // auto-assign off for the session).
    seating_.set_locked(status.running);

    if (import_.poll()) on_import_changed();
    take_dialog_result();
    if (flow_.has_focus()) {
      for (NavDir dir : nav_.tick(now_ns)) send_key(rml_key(dir));
    }

    // Size and density follow the window: a 10-foot layout designed at 720p (ADR 0010 §5).
    int w = 0;
    int h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    if (w > 0 && h > 0) {
      context_->SetDimensions(Rml::Vector2i(w, h));
      context_->SetDensityIndependentPixelRatio(static_cast<float>(h) / 720.0f);
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(host_.window, &ww, &wh);
      mouse_scale_ = ww > 0 ? static_cast<float>(w) / static_cast<float>(ww) : 1.0f;
    }

    show_current_document();
    refresh_view();
    context_->Update();
    ensure_focus();
  }

  void render(SDL_Renderer* renderer, int, int) override {
    if (renderer != renderer_ || flow_.screen() == Screen::playing) return;
    render_.begin_frame();
    context_->Render();
    render_.end_frame();
  }

 private:
  fs::path library_file() const { return host_.user_dir / "library.json"; }

  // ---- Input ----

  void send_key(Rml::Input::KeyIdentifier key) {
    context_->ProcessKeyDown(key, 0);
    context_->ProcessKeyUp(key, 0);
  }

  void on_key(const SDL_KeyboardEvent& key, bool down) {
    if (key.key == SDLK_ESCAPE || key.key == SDLK_BACKSPACE) {
      if (down && !key.repeat) on_back();
      return;
    }
    // A keyboard that is not seated joins with Enter or Space on the seating screen.
    if (down && !key.repeat && flow_.screen() == Screen::seating && (key.key == SDLK_RETURN || key.key == SDLK_SPACE) &&
        seating_.join(input::kKeyboardDevice)) {
      return;
    }
    const std::optional<Rml::Input::KeyIdentifier> k = rml_key(key.key);
    if (!k) return;
    const int mods = rml_modifiers(key.mod);
    if (down) {
      context_->ProcessKeyDown(*k, mods);
    } else {
      context_->ProcessKeyUp(*k, mods);
    }
  }

  void on_pad_button(const SDL_GamepadButtonEvent& b, bool down) {
    if (const std::optional<NavDir> dir = dpad_dir(b.button)) {
      if (const std::optional<NavDir> step = nav_.dpad(*dir, down, now_ns_)) send_key(rml_key(*step));
      return;
    }
    if (!down) {
      if (b.button == SDL_GAMEPAD_BUTTON_SOUTH) context_->ProcessKeyUp(Rml::Input::KI_RETURN, 0);
      return;
    }
    // "Press any button to join" (ADR 0010 §5): a controller that is not seated takes
    // a seat, and that press does nothing else.
    const input::DeviceId pad{input::DeviceKind::gamepad, static_cast<std::uint32_t>(b.which)};
    if (flow_.screen() == Screen::seating && seating_.join(pad)) return;
    switch (b.button) {
      case SDL_GAMEPAD_BUTTON_SOUTH: context_->ProcessKeyDown(Rml::Input::KI_RETURN, 0); break;  // confirm, by position
      case SDL_GAMEPAD_BUTTON_EAST:                                                            // back, by position
      case SDL_GAMEPAD_BUTTON_BACK:
      case SDL_GAMEPAD_BUTTON_GUIDE: on_back(); break;
      default: break;
    }
  }

  void on_pad_axis(const SDL_GamepadAxisEvent& a) {
    if (a.axis == SDL_GAMEPAD_AXIS_LEFTX) stick_x_ = a.value;
    else if (a.axis == SDL_GAMEPAD_AXIS_LEFTY) stick_y_ = a.value;
    else return;
    if (const std::optional<NavDir> step = nav_.stick(stick_x_, stick_y_, now_ns_)) send_key(rml_key(*step));
  }

  void on_back() {
    switch (flow_.screen()) {
      case Screen::import:
        if (import_.busy()) {
          import_.cancel();
          return;
        }
        import_.reset();
        break;
      case Screen::seating:
        if (flow_.starting()) return;
        break;
      case Screen::pause:
        resume();  // back on the pause screen resumes
        return;
      default: break;
    }
    flow_.back();
  }

  // Held buttons (the confirm press) are masked by the shell's GameInputGate when game
  // input goes live again (ADR 0010 §3.3), so the UI resumes at once.
  void resume() {
    if (flow_.resume()) host_.commands->requestResume();
  }

  // ---- Actions ----

  void on_file_chosen(const fs::path& path) {
    const Screen s = flow_.screen();
    if (import_.busy() || !(s == Screen::library || s == Screen::add_dump || s == Screen::file_browser ||
                            s == Screen::import)) {
      return;
    }
    last_folder_ = path.parent_path();
    verifying_ = std::nullopt;
    import_.reset();
    import_.start(path);
    if (s != Screen::import) flow_.go(Screen::import);
  }

  void play(std::size_t index) {
    LibraryEntry* entry = library_.at(index);
    if (!entry) return;
    selected_ = index;
    message_.clear();
    if (entry->verified) {
      flow_.go(Screen::seating);
      return;
    }
    // After a restart the dump is checked again, and the build is a cache hit. The
    // VerifiedDump always comes from make_verified (ADR 0009, P1-06).
    verifying_ = index;
    import_.reset();
    import_.start(entry->dump_path, entry->sha256);
    flow_.go(Screen::import);
  }

  void on_import_changed() {
    if (import_.state() != ImportFlow::State::done || !import_.result()) return;
    const ImportResult& result = *import_.result();
    if (verifying_) {
      if (LibraryEntry* entry = library_.at(*verifying_)) entry->verified = result.dump;
      selected_ = *verifying_;
      verifying_.reset();
      import_.reset();
      flow_.to_library();
      flow_.go(Screen::seating);
      return;
    }
    selected_ = library_.add(result.dump, result.path);
    save_library();
  }

  // Durable and atomic (core/shell/durable_file.h): a crash leaves the old list or the
  // new one, never a mix.
  void save_library() {
    const std::string text = library_.to_json();
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    if (Result written = shell::write_file_atomic(library_file(), std::span<const std::byte>(bytes, text.size()));
        !written) {
      message_ = "Your library list could not be saved: " + written.error();
      std::fprintf(stderr, "ui: library: %s\n", written.error().c_str());
    }
  }

  void start_party() {
    LibraryEntry* entry = selected_ ? library_.at(*selected_) : nullptr;
    if (!entry || !entry->verified || !seating_.ready() || seating_.locked() || flow_.starting()) return;
    flow_.begin_start(host_.status().start_failures);
    shell::StartRequest request;
    request.dump = *entry->verified;  // from asset_import::make_verified; the shell picks the seed
    host_.commands->requestStart(request);
  }

  void open_native_dialog() {
    static const SDL_DialogFileFilter kFilters[] = {
        {"Game dumps", "iso;gcm;z64;n64;v64;rvz;wia;gcz;wbfs;ciso"},
        {"All files", "*"},
    };
    const std::string start = last_folder_.empty() ? std::string() : to_utf8(last_folder_);
    SDL_ShowOpenFileDialog(&on_dialog_result, nullptr, host_.window, kFilters, 2, start.empty() ? nullptr : start.c_str(),
                           false);
  }

  void take_dialog_result() {
    std::optional<std::string> path;
    {
      DialogInbox& inbox = dialog_inbox();
      const std::lock_guard lock(inbox.mutex);
      path.swap(inbox.path);
    }
    if (path) on_file_chosen(from_utf8(*path));
  }

  // ---- Data model ----

  bool bind_model() {
    Rml::DataModelConstructor c = context_->CreateDataModel("ui");
    if (!c) return false;
    if (auto s = c.RegisterStruct<TitleRow>()) {
      s.RegisterMember("name", &TitleRow::name);
      s.RegisterMember("detail", &TitleRow::detail);
      s.RegisterMember("status", &TitleRow::status);
    }
    if (auto s = c.RegisterStruct<FileRow>()) {
      s.RegisterMember("label", &FileRow::label);
      s.RegisterMember("kind", &FileRow::kind);
    }
    if (auto s = c.RegisterStruct<DeviceRow>()) {
      s.RegisterMember("name", &DeviceRow::name);
      s.RegisterMember("port", &DeviceRow::port);
    }
    if (auto s = c.RegisterStruct<PortRow>()) {
      s.RegisterMember("label", &PortRow::label);
      s.RegisterMember("summary", &PortRow::summary);
      s.RegisterMember("seated", &PortRow::seated);
    }
    c.RegisterArray<std::vector<TitleRow>>();
    c.RegisterArray<std::vector<FileRow>>();
    c.RegisterArray<std::vector<DeviceRow>>();
    c.RegisterArray<std::vector<PortRow>>();

    c.Bind("titles", &view_.titles);
    c.Bind("has_titles", &view_.has_titles);
    c.Bind("library_message", &view_.library_message);
    c.Bind("files_location", &view_.files_location);
    c.Bind("files", &view_.files);
    c.Bind("files_error", &view_.files_error);
    c.Bind("imp_title", &view_.imp_title);
    c.Bind("imp_phase", &view_.imp_phase);
    c.Bind("imp_file", &view_.imp_file);
    c.Bind("imp_stage", &view_.imp_stage);
    c.Bind("imp_width", &view_.imp_width);
    c.Bind("imp_heading", &view_.imp_heading);
    c.Bind("imp_message", &view_.imp_message);
    c.Bind("seat_title", &view_.seat_title);
    c.Bind("devices", &view_.devices);
    c.Bind("ports", &view_.ports);
    c.Bind("seat_ready", &view_.seat_ready);
    c.Bind("seat_starting", &view_.seat_starting);
    c.Bind("seat_note", &view_.seat_note);
    c.Bind("seat_status", &view_.seat_status);
    c.Bind("pause_title", &view_.pause_title);

    const auto on = [&c](const char* name, std::function<void(const Rml::VariantList&)> fn) {
      c.BindEventCallback(name, [fn = std::move(fn)](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        fn(args);
      });
    };
    const auto index_arg = [](const Rml::VariantList& args, std::size_t i) -> int {
      return i < args.size() ? args[i].Get<int>(-1) : -1;
    };
    on("back", [this](const Rml::VariantList&) { on_back(); });
    on("exit_app", [this](const Rml::VariantList&) { host_.commands->requestQuit(); });
    on("add_dump", [this](const Rml::VariantList&) {
      message_.clear();
      flow_.go(Screen::add_dump);
    });
    on("play", [this, index_arg](const Rml::VariantList& a) {
      if (const int i = index_arg(a, 0); i >= 0) play(static_cast<std::size_t>(i));
    });
    on("browse_native", [this](const Rml::VariantList&) { open_native_dialog(); });
    on("browse_list", [this](const Rml::VariantList&) {
      browser_.open(last_folder_);
      flow_.go(Screen::file_browser);
    });
    on("file_open", [this, index_arg](const Rml::VariantList& a) {
      const int i = index_arg(a, 0);
      if (i < 0) return;
      if (const std::optional<fs::path> file = browser_.activate(static_cast<std::size_t>(i))) on_file_chosen(*file);
    });
    on("import_cancel", [this](const Rml::VariantList&) { import_.cancel(); });
    on("import_retry", [this](const Rml::VariantList&) {
      import_.reset();
      flow_.to_library();
      flow_.go(Screen::add_dump);
    });
    on("import_library", [this](const Rml::VariantList&) {
      import_.reset();
      verifying_.reset();
      flow_.to_library();
    });
    on("import_play", [this](const Rml::VariantList&) {
      import_.reset();
      flow_.to_library();
      if (selected_) flow_.go(Screen::seating);
    });
    on("seat_cycle", [this, index_arg](const Rml::VariantList& a) {
      const int i = index_arg(a, 0);
      const std::vector<SeatDevice> devices = seating_.devices();
      if (i < 0 || static_cast<std::size_t>(i) >= devices.size()) return;
      seating_.cycle(devices[static_cast<std::size_t>(i)].id, index_arg(a, 1) < 0 ? -1 : 1);
    });
    on("start_party", [this](const Rml::VariantList&) { start_party(); });
    on("resume", [this](const Rml::VariantList&) { resume(); });
    on("quit_library", [this](const Rml::VariantList&) {
      flow_.quit_to_library();
      host_.commands->requestStop();
    });
    model_ = c.GetModelHandle();
    return true;
  }

  template <typename T>
  void set(const char* name, T& field, T value) {
    if (field == value) return;
    field = std::move(value);
    model_.DirtyVariable(name);
  }

  void refresh_view() {
    // Library
    std::vector<TitleRow> titles;
    for (const LibraryEntry& e : library_.entries()) {
      titles.push_back(TitleRow{e.display_name(), e.detail(), e.verified ? "Ready" : "Select to check and play"});
    }
    set("has_titles", view_.has_titles, !titles.empty());
    set("titles", view_.titles, std::move(titles));
    set("library_message", view_.library_message, Rml::String(flow_.message().empty() ? message_ : flow_.message()));

    // File list
    std::vector<FileRow> files;
    for (const FileEntry& f : browser_.entries()) {
      const char* kind = f.kind == FileEntry::Kind::up       ? "up"
                         : f.kind == FileEntry::Kind::root   ? "root"
                         : f.kind == FileEntry::Kind::folder ? "folder"
                                                             : "file";
      files.push_back(FileRow{f.label, kind});
    }
    set("files", view_.files, std::move(files));
    set("files_location", view_.files_location, browser_.location());
    set("files_error", view_.files_error, browser_.error());

    // Import
    const ImportFlow::State st = import_.state();
    set("imp_title", view_.imp_title, Rml::String(verifying_ ? "Checking your game" : "Adding a game"));
    set("imp_phase", view_.imp_phase,
        Rml::String(st == ImportFlow::State::done ? "done" : st == ImportFlow::State::rejected ? "rejected" : "busy"));
    set("imp_file", view_.imp_file, import_.path().empty() ? std::string() : to_utf8(import_.path().filename()));
    std::string stage = import_.progress().stage;
    if (st == ImportFlow::State::cancelling) stage = "Stopping...";
    if (!import_.progress().detail.empty()) stage += ": " + import_.progress().detail;
    set("imp_stage", view_.imp_stage, stage);
    set("imp_width", view_.imp_width, percent(st == ImportFlow::State::identifying ? 0.0 : import_.progress().overall()));
    std::string heading;
    std::string message;
    if (const std::optional<Rejection>& r = import_.rejection()) {
      heading = r->heading;
      message = r->message;
    } else if (const std::optional<ImportResult>& done = import_.result()) {
      const std::string name(asset_import::title_display_name(done->known.title));
      heading = name + " is ready";
      message = "Verified: " + name + " (" + done->known.region + ", " + done->known.revision + ")." +
                (done->reused_build ? " The local build was already up to date." : " It was built on this computer.");
    } else if (const std::optional<asset_import::KnownDump>& known = import_.identified()) {
      heading = std::string(asset_import::title_display_name(known->title)) + " (" + known->region + ", " +
                known->revision + ")";
    }
    set("imp_heading", view_.imp_heading, heading);
    set("imp_message", view_.imp_message, message);

    // Seating
    const LibraryEntry* entry = selected_ ? library_.at(*selected_) : nullptr;
    set("seat_title", view_.seat_title, entry ? entry->display_name() : std::string());
    std::vector<DeviceRow> devices;
    for (const SeatDevice& d : seating_.devices()) devices.push_back(DeviceRow{d.name, d.port_label()});
    set("devices", view_.devices, std::move(devices));
    std::vector<PortRow> ports;
    const auto seats = seating_.ports();
    for (std::size_t p = 0; p < seats.size(); ++p) {
      ports.push_back(PortRow{"Player " + std::to_string(p + 1), seats[p].summary(), seats[p].seated});
    }
    set("ports", view_.ports, std::move(ports));
    set("seat_ready", view_.seat_ready,
        seating_.ready() && !seating_.locked() && entry && entry->verified.has_value());
    set("seat_starting", view_.seat_starting, flow_.starting());
    set("seat_note", view_.seat_note,
        seating_.locked() ? std::string("Seats are locked while a game is running.") : seating_.cpu_note());
    set("seat_status", view_.seat_status,
        Rml::String(flow_.starting() ? "Starting the game..." : (flow_.screen() == Screen::seating ? flow_.message() : "")));

    // Pause
    set("pause_title", view_.pause_title, entry ? entry->display_name() : std::string());
  }

  // ---- Documents and focus ----

  void show_current_document() {
    const char* want = document_for(flow_.screen());
    if (shown_ == want) return;
    for (auto& [name, doc] : docs_) {
      if (!want || name != want) doc->Hide();
    }
    if (want) docs_[want]->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
    shown_ = want;
    nav_.clear();
  }

  // Every screen opens with a focused element, so a controller user is never stuck
  // (ADR 0010 §5). Lists made by data-for only exist after Update(), so this runs after it.
  void ensure_focus() {
    if (!shown_) return;
    Rml::ElementDocument* doc = docs_[shown_];
    Rml::Element* focus = context_->GetFocusElement();
    if (focus && focus != doc && focus->GetOwnerDocument() == doc && focus->IsVisible(true)) return;
    for (const char* selector : {"[autofocus]", ".btn"}) {
      Rml::ElementList candidates;
      doc->QuerySelectorAll(candidates, selector);
      for (Rml::Element* el : candidates) {
        if (el->IsVisible(true) && el->Focus(true)) return;
      }
    }
  }

  const shell::UiHost host_;
  const fs::path root_;
  SDL_Renderer* renderer_;
  SdlRenderInterface render_;
  HostSystemInterface system_;
  Utf8FileInterface files_;
  bool initialised_ = false;
  Rml::Context* context_ = nullptr;
  Rml::DataModelHandle model_;
  std::map<std::string, Rml::ElementDocument*> docs_;
  const char* shown_ = nullptr;

  AssetImportBackend backend_;
  ImportFlow import_;
  Seating seating_;
  LauncherFlow flow_;
  Library library_;
  FileBrowser browser_;
  NavRepeat nav_;
  View view_;

  std::optional<std::size_t> selected_;   // the library entry being played
  std::optional<std::size_t> verifying_;  // re-checking this entry
  fs::path last_folder_;
  std::string message_;
  std::uint64_t now_ns_ = 0;
  int stick_x_ = 0;
  int stick_y_ = 0;
  float mouse_scale_ = 1.0f;
};

}  // namespace

std::unique_ptr<shell::IOverlay> make_launcher(const shell::UiHost& host, const LauncherOptions& options) {
  auto launcher = std::make_unique<Launcher>(host, find_asset_root(options.asset_root));
  if (!launcher->init()) return nullptr;
  return launcher;
}

}  // namespace mpt::ui
