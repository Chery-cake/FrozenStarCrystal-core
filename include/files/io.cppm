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

export module files:io;

import std.compat;
import :structs;
import :internal;

export namespace files {

// ── Read ───────────────────────────────────────────────────────────────
template <IOPolicy Policy = IOPolicy::Auto>
std::expected<blob, error> load(const path &p);
std::expected<void, error> load_into(const path &, blob &);

template <ChunkConsumer F>
std::expected<void, error> for_each_chunk(const path &, F &&,
                                          std::size_t = base_chunk_size);

std::expected<std::size_t, error>
pread(file_handle, std::uintmax_t, std::span<std::byte>, const path &p = {});
std::expected<std::size_t, error> pread(const path &, std::uintmax_t,
                                        std::span<std::byte>);

// ── Write ──────────────────────────────────────────────────────────────
template <Sync Sync = Sync::File_and_dir, IOPolicy Policy = IOPolicy::Auto>
std::expected<void, error> save_atomic(const path &,
                                       std::span<const std::byte>);
template <Sync Sync = Sync::File_and_dir, IOPolicy Policy = IOPolicy::Auto>
std::expected<void, error> save_exclusive(const path &,
                                          std::span<const std::byte>);
std::expected<void, error> append(const path &, std::span<const std::byte>);

template <Sync Sync, typename Body>
std::expected<void, error> atomic_replace(const path &, Body &&);

template <StreamEncoder E, Sync Sync = Sync::File_and_dir>
std::expected<void, error> save_streamed_atomic(const path &, E &,
                                                std::span<const std::byte>,
                                                size_t chunk = base_chunk_size);

template <StreamEncoder E, Sync Sync = Sync::None>
std::expected<void, error> write_streamed(const path &p, E &enc,
                                          std::span<const std::byte> data,
                                          std::size_t chunk = base_chunk_size) {
  return save_streamed_atomic<E, Sync>(p, enc, data, chunk);
}

std::expected<void, error>
write_all(file_handle h, std::span<const std::byte> data, const path &p);

std::expected<std::size_t, error> pwrite(file_handle h, std::uintmax_t offset,
                                         std::span<const std::byte> data,
                                         const path &p = {});
std::expected<std::size_t, error> pwrite(const path &, std::uintmax_t,
                                         std::span<const std::byte>);

// ── Streamed read ──────────────────────────────────────────────────────
template <StreamDecoder D>
std::expected<blob, error> load_streamed(const path &, D &,
                                         size_t = base_chunk_size);

template <StreamDecoder D, ChunkConsumer F>
std::expected<void, error> load_streamed(const path &, D &, F &&,
                                         size_t = base_chunk_size);

// ── Codec convenience ──────────────────────────────────────────────────
template <Encoder E>
std::expected<void, error> save(const path &, std::span<const std::byte>,
                                const E &);

template <Decoder D> std::expected<blob, error> load(const path &, const D &);
template <Decoder D1, Decoder... Ds>
std::expected<blob, error> load(const path &, D1, Ds...);

// ═══════════════════════════════════════════════════════════════════════
// Template implementations
// ═══════════════════════════════════════════════════════════════════════

template <IOPolicy Policy> std::expected<blob, error> load(const path &p) {
  if constexpr (Policy == IOPolicy::Buffered) {
    return load_buffered(p);
  } else if constexpr (Policy == IOPolicy::Mapped) {
    return load_mapped(p);
  }

  std::error_code ec;
  auto sz = std::filesystem::file_size(p, ec);
  if (ec) {
    return std::unexpected(error{ec, p, "load"});
  }
  return sz >= std::size_t{1} << 20 ? load_mapped(p) : load_buffered(p);
}

template <ChunkConsumer F>
std::expected<void, error> for_each_chunk(const path &p, F &&f,
                                          std::size_t chunk) {
  if (!valid_chunk(chunk)) {
    return std::unexpected{chunk_err(p, "for_each_chunk")};
  }

  auto h = open_for_read(p);
  if (!h) {
    return std::unexpected{std::move(h.error())};
  }
  handle_guard g{*h};

  blob buf(chunk);
  std::uintmax_t off = 0;
  while (true) {
    auto n = pread(*h, off, std::span<std::byte>(buf.data(), chunk), p);
    if (!n) {
      return std::unexpected{std::move(n.error())};
    }
    if (*n == 0) {
      break;
    }
    f(std::span<const std::byte>(buf.data(), *n));
    off += *n;
  }
  return {};
}

template <Sync Sync, IOPolicy Policy>
std::expected<void, error> save_atomic(const path &target,
                                       std::span<const std::byte> data) {
  return atomic_replace<Sync>(
      target,
      [&](file_handle h, const path &tmp) -> std::expected<void, error> {
        const bool use_mmap =
            (Policy == IOPolicy::Mapped) ||
            (Policy == IOPolicy::Auto && data.size() >= (std::size_t{1} << 20));

        if (use_mmap && !data.empty()) {
#ifdef _WIN32
          LARGE_INTEGER sz{};
          sz.QuadPart = static_cast<LONGLONG>(data.size());
          if (!SetFilePointerEx(h, sz, nullptr, FILE_BEGIN) || !SetEndOfFile(h))
            return std::unexpected{win_err(tmp, "save_atomic:truncate")};
          HANDLE map =
              CreateFileMappingW(h, nullptr, PAGE_READWRITE, 0, 0, nullptr);
          if (!map)
            return std::unexpected{win_err(tmp, "save_atomic:map")};
          void *view = MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, data.size());
          if (!view) {
            auto e = win_err(tmp, "save_atomic:view");
            CloseHandle(map);
            return std::unexpected{e};
          }
          std::memcpy(view, data.data(), data.size());
          FlushViewOfFile(view, data.size());
          UnmapViewOfFile(view);
          CloseHandle(map);
#else
          if (::ftruncate(h, static_cast<off_t>(data.size())) < 0)
            return std::unexpected{posix_err(tmp, "save_atomic:ftruncate")};
          void *m = ::mmap(nullptr, data.size(), PROT_WRITE, MAP_SHARED, h, 0);
          if (m == MAP_FAILED)
            return std::unexpected{posix_err(tmp, "save_atomic:mmap")};
          std::memcpy(m, data.data(), data.size());
          ::msync(m, data.size(), MS_SYNC);
          ::munmap(m, data.size());
#endif
          return {};
        }
        return write_all(h, data, tmp);
      });
}

