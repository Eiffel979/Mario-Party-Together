#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "mpt/ui/model/file_browser.h"

using namespace mpt::ui;
namespace fs = std::filesystem;

namespace {

// Empty placeholder files only: the browser looks at names, never content.
fs::path make_tree() {
  const fs::path root = fs::temp_directory_path() / "mpt_ui_model_tests" / "browser";
  fs::remove_all(root);
  fs::create_directories(root / "Games" / "Inner");
  fs::create_directories(root / ".hidden");
  for (const char* name : {"Games/b_disc.iso", "Games/A_disc.ISO", "Games/notes.txt", "Games/packed.rvz"}) {
    std::ofstream(root / name).put('x');
  }
  return root;
}

}  // namespace

TEST_CASE("the list shows folders, then dump files, and hides everything else") {
  const fs::path root = make_tree();
  FileBrowser browser([root] { return std::vector<fs::path>{root}; });
  REQUIRE(browser.entries().size() == 1);
  CHECK(browser.entries()[0].kind == FileEntry::Kind::root);
  CHECK(browser.location() == "This computer");

  CHECK_FALSE(browser.activate(0));  // into the root
  CHECK(browser.folder() == root);
  // up, Games (.hidden is skipped)
  REQUIRE(browser.entries().size() == 2);
  CHECK(browser.entries()[0].kind == FileEntry::Kind::up);
  CHECK(browser.entries()[1].label == "Games");

  CHECK_FALSE(browser.activate(1));
  REQUIRE(browser.entries().size() == 5);  // up, Inner, A_disc.ISO, b_disc.iso, packed.rvz
  CHECK(browser.entries()[1].kind == FileEntry::Kind::folder);
  CHECK(browser.entries()[2].label == "A_disc.ISO");
  CHECK(browser.entries()[3].label == "b_disc.iso");
  CHECK(browser.entries()[4].label == "packed.rvz");  // shown, so the user gets the specific message

  const std::optional<fs::path> chosen = browser.activate(3);
  REQUIRE(chosen);
  CHECK(chosen->filename() == "b_disc.iso");

  CHECK_FALSE(browser.activate(0));  // up
  CHECK(browser.folder() == root);
}

TEST_CASE("a folder that cannot be listed keeps the current one and says so") {
  const fs::path root = make_tree();
  FileBrowser browser([root] { return std::vector<fs::path>{root}; });
  browser.open(root / "Games");
  browser.open(root / "does-not-exist");
  CHECK(browser.folder() == root / "Games");
  CHECK_FALSE(browser.error().empty());
}

TEST_CASE("dump candidates by extension") {
  CHECK(FileBrowser::is_dump_candidate("x.iso"));
  CHECK(FileBrowser::is_dump_candidate("x.GCM"));
  CHECK(FileBrowser::is_dump_candidate("x.z64"));
  CHECK(FileBrowser::is_dump_candidate("x.wbfs"));
  CHECK_FALSE(FileBrowser::is_dump_candidate("x.txt"));
  CHECK_FALSE(FileBrowser::is_dump_candidate("iso"));
}
