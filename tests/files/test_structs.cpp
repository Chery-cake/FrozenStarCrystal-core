#include <cassert>
#include <cstdio>

import std.compat;
import files;

using namespace files;

namespace {

void test_span_sv_blob() {
  std::string s = "hello world";
  auto sp = as_span(s);
  assert(sp.size() == s.size());
  assert(as_sv(sp) == s);

  auto b = to_blob(std::string_view{"abc"});
  assert(b.size() == 3);
  assert(as_sv(b) == "abc");

  auto b2 = to_blob(as_span("xyz"));
  assert(b2.size() == 3);
  assert(as_sv(b2) == "xyz");

  // Empty span round-trips
  auto empty = as_span(std::string_view{});
  assert(empty.empty());
  assert(as_sv(empty).empty());
}

void test_error() {
  error e{std::make_error_code(std::errc::io_error), path{"/tmp/x"}, "op1"};
  assert(e.code == std::errc::io_error);
  assert(e.path == path{"/tmp/x"});
  assert(e.op == "op1");

  std::string w = e.what();
  assert(w.find("op1") != std::string::npos);
  assert(w.find("/tmp/x") != std::string::npos);

  error e2{"custom msg", path{"/tmp/y"}, "op2"};
  std::string w2 = e2.what();
  assert(w2.find("custom msg") != std::string::npos);
  assert(w2.find("op2") != std::string::npos);
}

// Concepts: compile-time checks.
struct ConstEnc {
  blob encode(std::span<const std::byte>) const { return {}; }
};
struct NonConstEnc {
  blob encode(std::span<const std::byte>) { return {}; }
};
struct ConstDec {
  blob decode(std::span<const std::byte>) const { return {}; }
};

static_assert(Encoder<ConstEnc>);
static_assert(StreamEncoder<ConstEnc>);
static_assert(!Encoder<NonConstEnc>); // needs const
static_assert(StreamEncoder<NonConstEnc>);
static_assert(Decoder<ConstDec>);
static_assert(Codec<ConstEnc> == false); // ConstEnc has no decode()
static_assert(NamedCodec<ConstEnc> == false);

} // namespace

int main() {
  test_span_sv_blob();
  test_error();
  std::puts("test_structs: ok");
  return 0;
}
