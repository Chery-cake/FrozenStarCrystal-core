module;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

export module test_helper;

import std.compat;
import files;

export namespace test_helper {

// ── RAII temp directory (unique per process, wiped on destruction) ─────
struct temp_dir {
  files::path dir;

  temp_dir() {
    auto base = files::default_dir<files::Location::Temp>("fsc_test");
    if (!base)
      throw std::runtime_error("test_helper: cannot resolve temp dir");
    static std::atomic<unsigned> counter{0};
    auto stamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
#ifdef _WIN32
    auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    auto pid = static_cast<unsigned long>(::getpid());
#endif
    auto uniq = std::to_string(pid) + "_" +
                std::to_string(counter.fetch_add(1)) + "_" +
                std::to_string(stamp & 0xFFFFFF);
    dir = *base / files::path{uniq};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
      throw std::runtime_error("test_helper: mkdir: " + ec.message());
  }
  ~temp_dir() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  temp_dir(const temp_dir &) = delete;
  temp_dir &operator=(const temp_dir &) = delete;

  [[nodiscard]] files::path file(std::string_view n) const {
    return dir / files::path{std::string(n)};
  }
};

// ── deterministic / random data generators ─────────────────────────────
inline files::blob make_pattern(std::size_t size, unsigned char seed = 0) {
  files::blob out(size);
  for (std::size_t i = 0; i < size; ++i)
    out[i] = static_cast<std::byte>((seed + i) & 0xFF);
  return out;
}

inline files::blob make_random(std::size_t size,
                               std::uint32_t seed = 0xC0FFEE) {
  files::blob out(size);
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto &b : out)
    b = static_cast<std::byte>(dist(rng));
  return out;
}

inline bool blob_eq(std::span<const std::byte> a,
                    std::span<const std::byte> b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

inline void poke(files::blob &b, std::size_t offset,
                 std::span<const std::byte> patch) {
  if (offset + patch.size() > b.size())
    throw std::out_of_range("poke");
  std::copy(patch.begin(), patch.end(),
            b.begin() + static_cast<std::ptrdiff_t>(offset));
}

// ── platform-only helpers (NOT inline: must not force headers on users) ─
files::file_handle open_read(const files::path &p) {
#ifdef _WIN32
  return CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
  return ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
#endif
}

void close_fh(files::file_handle h) {
#ifdef _WIN32
  CloseHandle(h);
#else
  ::close(h);
#endif
}

} // namespace test_helper
