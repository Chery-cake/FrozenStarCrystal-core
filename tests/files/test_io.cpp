#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

import std.compat;
import files;
import test_helper;

using namespace files;
using test_helper::blob_eq;
using test_helper::make_pattern;
using test_helper::make_random;
using test_helper::poke;
using test_helper::temp_dir;

namespace {

// ── codecs for streaming / convenience tests ───────────────────────────
struct XorCodec {
  std::byte key = std::byte{0x5A};
  blob encode(std::span<const std::byte> in) const {
    blob o(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
      o[i] = in[i] ^ key;
    return o;
  }
  blob decode(std::span<const std::byte> in) const {
    return encode(in); // XOR is symmetric
  }
};
struct IdentityCodec {
  blob encode(std::span<const std::byte> in) const {
    return blob(in.begin(), in.end());
  }
  blob decode(std::span<const std::byte> in) const {
    return blob(in.begin(), in.end());
  }
};

// ── load ───────────────────────────────────────────────────────────────
void test_load_all_policies(const path &p, const blob &expected) {
  auto check = [&](std::expected<blob, error> r) {
    assert(r.has_value());
    assert(blob_eq(*r, expected));
  };
  check(load<IOPolicy::Buffered>(p));
  check(load<IOPolicy::Mapped>(p));
  check(load<IOPolicy::Auto>(p));
  check(load(p));
}

void test_load() {
  temp_dir td;

  // Small / empty / 1-byte / large
  for (std::size_t sz :
       {std::size_t{0}, std::size_t{1}, std::size_t{256}, std::size_t{1024}}) {
    auto data = make_pattern(sz, 0x11);
    auto p = td.file("load_" + std::to_string(sz) + ".bin");
    assert(save_atomic(p, data).has_value());
    test_load_all_policies(p, data);
  }

  // Large (Auto should pick mapped)
  auto big = make_pattern(2 * 1024 * 1024, 0x77);
  auto pb = td.file("load_big.bin");
  assert(save_atomic(pb, big).has_value());
  test_load_all_policies(pb, big);

  // Missing
  assert(!load(td.file("nope.bin")).has_value());
}

// ── load_into ──────────────────────────────────────────────────────────
void test_load_into() {
  temp_dir td;
  auto data = make_pattern(4096, 0x42);
  auto p = td.file("load_into.bin");
  assert(save_atomic(p, data).has_value());

  blob out;
  assert(load_into(p, out).has_value());
  assert(blob_eq(out, data));

  // Clobbers pre-existing content
  out.assign({std::byte{0xFF}, std::byte{0xFF}});
  assert(load_into(p, out).has_value());
  assert(blob_eq(out, data));

  // Missing
  blob dummy;
  assert(!load_into(td.file("missing.bin"), dummy).has_value());
}

// ── for_each_chunk ─────────────────────────────────────────────────────
void test_for_each_chunk() {
  temp_dir td;
  auto data = make_pattern(10 * 1024, 0x55);
  auto p = td.file("chunk.bin");
  assert(save_atomic(p, data).has_value());

  auto run = [&](std::size_t chunk) -> std::pair<blob, std::size_t> {
    blob col;
    std::size_t n = 0;
    auto r = for_each_chunk(
        p,
        [&](std::span<const std::byte> c) {
          col.insert(col.end(), c.begin(), c.end());
          ++n;
        },
        chunk);
    assert(r.has_value());
    return {std::move(col), n};
  };

  for (std::size_t c :
       {std::size_t{1}, std::size_t{1024}, std::size_t{4096}, data.size()}) {
    auto [col, n] = run(c);
    assert(blob_eq(col, data));
    assert(n == (data.size() + c - 1) / c);
  }

  // Invalid chunk
  assert(!for_each_chunk(p, [](auto) {}, 0).has_value());

  // Empty file → callback never called
  auto pe = td.file("empty.bin");
  assert(save_atomic(pe, blob{}).has_value());
  std::size_t calls = 0;
  assert(for_each_chunk(pe, [&](auto) { ++calls; }).has_value());
  assert(calls == 0);
}

// ── pread ──────────────────────────────────────────────────────────────
void test_pread_path() {
  temp_dir td;
  auto data = make_pattern(1024, 0x00);
  auto p = td.file("pread.bin");
  assert(save_atomic(p, data).has_value());

  struct probe {
    std::size_t off, len, want;
  };
  for (probe pr : {probe{0, 16, 16},         // start
                   probe{500, 16, 16},       // middle
                   probe{1024 - 16, 16, 16}, // end
                   probe{127, 32, 32},       // random
                   probe{1024, 8, 0},        // exactly EOF
                   probe{1024 - 8, 16, 8},   // partial at end
                   probe{4096, 8, 0}}) {     // far past EOF
    std::vector<std::byte> buf(pr.len);
    auto r = pread(p, pr.off, std::span<std::byte>(buf));
    assert(r.has_value());
    assert(*r == pr.want);
    assert(std::equal(buf.begin(),
                      buf.begin() + static_cast<std::ptrdiff_t>(*r),
                      data.begin() + static_cast<std::ptrdiff_t>(pr.off)));
  }
  assert(!pread(td.file("nope.bin"), 0, std::span<std::byte>{}).has_value());
}

void test_pread_handle() {
  temp_dir td;
  auto data = make_pattern(512, 0xAA);
  auto p = td.file("pread_handle.bin");
  assert(save_atomic(p, data).has_value());

  auto h = test_helper::open_read(p);
  assert(h != invalid_handle);

  std::byte buf[32]{};
  auto r = pread(h, 100, std::span<std::byte>(buf, 32), p);
  assert(r.has_value() && *r == 32);
  assert(std::equal(buf, buf + 32, data.begin() + 100));

  // Past EOF
  auto r2 = pread(h, data.size() + 5, std::span<std::byte>(buf, 32), p);
  assert(r2.has_value() && *r2 == 0);

  test_helper::close_fh(h);

  // Invalid handle
  auto r3 = pread(invalid_handle, 0, std::span<std::byte>(buf, 32), p);
  assert(!r3.has_value());
}

// ── save_atomic ────────────────────────────────────────────────────────
void test_save_atomic() {
  temp_dir td;

  // Empty
  auto pe = td.file("save_empty.bin");
  assert(save_atomic(pe, blob{}).has_value());
  assert(std::filesystem::file_size(pe) == 0);

  // Small
  auto ps = td.file("save_small.bin");
  auto small = make_pattern(512, 0x33);
  assert(save_atomic(ps, small).has_value());
  assert(blob_eq(*load(ps), small));

  // Large (mmap path)
  auto pl = td.file("save_large.bin");
  auto large = make_random(2 * 1024 * 1024, 0x1234);
  assert(save_atomic(pl, large).has_value());
  assert(blob_eq(*load<IOPolicy::Buffered>(pl), large));

  // Overwrite
  auto po = td.file("save_overwrite.bin");
  assert(save_atomic(po, make_pattern(100, 0xAA)).has_value());
  assert(save_atomic(po, make_pattern(50, 0xBB)).has_value());
  assert(std::filesystem::file_size(po) == 50);

  // Sync variants
  assert(
      (save_atomic<Sync::None>(td.file("s_none.bin"), make_pattern(10, 0x01)))
          .has_value());
  assert(
      (save_atomic<Sync::File>(td.file("s_file.bin"), make_pattern(10, 0x02)))
          .has_value());
  assert((save_atomic<Sync::File_and_dir>(td.file("s_dir.bin"),
                                          make_pattern(10, 0x03)))
             .has_value());

  // Policy variants
  auto d = make_pattern(100, 0x11);
  assert((save_atomic<Sync::File_and_dir, IOPolicy::Buffered>(
              td.file("s_buf.bin"), d))
             .has_value());
  assert((save_atomic<Sync::File_and_dir, IOPolicy::Mapped>(
              td.file("s_map.bin"), d))
             .has_value());

  // Bad target dir
  assert(!save_atomic(td.file("no_such_dir/x.bin"), make_pattern(10, 0))
              .has_value());
}

// ── save_exclusive ─────────────────────────────────────────────────────
void test_save_exclusive() {
  temp_dir td;

  auto p = td.file("excl.bin");
  auto data = make_pattern(256, 0x77);
  assert(save_exclusive(p, data).has_value());
  assert(blob_eq(*load(p), data));

  // Second attempt must fail
  assert(!save_exclusive(p, data).has_value());

  // Sync variants
  assert(
      (save_exclusive<Sync::None>(td.file("excl_none.bin"), make_pattern(8, 1)))
          .has_value());
  assert(
      (save_exclusive<Sync::File>(td.file("excl_file.bin"), make_pattern(8, 2)))
          .has_value());
}

// ── append ─────────────────────────────────────────────────────────────
void test_append() {
  temp_dir td;
  auto p = td.file("append.bin");

  auto d1 = make_pattern(100, 0x00);
  auto d2 = make_pattern(50, 0x32);
  auto d3 = make_random(1024 * 1024, 0x99);

  assert(append(p, d1).has_value());
  assert(append(p, d2).has_value());
  assert(append(p, blob{}).has_value()); // empty append
  assert(append(p, d3).has_value());

  blob expected = d1;
  expected.insert(expected.end(), d2.begin(), d2.end());
  expected.insert(expected.end(), d3.begin(), d3.end());
  assert(blob_eq(*load<IOPolicy::Buffered>(p), expected));

  assert(std::filesystem::file_size(p) == expected.size());
}

// ── atomic_replace ─────────────────────────────────────────────────────
void test_atomic_replace() {
  temp_dir td;

  // Basic create
  auto p = td.file("replace.bin");
  auto data = make_pattern(200, 0x44);
  assert((atomic_replace<Sync::File_and_dir>(p, [&](file_handle h,
                                                    const path &tmp) {
           return write_all(h, data, tmp);
         })).has_value());
  assert(blob_eq(*load(p), data));

  // Body fails → original untouched
  auto p2 = td.file("replace_keep.bin");
  auto orig = make_pattern(100, 0x11);
  assert(save_atomic(p2, orig).has_value());
  assert(!(atomic_replace<Sync::None>(p2, [&](file_handle, const path &) {
            return std::expected<void, error>{
                std::unexpected{error{"boom", p2, "test"}}};
          })).has_value());
  assert(blob_eq(*load(p2), orig));

  // Rewrite existing file atomically
  auto p3 = td.file("replace_rewrite.bin");
  assert(save_atomic(p3, make_pattern(64, 0xAA)).has_value());
  auto newdata = make_pattern(128, 0xBB);
  assert((atomic_replace<Sync::File>(p3, [&](file_handle h, const path &tmp) {
           return write_all(h, newdata, tmp);
         })).has_value());
  assert(blob_eq(*load(p3), newdata));
}

// ── pwrite (path overload) ────────────────────────────────────────────
void test_pwrite_path() {
  temp_dir td;
  const std::size_t N = 1024;
  auto data = make_pattern(N, 0x00);
  auto p = td.file("pwrite_path.bin");
  assert(save_atomic(p, data).has_value());

  std::byte patch[8] = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                        std::byte{0xEF}, std::byte{0xCA}, std::byte{0xFE},
                        std::byte{0xBA}, std::byte{0xBE}};