template <Sync Sync, IOPolicy Policy>
std::expected<void, error> save_exclusive(const path &p,
                                          std::span<const std::byte> data) {
#ifdef _WIN32
  HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return std::unexpected{err(p, "save_exclusive:create")};
  std::size_t off = 0;
  while (off < data.size()) {
    DWORD chunk =
        static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 1u << 30));
    DWORD written = 0;
    if (!WriteFile(h, data.data() + off, chunk, &written, nullptr) ||
        written != chunk) {
      CloseHandle(h);
      DeleteFileW(p.c_str());
      return std::unexpected{err(p, "save_exclusive:write")};
    }
    off += written;
  }
  if constexpr (Sync != Sync::None)
    FlushFileBuffers(h);
  CloseHandle(h);
  return {};
#else
  int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) {
    return std::unexpected{err(p, "save_exclusive:create")};
  }
  std::size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ::close(fd);
      ::unlink(p.c_str());
      return std::unexpected{err(p, "save_exclusive:write")};
    }
    off += static_cast<std::size_t>(n);
  }
  if constexpr (Sync != Sync::None) {
    ::fsync(fd);
  }
  ::close(fd);
  if constexpr (Sync == Sync::File_and_dir) {
    sync_dir(p.parent_path());
  }
  return {};
#endif
}

template <Sync Sync, typename Body>
std::expected<void, error> atomic_replace(const path &target, Body &&body) {
  path tmp;
  auto h = open_unique_tmp(target, tmp);
  if (!h) {
    return std::unexpected{std::move(h.error())};
  }

  struct guard {
    file_handle h;
    path tmp;
    bool committed = false;
    ~guard() {
      if (committed) {
        return;
      }
      if (h != invalid_handle) {
        close_handle(h);
      }
      discard(tmp);
    }
  } g{*h, tmp};

  auto r = body(g.h, g.tmp);
  if (!r) {
    return std::unexpected{std::move(r.error())};
  }

  if constexpr (Sync != Sync::None) {
    if (auto s = flush_file(g.h, g.tmp); !s) {
      return std::unexpected{std::move(s.error())};
    }
  }

  close_handle(g.h);
  g.h = invalid_handle;

  if (auto rr = rename_atomic(g.tmp, target); !rr) {
    return rr;
  }

  g.committed = true;
  if constexpr (Sync == Sync::File_and_dir) {
    sync_dir(target.parent_path());
  }
  return {};
}

template <StreamEncoder E, Sync Sync>
std::expected<void, error> save_streamed_atomic(const path &target, E &enc,
                                                std::span<const std::byte> data,
                                                std::size_t chunk) {
  if (!valid_chunk(chunk))
    return std::unexpected{chunk_err(target, "save_streamed_atomic")};
  return atomic_replace<Sync>(target, [&](file_handle h, const path &tmp) {
    for (std::size_t off = 0; off < data.size(); off += chunk) {
      auto piece = data.subspan(off, std::min(chunk, data.size() - off));
      auto out = enc.encode(piece);
      if (auto w = write_all(h, out, tmp); !w)
        return w;
    }
    return std::expected<void, error>{};
  });
}

template <StreamDecoder D>
std::expected<blob, error> load_streamed(const path &p, D &dec,
                                         std::size_t chunk) {
  blob out;
  auto r = for_each_chunk(
      p,
      [&](std::span<const std::byte> c) {
        auto part = dec.decode(c);
        out.insert(out.end(), part.begin(), part.end());
      },
      chunk);
  if (!r) {
    return std::unexpected{std::move(r.error())};
  }
  return out;
}

template <StreamDecoder D, ChunkConsumer F>
std::expected<void, error> load_streamed(const path &p, D &dec, F &&f,
                                         std::size_t chunk) {
  return for_each_chunk(
      p,
      [&](std::span<const std::byte> c) {
        auto part = dec.decode(c);
        f(std::span<const std::byte>(part));
      },
      chunk);
}

template <Encoder E>
std::expected<void, error> save(const path &p, std::span<const std::byte> data,
                                const E &enc) {
  auto encoded = enc.encode(data);
  return save_atomic(p, std::span<const std::byte>(encoded));
}

template <Decoder D>
std::expected<blob, error> load(const path &p, const D &d) {
  auto raw = load(p);
  if (!raw) {
    return std::unexpected{std::move(raw.error())};
  }
  auto decoded = d.decode(std::span<const std::byte>(*raw));
  return std::expected<blob, error>{std::move(decoded)};
}

template <Decoder D1, Decoder... Ds>
std::expected<blob, error> load(const path &p, D1 d1, Ds... ds) {
  auto raw = load(p);
  if (!raw) {
    return std::unexpected(std::move(raw.error()));
  }

  *raw = d1.decode(std::span<const std::byte>(*raw));
  ((*raw = ds.decode(std::span<const std::byte>(*raw))), ...);

  return std::move(*raw);
}

} // namespace files
