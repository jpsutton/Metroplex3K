#include <fstream>
#include "core/gamedir.h"
#include "testutil.h"
using namespace m3k;

int main() {
  fs::path base = fs::temp_directory_path() / "m3k_gamedir_test";
  fs::remove_all(base);
  fs::create_directories(base / "GOG Games" / "SimCity 3000 Unlimited" / "APPS" / "sys");
  fs::create_directories(base / "GOG Games" / "SimCity 3000 Unlimited" / "APPS" / "Res");
  std::ofstream(base / "GOG Games" / "SimCity 3000 Unlimited" / "APPS" / "sys" / "sys.pak").put('x');
  fs::path root = base / "GOG Games" / "SimCity 3000 Unlimited";
  CHECK(findGameRoot(base) == root);            // parent folder found, case-insensitive
  CHECK(findGameRoot(root) == root);
  CHECK(findGameRoot(root / "APPS") == root);   // Apps dir itself
  CHECK(findGameRoot(base / "nonexistent").empty());
  CHECK(looksLikeGogInstaller("setup_simcity_3000_unlimited_2.0.0.5.exe"));
  CHECK(!looksLikeGogInstaller("readme.txt"));
  fs::remove_all(base);
  TEST_MAIN_END();
}