  // Overwrite at start / middle / end / arbitrary offsets
  for (std::size_t off :
       {std::size_t{0}, N / 2, N - 8, std::size_t{127}, std::size_t{900}}) {
    auto r = pwrite(p, off, std::span<const std::byte>(patch, 8));
    assert(r.has_value() && *r == 8);

    std::byte got[8]{};
    auto rr = pread(p, off, std::span<std::byte>(got, 8));
    assert(rr.has_value() && *rr == 8);
    assert(std::equal(got, got + 8, patch));
  }

  // Missing file → fails (open_for_write has no O_CREAT)
  assert(!pwrite(td.file("nope.bin"), 0, std::span<const std::byte>(patch, 8))
              .has_value());

  // Extend file past EOF: bytes 16..19 stay a zero "hole",
  // bytes 20..23 get the patch.
  auto pe = td.file("pwrite_extend.bin");
  assert(save_atomic(pe, make_pattern(16, 0x00)).has_value());
  auto rex = pwrite(pe, 20, std::span<const std::byte>(patch, 4));
  assert(rex.has_value() && *rex == 4);
  assert(std::filesystem::file_size(pe) == 24);

  auto after = load(pe);
  assert(after.has_value());
  for (std::size_t i = 16; i < 20; ++i)
    assert((*after)[i] == std::byte{0x00});
  assert(std::equal(after->begin() + 20, after->begin() + 24, patch));
}

// ── pwrite (handle overload) ──────────────────────────────────────────
void test_pwrite_handle() {
  temp_dir td;
  const std::size_t N = 512;
  auto data = make_pattern(N, 0x00);
  auto p = td.file("pwrite_handle.bin");
  assert(save_atomic(p, data).has_value());

  std::byte patch[8] = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                        std::byte{0x44}, std::byte{0x55}, std::byte{0x66},
                        std::byte{0x77}, std::byte{0x88}};

