#include <cassert>
#include <cstdio>

import std.compat;
import files;
import test_helper;

using namespace files;
using test_helper::blob_eq;
using test_helper::make_pattern;
using test_helper::temp_dir;

namespace {

void test_file_stamp_read() {
  temp_dir td;
  auto p = td.file("stamp.bin");
  assert(save_atomic(p, make_pattern(100, 0x00)).has_value());

  auto s = file_stamp::read(p);
  assert(s.has_value());
  assert(s->valid);
  assert(s->size == 100);

  assert(!file_stamp::read(td.file("nope.bin")).has_value());

  // Zero-length file
  auto pe = td.file("empty.bin");
  assert(save_atomic(pe, blob{}).has_value());
  auto se = file_stamp::read(pe);
  assert(se.has_value() && se->size == 0);
}

void test_changed_since() {
  temp_dir td;
  auto p = td.file("changed.bin");
  assert(save_atomic(p, make_pattern(50, 0x00)).has_value());

  auto baseline = file_stamp::read(p);
  assert(baseline.has_value());

  // Untouched → not changed
  assert(!changed_since(p, *baseline));

  // Different size is detectable without depending on mtime resolution
  assert(save_atomic(p, make_pattern(60, 0x01)).has_value());
  assert(changed_since(p, *baseline));

  // Deleted / missing file → "changed"
  assert(changed_since(td.file("gone.bin"), *baseline));
}

void test_reloadable_file() {
  temp_dir td;
  auto p = td.file("reloadable.bin");

  // Initial state
  assert(save_atomic(p, make_pattern(64, 0xAA)).has_value());

  auto cache = reloadable_file<blob>::with_default_loader(p);
  assert(!cache.loaded());
  assert(cache.get() == nullptr);
  assert(cache.file() == p);

  // First refresh loads
  auto r1 = cache.refresh();
  assert(r1.has_value() && *r1 == true);
  assert(cache.loaded());
  assert(cache.get() != nullptr);
  assert(blob_eq(cache.value(), make_pattern(64, 0xAA)));

  // Second refresh is a no-op
  auto r2 = cache.refresh();
  assert(r2.has_value() && *r2 == false);

  // Change on disk (different size → always detected)
  auto v2 = make_pattern(128, 0xBB);
  assert(save_atomic(p, v2).has_value());
  auto r3 = cache.refresh();
  assert(r3.has_value() && *r3 == true);
  assert(blob_eq(cache.value(), v2));
  assert(cache.stamp().size == 128);

  // reload() is unconditional
  auto v3 = make_pattern(32, 0xCC);
  assert(save_atomic(p, v3).has_value());
  assert(cache.reload().has_value());
  assert(blob_eq(cache.value(), v3));

  // Custom loader is invoked exactly when needed
  auto p2 = td.file("reloadable_custom.bin");
  assert(save_atomic(p2, to_blob(std::string_view{"a"})).has_value());
  std::size_t calls = 0;
  reloadable_file<blob> c{p2, [&](const path &pp) {
                            ++calls;
                            return load(pp);
                          }};
  assert(c.refresh().has_value());
  assert(calls == 1);
  assert(c.refresh().has_value());
  assert(calls == 1); // cache hit
  assert(c.reload().has_value());
  assert(calls == 2); // forced reload
}

} // namespace

int main() {
  test_file_stamp_read();
  test_changed_since();
  test_reloadable_file();
  std::puts("test_watch: ok");
  return 0;
}
