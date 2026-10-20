module;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

export module files:locations;

import std.compat;
import :structs;

// ── module-internal helpers ────────────────────────────────────────────
namespace files {

struct env_snapshot {
  path local_appdata;
  path appdata;
  path xdg_data_home;
  path home;
};

inline const env_snapshot &env() {
  static const env_snapshot snap = [] {
    auto get = [](const char *n) -> path {
      // Serialized by the magic-static guard: runs on exactly one
      // thread, once. The returned pointer is copied into `path`
      // immediately, so no reference into the environment block is
      // retained after this lambda returns.
      const char *v = std::getenv(n); // NOLINT(concurrency-mt-unsafe)
      return (v != nullptr && *v != '\0') ? path{v} : path{};
    };
    return env_snapshot{
        .local_appdata = get("LOCALAPPDATA"),
        .appdata = get("APPDATA"),
        .xdg_data_home = get("XDG_DATA_HOME"),
        .home = get("HOME"),
    };
  }();
  return snap;
}

inline path user_data_root() {
#ifdef _WIN32
  const auto &e = env();
  if (!e.local_appdata.empty())
    return e.local_appdata;
  return e.appdata;
#elif defined(__APPLE__)
  if (auto h = env().home; !h.empty())
    return h / "Library" / "Application Support";
  return {};
#else
  const auto &e = env();
  if (!e.xdg_data_home.empty()) {
    return e.xdg_data_home;
  }
  if (!e.home.empty()) {
    return e.home / ".local" / "share";
  }
  return {};
#endif
}

inline path executable_path() {
#ifdef _WIN32
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    DWORD n =
        GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0)
      return {};
    if (n < buf.size()) {
      buf.resize(n);
      return path{buf};
    }
    buf.resize(buf.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t sz = 0;
  _NSGetExecutablePath(nullptr, &sz);
  std::string buf(sz, '\0');
  if (_NSGetExecutablePath(buf.data(), &sz) != 0)
    return {};
  buf.resize(std::strlen(buf.c_str()));
  return path{buf};
#else
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? path{} : p;
#endif
}

} // namespace files

// ── public API ─────────────────────────────────────────────────────────
export namespace files {

// Compile-time dispatch (preferred when Location is known at call site).
template <Location L>
[[nodiscard]] std::expected<path, error>
default_dir(std::string_view app = {}) {
  if constexpr (L == Location::Temp) {
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path(ec);
    if (ec) {
      return std::unexpected{error{ec, {}, "default_dir:temp"}};
    }
    if (!app.empty()) {
      base /= path{std::string(app)};
    }
    return base;
  } else if constexpr (L == Location::Data) {
    auto base = user_data_root();
    if (base.empty()) {
      return std::unexpected{
          error{"cannot resolve data dir", {}, "default_dir:data"}};
    }
    if (!app.empty()) {
      base /= path{std::string(app)};
    }
    return base;
  } else if constexpr (L == Location::Executable) {
    auto exe = executable_path();
    if (exe.empty()) {
      return std::unexpected{
          error{"cannot resolve executable", {}, "default_dir:exe"}};
    }
    return exe.parent_path(); // no app subdir
  } else {
    return std::unexpected{error{"invalid location", {}, "default_dir"}};
  }
}

// Runtime dispatch — needed by find() and other value-based call sites.
[[nodiscard]] inline std::expected<path, error>
default_dir(Location loc, std::string_view app = {}) {
  switch (loc) {
  case Location::Temp:
    return default_dir<Location::Temp>(app);
  case Location::Data:
    return default_dir<Location::Data>(app);
  case Location::Executable:
    return default_dir<Location::Executable>(app);
  }
  return std::unexpected{error{"invalid location", {}, "default_dir"}};
}

// ── ensure_dir: template + runtime variants ────────────────────────────
template <Location L>
[[nodiscard]] std::expected<path, error> ensure_dir(std::string_view app = {}) {
  auto d = default_dir<L>(app);
  if (!d) {
    return d;
  }
  std::error_code ec;
  std::filesystem::create_directories(*d, ec);
  if (ec) {
    return std::unexpected{error{ec, *d, "ensure_dir"}};
  }
  return d;
}

[[nodiscard]] inline std::expected<path, error>
ensure_dir(Location loc, std::string_view app = {}) {
  auto d = default_dir(loc, app);
  if (!d) {
    return d;
  }
  std::error_code ec;
  std::filesystem::create_directories(*d, ec);
  if (ec) {
    return std::unexpected{error{ec, *d, "ensure_dir"}};
  }
  return d;
}

// ── default_path ───────────────────────────────────────────────────────
template <Location L>
[[nodiscard]] std::expected<path, error>
default_path(const path &relative, std::string_view app = {}) {
  auto d = default_dir<L>(app);
  if (!d) {
    return std::unexpected{std::move(d.error())};
  }
  return *d / relative;
}

[[nodiscard]] inline std::expected<path, error>
default_path(Location loc, const path &relative, std::string_view app = {}) {
  auto d = default_dir(loc, app);
  if (!d) {
    return std::unexpected{std::move(d.error())};
  }
  return *d / relative;
}

} // namespace files
namespace files {
template <Location L>
[[nodiscard]] inline std::optional<path> try_find(const path &relative,
                                                  std::string_view app) {
  auto d = default_dir<L>(app);
  if (!d) {
    return std::nullopt;
  }
  auto p = *d / relative;
  std::error_code ec;
  if (!std::filesystem::exists(p, ec) || ec) {
    return std::nullopt;
  }
  return p;
}
} // namespace files

export namespace files {

// ── find: search multiple locations, first hit wins ────────────────────
template <Location... Ls>
[[nodiscard]] inline std::expected<path, error>
find(const path &relative, std::string_view app = {}) {
  std::optional<path> found;
  ((found = try_find<Ls>(relative, app)).has_value() || ...);
  if (!found) {
    return std::unexpected{
        error{"not found in any location", relative, "find"}};
  }
  return std::move(*found);
}

[[nodiscard]] inline std::expected<path, error>
find(std::span<const Location> order, const path &relative,
     std::string_view app = {}) {
  auto valid = order |
               std::views::transform([&](Location loc) -> std::optional<path> {
                 auto d = default_dir(loc, app);
                 return d ? std::optional<path>{*d / relative} : std::nullopt;
               }) |
               std::views::filter([](auto &&o) {
                 if (!o) {
                   return false;
                 }
                 std::error_code ec;
                 return std::filesystem::exists(*o, ec) && !ec;
               }) |
               std::views::transform([](auto &&o) { return std::move(*o); });

  auto it = std::ranges::begin(valid);
  if (it == std::ranges::end(valid)) {
    return std::unexpected{
        error{"not found in any location", relative, "find"}};
  }
  return *it;
}

// initializer_list convenience: find({Location::Data, Location::Executable},
// ...)
[[nodiscard]] inline std::expected<path, error>
find(std::initializer_list<Location> order, const path &relative,
     std::string_view app = {}) {
  return find(std::span<const Location>{order.begin(), order.size()}, relative,
              app);
}

} // namespace files
