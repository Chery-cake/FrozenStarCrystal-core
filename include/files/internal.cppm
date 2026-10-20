module;

export module files:internal;

import std.compat;
import :structs;

export namespace files {

inline constexpr std::size_t base_chunk_size =
    static_cast<std::size_t>(64 * 1024);

// ── RAII guard (visible to users only because templates instantiate it) ─
struct handle_guard {
  file_handle h;
  explicit handle_guard(file_handle hh) noexcept : h{hh} {}
  ~handle_guard(); // defined in src/files/io.cpp
  handle_guard(const handle_guard &) = delete;
  handle_guard &operator=(const handle_guard &) = delete;
  [[nodiscard]] file_handle get() const noexcept { return h; }
};

// ── handle helpers ─────────────────────────────────────────────────────
void close_handle(file_handle h) noexcept;
void discard(const path &tmp) noexcept;

// ── small inline helpers ───────────────────────────────────────────────
[[nodiscard]] constexpr bool valid_chunk(std::size_t chunk) noexcept {
  return chunk > 0;
}
[[nodiscard]] error chunk_err(const path &p, std::string_view op);

// ── error helpers ──────────────────────────────────────────────────────
std::error_code errno_code() noexcept;
#ifdef _WIN32
std::error_code last_error_code() noexcept;
error win_err(const path &p, std::string_view op);
#endif
error posix_err(const path &p, std::string_view op);
error err(const path &p, std::string_view op);

// ── open / names ───────────────────────────────────────────────────────
std::expected<file_handle, error> open_for_read(const path &p);
std::expected<file_handle, error> open_for_write(const path &p);
std::uint64_t unique_suffix();
void sync_dir(const path &dir);

// ── full-buffer loaders ────────────────────────────────────────────────
std::expected<blob, error> load_buffered(const path &p);
std::expected<blob, error> load_mapped(const path &p);

// ── temp-file lifecycle ────────────────────────────────────────────────
std::expected<file_handle, error> open_unique_tmp(const path &target,
                                                  path &out_path);
void close_and_discard(file_handle h, const path &tmp) noexcept;

// ── durability ─────────────────────────────────────────────────────────
std::expected<void, error> flush_file(file_handle h, const path &p);
std::expected<void, error> rename_atomic(const path &from, const path &to);

} // namespace files