  // We need a writeable handle. atomic_replace hands us one.
  bool ran = false;
  auto r = atomic_replace<Sync::None>(
      p, [&](file_handle h, const path &tmp) -> std::expected<void, error> {
        ran = true;
        if (auto w = write_all(h, data, tmp); !w)
          return w;
        // pwrite in the middle (absolute offset, independent of pos)
        auto pw = pwrite(h, 100, std::span<const std::byte>(patch, 8), tmp);
        if (!pw)
          return std::unexpected{pw.error()};
        if (*pw != 8)
          return std::unexpected{error{"short pwrite", tmp, "pwrite"}};
        // pwrite at the start
        auto pw2 = pwrite(h, 0, std::span<const std::byte>(patch, 4), tmp);
        if (!pw2)
          return std::unexpected{pw2.error()};
        if (*pw2 != 4)
          return std::unexpected{error{"short pwrite", tmp, "pwrite"}};
        return {};
      });
  assert(ran && r.has_value());

  auto got = load(p);
  assert(got.has_value());
  assert(std::equal(got->begin() + 100, got->begin() + 108, patch));
  assert(std::equal(got->begin() + 0, got->begin() + 4, patch));

  // Invalid handle → error
  std::byte buf[4]{};
  auto bad = pwrite(invalid_handle, 0, std::span<const std::byte>(buf, 4), p);
  assert(!bad.has_value());
}

