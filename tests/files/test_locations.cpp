#include <cassert>
#include <cstdio>
#include <string>

import std.compat;
import files;
import test_helper;

using namespace files;
using test_helper::temp_dir;

namespace {

// Use temp_dir's own leaf name as the "app", so default_dir<L>(app)
// resolves to something we own and auto-clean (for Temp/Executable we
// only verify shape; we don't create anything for Data).
void test_default_dir_compile_and_runtime() {
  temp_dir td;
  auto app = td.dir.lexically_relative(*default_dir<Location::Temp>()).string();

  // Compile-time
  {
    auto r = default_dir<Location::Temp>(app);
    assert(r.has_value());
    assert(*r == td.dir); // <sys_temp>/<app>
  }
  {
    auto r = default_dir<Location::Temp>();
    assert(r.has_value());
    assert(!r->empty());
  }
  {
    auto r = default_dir<Location::Executable>();
    assert(r.has_value());
    assert(std::filesystem::exists(*r));
    assert(std::filesystem::is_directory(*r));
  }
  // Data may be unavailable on some runners; treat as conditional.
  {
    auto r = default_dir<Location::Data>(app);
    if (r.has_value()) {
      assert(r->filename() == path{app}.filename());
    }
  }

  // Runtime dispatch
  for (auto loc : {Location::Temp, Location::Executable}) {
    auto r = default_dir(loc);
    assert(r.has_value());
    assert(!r->empty());
  }
  {
    auto r = default_dir(Location::Temp, app);
    assert(r.has_value());
    assert(*r == td.dir);
  }
}

void test_ensure_dir() {
  temp_dir td;
  auto app = td.dir.lexically_relative(*default_dir<Location::Temp>()).string();

  // Temp variant is idempotent (already created by temp_dir)
  {
    auto r = ensure_dir<Location::Temp>(app);
    assert(r.has_value());
    assert(*r == td.dir);
    assert(std::filesystem::is_directory(*r));
  }
  // Runtime variant, same
  {
    auto r = ensure_dir(Location::Temp, app);
    assert(r.has_value());
    assert(*r == td.dir);
  }
  // Data variant: only if resolvable, clean up explicitly
  if (auto d = default_dir<Location::Data>("fsc_ensure_probe")) {
    auto r = ensure_dir<Location::Data>("fsc_ensure_probe");
    if (r.has_value()) {
      assert(std::filesystem::is_directory(*r));
      std::error_code ec;
      std::filesystem::remove_all(*r, ec);
    }
  }
}

void test_default_path() {
  temp_dir td;
  auto app = td.dir.lexically_relative(*default_dir<Location::Temp>()).string();

  auto r = default_path<Location::Temp>(path{"file.bin"}, app);
  assert(r.has_value());
  assert(*r == td.dir / "file.bin");

  auto r2 = default_path(Location::Temp, path{"other.bin"}, app);
  assert(r2.has_value());
  assert(*r2 == td.dir / "other.bin");

  // No app
  auto r3 = default_path<Location::Temp>(path{"bare.bin"});
  assert(r3.has_value());
  assert(r3->filename() == path{"bare.bin"});
}

void test_find() {
  temp_dir td;
  auto app = td.dir.lexically_relative(*default_dir<Location::Temp>()).string();

  // Create a real file at <sys_temp>/<app>/findme.txt
  auto target = td.file("findme.txt");
  assert(save_atomic(target, to_blob(std::string_view{"hello"})).has_value());

  // Templated search hits Temp first
  {
    auto r =
        find<Location::Temp, Location::Executable>(path{"findme.txt"}, app);
    assert(r.has_value());
    assert(*r == target);
  }
  // Not found in Temp-only
  {
    auto r = find<Location::Temp>(path{"nope.txt"}, app);
    assert(!r.has_value());
  }
  // Runtime span
  {
    Location order[] = {Location::Temp, Location::Executable};
    auto r = find(std::span<const Location>(order), path{"findme.txt"}, app);
    assert(r.has_value() && *r == target);
  }
  // Runtime initializer_list
  {
    auto r = find({Location::Temp}, path{"findme.txt"}, app);
    assert(r.has_value() && *r == target);
  }
}

} // namespace

int main() {
  test_default_dir_compile_and_runtime();
  test_ensure_dir();
  test_default_path();
  test_find();
  std::puts("test_locations: ok");
  return 0;
}
