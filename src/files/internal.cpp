module;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

module files;

import std.compat;
import :internal;

namespace files {

// ── handle helpers ─────────────────────────────────────────────────────
void close_handle(file_handle h) noexcept {
#ifdef _WIN32
  CloseHandle(h);
#else
  ::close(h);
#endif
}

void discard(const path &tmp) noexcept {
#ifdef _WIN32
  DeleteFileW(tmp.c_str());
#else
  ::unlink(tmp.c_str());
#endif
}

// ── error helpers ──────────────────────────────────────────────────────
error chunk_err(const path &p, std::string_view op) {
  return error{"chunk size must be > 0", p, op};
}

std::error_code errno_code() noexcept {
  return {errno, std::generic_category()};
}

#ifdef _WIN32
std::error_code last_error_code() noexcept {
  return {static_cast<int>(GetLastError()), std::system_category()};
}
error win_err(const path &p, std::string_view op) {
  return error{last_error_code(), p, op};
}
#endif

error posix_err(const path &p, std::string_view op) {
  return error{errno_code(), p, op};
}

error err(const path &p, std::string_view op) {
#ifdef _WIN32
  return win_err(p, op);
#else
  return posix_err(p, op);
#endif
}

// ── open / names ───────────────────────────────────────────────────────
std::expected<file_handle, error> open_for_read(const path &p) {
#ifdef _WIN32
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{err(p, "open_for_read")};
  return h;
#else
  int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return std::unexpected{err(p, "open_for_read")};
  }
  return fd;
#endif
}

std::expected<file_handle, error> open_for_write(const path &p) {
#ifdef _WIN32
  HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{err(p, "open_for_write")};
  return h;
#else
  int fd = ::open(p.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return std::unexpected{err(p, "open_for_write")};
  }
  return fd;
#endif
}

std::uint64_t unique_suffix() {
  static const std::uint64_t seed = [] {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) | rd();
  }();
  static std::atomic<std::uint64_t> counter{0};
  return seed ^ counter.fetch_add(1, std::memory_order_relaxed);
}

void sync_dir(const path &dir) {
#ifndef _WIN32
  if (dir.empty()) {
    return;
  }
  int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    return;
  }
  ::fsync(fd);
  ::close(fd);
#else
  (void)dir; // MOVEFILE_WRITE_THROUGH already commits rename metadata
#endif
}

// ── full-buffer loaders ────────────────────────────────────────────────
std::expected<blob, error> load_buffered(const path &p) {
  blob out;
  auto r = load_into(p, out);
  if (!r) {
    return std::unexpected(std::move(r.error()));
  }
  return out;
}

// TODO
// optimize for larger files
std::expected<blob, error> load_mapped(const path &p) {
#ifdef _WIN32
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{err(p, "load_mapped:open")};

  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz)) {
    auto e = err(p, "load_mapped:size");
    CloseHandle(h);
    return std::unexpected{e};
  }
  if (sz.QuadPart == 0) {
    CloseHandle(h);
    return blob{};
  }

  HANDLE map = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (map == nullptr) {
    auto e = err(p, "load_mapped:map");
    CloseHandle(h);
    return std::unexpected{e};
  }

  const void *view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
  if (view == nullptr) {
    auto e = err(p, "load_mapped:view");
    CloseHandle(map);
    CloseHandle(h);
    return std::unexpected{e};
  }

  blob out(static_cast<std::size_t>(sz.QuadPart));
  std::memcpy(out.data(), view, out.size());
  UnmapViewOfFile(view);
  CloseHandle(map);
  CloseHandle(h);
  return out;
#else
  int fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0) {
    return std::unexpected{err(p, "load_mapped:open")};
  }

  struct stat st{};
  if (::fstat(fd, &st) < 0) {
    ::close(fd);
    return std::unexpected{err(p, "load_mapped:fstat")};
  }

  auto sz = static_cast<std::size_t>(st.st_size);
  if (sz == 0) {
    ::close(fd);
    return blob{};
  }

  void *m = ::mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
  if (m == MAP_FAILED) {
    ::close(fd);
    return std::unexpected{err(p, "load_mapped:mmap")};
  }

  blob out(sz);
  std::memcpy(out.data(), m, sz);
  ::munmap(m, sz);
  ::close(fd);
  return out;
#endif
}

// ── temp-file lifecycle ────────────────────────────────────────────────
std::expected<file_handle, error> open_unique_tmp(const path &target,
                                                  path &out_path) {
#ifdef _WIN32
  for (int attempt = 0; attempt < 16; ++attempt) {
    out_path = target;
    out_path += ".tmp." + std::to_string(unique_suffix());
    HANDLE h = CreateFileW(out_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
      return h;
    DWORD e = GetLastError();
    if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) {
      SetLastError(e);
      return std::unexpected{win_err(out_path, "open_unique_tmp")};
    }
  }
#else
  for (int attempt = 0; attempt < 16; ++attempt) {
    out_path = target;
    out_path += ".tmp." + std::to_string(unique_suffix());
    int fd =
        ::open(out_path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd >= 0)
      return fd;
    if (errno != EEXIST) {
      return std::unexpected{posix_err(out_path, "open_unique_tmp")};
    }
  }
#endif
  return std::unexpected{
      error{"too many temp-name collisions", target, "open_unique_tmp"}};
}

void close_and_discard(file_handle h, const path &tmp) noexcept {
  if (h != invalid_handle) {
    close_handle(h);
  }
  discard(tmp);
}

// ── durability ─────────────────────────────────────────────────────────
std::expected<void, error> flush_file(file_handle h, const path &p) {
#ifdef _WIN32
  if (!FlushFileBuffers(h))
    return std::unexpected{win_err(p, "flush_file")};
  return {};
#else
  if (::fsync(h) < 0)
    return std::unexpected{posix_err(p, "flush_file")};
  return {};
#endif
}

std::expected<void, error> rename_atomic(const path &from, const path &to) {
#ifdef _WIN32
  if (!MoveFileExW(from.c_str(), to.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return std::unexpected{win_err(to, "rename_atomic")};
  return {};
#else
  if (::rename(from.c_str(), to.c_str()) < 0)
    return std::unexpected{posix_err(to, "rename_atomic")};
  return {};
#endif
}

} // namespace files