// ── streamed encode / decode ───────────────────────────────────────────
void test_streamed_codecs() {
  temp_dir td;

  auto data = make_pattern(10 * 1024, 0x00);
  auto p = td.file("stream.bin");

  // save_streamed_atomic + load_streamed
  {
    XorCodec enc;
    assert((save_streamed_atomic<XorCodec, Sync::File>(p, enc, data, 1024))
               .has_value());

    // On-disk content must be XOR-transformed
    auto raw = load(p);
    assert(raw.has_value());
    for (std::size_t i = 0; i < data.size(); ++i)
      assert((*raw)[i] == (data[i] ^ std::byte{0x5A}));

    // Decode back
    XorCodec dec;
    auto round = load_streamed(p, dec, 1024);
    assert(round.has_value() && blob_eq(*round, data));
  }

  // write_streamed
  {
    auto p2 = td.file("write_streamed.bin");
    auto d2 = make_pattern(4096, 0x11);
    XorCodec enc;
    assert(
        (write_streamed<XorCodec, Sync::File>(p2, enc, d2, 512)).has_value());
    XorCodec dec;
    auto round = load_streamed(p2, dec, 512);
    assert(round.has_value() && blob_eq(*round, d2));
  }

  // load_streamed with consumer
  {
    auto p3 = td.file("stream_consumer.bin");
    auto d3 = make_pattern(8192, 0x22);
    XorCodec enc;
    assert((save_streamed_atomic<XorCodec, Sync::File>(p3, enc, d3, 1024))
               .has_value());

    XorCodec dec;
    blob col;
    auto r = load_streamed(
        p3, dec,
        [&](std::span<const std::byte> c) {
          col.insert(col.end(), c.begin(), c.end());
        },
        1024);
    assert(r.has_value() && blob_eq(col, d3));
  }

  // Invalid chunk
  XorCodec e;
  assert(
      (!save_streamed_atomic<XorCodec, Sync::None>(p, e, data, 0).has_value()));
}

