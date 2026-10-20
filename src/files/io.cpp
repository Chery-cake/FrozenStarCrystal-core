module;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

module files;

import std.compat;
import :internal;

namespace files {

// ── handle_guard out-of-line dtor ──────────────────────────────────────
handle_guard::~handle_guard() {
  if (h != invalid_handle) {
    close_handle(h);
  }
}

// ── load_into ──────────────────────────────────────────────────────────
std::expected<void, error> load_into(const path &p, blob &out) {
  auto h = open_for_read(p);
  if (!h) {
    return std::unexpected{std::move(h.error())};
  }
  handle_guard g{*h};

  std::size_t sz = 0;
#ifdef _WIN32
  LARGE_INTEGER li{};
  if (!GetFileSizeEx(*h, &li)) {
    return std::unexpected{win_err(p, "load_into:size")};
  }
  sz = static_cast<std::size_t>(li.QuadPart);
#else
  struct stat st{};
  if (::fstat(*h, &st) < 0) {
    return std::unexpected{posix_err(p, "load_into:fstat")};
  }
  sz = static_cast<std::size_t>(st.st_size);
#endif

  out.resize(sz);
  if (sz == 0) {
    return {};
  }

  std::size_t off = 0;
  while (off < sz) {
    auto n =
        pread(*h, off, std::span<std::byte>(out.data() + off, sz - off), p);
    if (!n) {
      return std::unexpected{std::move(n.error())};
    }
    if (*n == 0) {
      return std::unexpected(error{"short read", p, "load_into:pread"});
    }
    off += *n;
  }
  return {};
}

// ── pread (already-open handle) ────────────────────────────────────────
std::expected<std::size_t, error> pread(file_handle h, std::uintmax_t offset,
                                        std::span<std::byte> buf,
                                        const path &p) {
#ifdef _WIN32
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{error{"invalid handle", p, "pread"}};

  OVERLAPPED ov{};
  ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
  ov.OffsetHigh = static_cast<DWORD>(offset >> 32);

  DWORD want = static_cast<DWORD>(std::min<std::size_t>(buf.size(), MAXDWORD));
  DWORD got = 0;
  if (!ReadFile(h, buf.data(), want, &got, &ov))
    return std::unexpected{win_err(p, "pread:ReadFile")};
  return static_cast<std::size_t>(got);
#else
  if (h < 0) {
    return std::unexpected{error{"invalid fd", p, "pread"}};
  }

  ssize_t n;
  do {
    n = ::pread(h, buf.data(), buf.size(), static_cast<off_t>(offset));
  } while (n < 0 && errno == EINTR);

  if (n < 0) {
    return std::unexpected{posix_err(p, "pread")};
  }
  return static_cast<std::size_t>(n);
#endif
}

// ── pread (convenience overload) ───────────────────────────────────────
std::expected<std::size_t, error> pread(const path &p, std::uintmax_t offset,
                                        std::span<std::byte> buf) {
  auto h = open_for_read(p);
  if (!h) {
    return std::unexpected{std::move(h.error())};
  }
  handle_guard g{*h};
  return pread(*h, offset, buf, p);
}

// ── append (platform-specific but not a template) ──────────────────────
std::expected<void, error> append(const path &p,
                                  std::span<const std::byte> data) {
#ifdef _WIN32
  HANDLE h =
      CreateFileW(p.c_str(),
                  GENERIC_READ | GENERIC_WRITE, // REQUIRED for LockFileEx
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{err(p, "append:open")};

  OVERLAPPED ov{};
  if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &ov)) {
    CloseHandle(h);
    return std::unexpected{err(p, "append:lock")};
  }

  LARGE_INTEGER zero{};
  zero.QuadPart = 0;
  SetFilePointerEx(h, zero, nullptr, FILE_END); // seek AFTER lock

  size_t off = 0;
  while (off < data.size()) {
    DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(data.size() - off, 1u << 30));
    DWORD written = 0;
    if (!WriteFile(h, data.data() + off, chunk, &written, nullptr) ||
        written != chunk) {
      UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, nullptr);
      CloseHandle(h);
      return std::unexpected{err(p, "append:write")};
    }
    off += written;
  }
  UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, nullptr);
  CloseHandle(h);
  return {};
#else
  int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    return std::unexpected{err(p, "append:open")};
  }

  if (::flock(fd, LOCK_EX) < 0) {
    auto e = err(p, "append:flock");
    ::close(fd);
    return std::unexpected{e};
  }

  std::expected<void, error> result{};
  std::size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      result = std::unexpected{err(p, "append:write")};
      break;
    }
    off += static_cast<std::size_t>(n);
  }

  ::flock(fd, LOCK_UN);
  ::close(fd);
  return result;
#endif
}

// ── write all (write content of the file) ───────────────────────────────────
std::expected<void, error>
write_all(file_handle h, std::span<const std::byte> data, const path &p) {
  std::size_t off = 0;
  while (off < data.size()) {
#ifdef _WIN32
    DWORD chunk =
        static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 1u << 30));
    DWORD written = 0;
    if (!WriteFile(h, data.data() + off, chunk, &written, nullptr))
      return std::unexpected{win_err(p, "write_all")};
    // Regular files: either all or nothing. Keep the check anyway for
    // safety.
    if (written != chunk)
      return std::unexpected{error{"short write", p, "write_all"}};
    off += written;
#else
    ssize_t n = ::write(h, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return std::unexpected{posix_err(p, "write_all")};
    }
    off += static_cast<std::size_t>(n);
#endif
  }
  return {};
}

// ── pwrite (already-open handle, single call) ─────────────────────────
std::expected<std::size_t, error> pwrite(file_handle h, std::uintmax_t offset,
                                         std::span<const std::byte> data,
                                         const path &p) {
#ifdef _WIN32
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{error{"invalid handle", p, "pwrite"}};

  OVERLAPPED ov{};
  ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
  ov.OffsetHigh = static_cast<DWORD>(offset >> 32);

  DWORD want = static_cast<DWORD>(std::min<std::size_t>(data.size(), MAXDWORD));
  DWORD wrote = 0;
  if (!WriteFile(h, data.data(), want, &wrote, &ov))
    return std::unexpected{win_err(p, "pwrite:WriteFile")};
  return static_cast<std::size_t>(wrote);
#else
  if (h < 0)
    return std::unexpected{error{"invalid fd", p, "pwrite"}};

  ssize_t n;
  do {
    n = ::pwrite(h, data.data(), data.size(), static_cast<off_t>(offset));
  } while (n < 0 && errno == EINTR);

  if (n < 0)
    return std::unexpected{posix_err(p, "pwrite")};
  return static_cast<std::size_t>(n);
#endif
}

std::expected<std::size_t, error> pwrite(const path &p, std::uintmax_t offset,
                                         std::span<const std::byte> data) {
  auto h = open_for_write(p); // needs a helper, see below
  if (!h)
    return std::unexpected{std::move(h.error())};
  handle_guard g{*h};
  return pwrite(*h, offset, data, p);
}

} // namespace files
