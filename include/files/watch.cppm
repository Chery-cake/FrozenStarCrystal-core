module;

export module files:watch;

import std.compat;
import :structs;
import :io;

export namespace files {

struct file_stamp {
  std::filesystem::file_time_type mtime;
  std::uintmax_t size = 0;
  bool valid = false;

  friend bool operator==(const file_stamp &, const file_stamp &) = default;
  constexpr auto operator<=>(const file_stamp &) const noexcept = default;

  [[nodiscard]] static std::expected<file_stamp, error> read(const path &p) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(p, ec);
    if (ec) {
      return std::unexpected{error{ec, p, "file_stamp:mtime"}};
    }
    auto s = std::filesystem::file_size(p, ec);
    if (ec) {
      return std::unexpected{error{ec, p, "file_stamp:size"}};
    }
    return file_stamp{.mtime = t, .size = s, .valid = true};
  }
};

// True if p's current fingerprint differs from baseline (or is unreadable).
[[nodiscard]] inline bool changed_since(const path &p,
                                        const file_stamp &baseline) {
  auto now = file_stamp::read(p);
  return !now || !(*now == baseline);
}

// Caches a value loaded from disk; refresh() reloads only if the
// on-disk mtime/size has changed since the last (re)load.
template <typename T = blob> class reloadable_file {
public:
  using loader_fn =
      std::move_only_function<std::expected<T, error>(const path &)>;

private:
  path p_;
  loader_fn load_;
  std::optional<T> value_;
  file_stamp stamp_{};

public:
  reloadable_file(path p, loader_fn load)
      : p_(std::move(p)), load_(std::move(load)) {}

  // Uses files::load<Policy>() as the loader (T must be blob).
  template <IOPolicy Policy = IOPolicy::Auto>
    requires std::is_same_v<T, blob>
  static reloadable_file with_default_loader(path p) {
    return reloadable_file{std::move(p),
                           [](const path &pp) { return load<Policy>(pp); }};
  }

  // Returns true if the value was (re)loaded, false if the cache was valid.
  std::expected<bool, error> refresh() {
    auto now = file_stamp::read(p_);
    if (!now) {
      return std::unexpected{std::move(now.error())};
    }
    if (value_ && *now == stamp_) {
      return false;
    }

    auto loaded = load_(p_);
    if (!loaded) {
      return std::unexpected{std::move(loaded.error())};
    }
    value_ = std::move(*loaded);
    stamp_ = *now;
    return true;
  }

  // Unconditional reload.
  std::expected<void, error> reload() {
    auto loaded = load_(p_);
    if (!loaded) {
      return std::unexpected{std::move(loaded.error())};
    }
    auto now = file_stamp::read(p_);
    if (!now) {
      return std::unexpected{std::move(now.error())};
    }
    value_ = std::move(*loaded);
    stamp_ = *now;
    return {};
  }

  [[nodiscard]] const path &file() const noexcept { return p_; }
  [[nodiscard]] const T *get() const noexcept {
    return value_ ? &*value_ : nullptr;
  }
  [[nodiscard]] const T &value() const { return *value_; }
  [[nodiscard]] bool loaded() const noexcept { return value_.has_value(); }
  [[nodiscard]] const file_stamp &stamp() const noexcept { return stamp_; }
};

} // namespace files