// ── save<Encoder> / load<Decoder> / load<D1, Ds...> ────────────────────
void test_convenience_codecs() {
  temp_dir td;
  auto p = td.file("convenience.bin");
  auto data = make_pattern(256, 0x55);
  IdentityCodec c;

  assert(save(p, data, c).has_value());
  auto r1 = load(p, c);
  assert(r1.has_value() && blob_eq(*r1, data));

  auto r2 = load(p, c, c); // chained
  assert(r2.has_value() && blob_eq(*r2, data));
}

// ── modify at start / middle / end / random ────────────────────────────
void test_modify_positions() {
  temp_dir td;
  const std::size_t N = 1024;
  std::byte patch[4] = {std::byte{0xCA}, std::byte{0xFE}, std::byte{0xBA},
                        std::byte{0xBE}};

  for (std::size_t off : {std::size_t{0}, N / 2, N - 4, std::size_t{127},
                          std::size_t{500}, std::size_t{900}}) {
    auto p = td.file("mod_" + std::to_string(off) + ".bin");
    auto original = make_pattern(N, 0x00);
    assert(save_atomic(p, original).has_value());

    auto modified = original;
    poke(modified, off, patch);
    assert(save_atomic(p, modified).has_value());

    // Verify via pread the exact region
    std::byte got[4]{};
    auto r = pread(p, off, std::span<std::byte>(got, 4));
    assert(r.has_value() && *r == 4);
    assert(std::equal(got, got + 4, patch));

    // And the whole file
    assert(blob_eq(*load(p), modified));
  }
}

// Use atomic_replace's body escape-hatch to modify at arbitrary offsets
void test_modify_inplace_via_atomic_replace() {
  temp_dir td;
  auto p = td.file("replace_pwrite.bin");
  auto original = make_pattern(4096, 0x00);
  assert(save_atomic(p, original).has_value());

  std::byte patch[8] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};

  for (std::size_t off :
       {std::size_t{0}, std::size_t{2048}, std::size_t{4088}}) {
    auto current = load(p);
    assert(current.has_value());

    auto r = atomic_replace<Sync::None>(
        p, [&](file_handle h, const path &tmp) -> std::expected<void, error> {
          if (auto w = write_all(h, *current, tmp); !w)
            return w;
          auto pw = pwrite(h, off, std::span<const std::byte>(patch, 8), tmp);
          if (!pw)
            return std::unexpected{pw.error()};
          if (*pw != 8)
            return std::unexpected{error{"short pwrite", tmp, "pwrite"}};
          return {};
        });
    assert(r.has_value());

    auto got = load(p);
    assert(got.has_value());
    assert(std::equal(got->begin() + static_cast<std::ptrdiff_t>(off),
                      got->begin() + static_cast<std::ptrdiff_t>(off + 8),
                      patch));
  }
}

} // namespace

int main() {
  test_load();
  test_load_into();
  test_for_each_chunk();
  test_pread_path();
  test_pread_handle();
  test_save_atomic();
  test_save_exclusive();
  test_append();
  test_atomic_replace();
  test_pwrite_path();
  test_pwrite_handle();
  test_streamed_codecs();
  test_convenience_codecs();
  test_modify_positions();
  test_modify_inplace_via_atomic_replace();
  std::puts("test_io: ok");
  return 0;
}
