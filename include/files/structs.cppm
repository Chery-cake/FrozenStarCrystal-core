module;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

export module files:structs;

import std.compat;

export namespace files {

#ifdef _WIN32
using file_handle = HANDLE;
inline constexpr file_handle invalid_handle = INVALID_HANDLE_VALUE;
#else
using file_handle = int;
inline constexpr file_handle invalid_handle = -1;
#endif

using blob = std::vector<std::byte>;
using path = std::filesystem::path;

struct error : std::runtime_error {
  std::error_code code;
  std::filesystem::path path;
  std::string_view op;

  error(std::error_code ec, std::filesystem::path p, std::string_view o)
      : std::runtime_error("error: " + ec.message() + " - file: " + p.string() +
                           " - operation: " + std::string(o)),
        code(ec), path(std::move(p)), op(o) {}

  error(std::string_view msg, std::filesystem::path p, std::string_view o)
      : std::runtime_error("error: " + std::string(msg) + " - file: " +
                           p.string() + " - operation: " + std::string(o)),
        code(std::make_error_code(std::errc::io_error)), path(std::move(p)),
        op(o) {}
};

enum class Sync : std::uint8_t {
  None,
  File,
  File_and_dir,
};

enum class IOPolicy : std::uint8_t {
  Buffered,
  Mapped,
  Auto,
};

enum class Location : std::uint8_t {
  Temp,       // OS temp dir (may be purged) — %TEMP%, $TMPDIR, /tmp
  Data,       // long-term user data — %LOCALAPPDATA%, $XDG_DATA_HOME,
              // ~/Library/Application Support
  Executable, // directory containing the running executable
  Absolute,   // absolute path, no pre defined location
};

inline std::span<const std::byte> as_span(std::string_view sv) noexcept {
  return {reinterpret_cast<const std::byte *>(sv.data()), sv.size()};
}
inline std::string_view as_sv(std::span<const std::byte> b) noexcept {
  return {reinterpret_cast<const char *>(b.data()), b.size()};
}
inline blob to_blob(std::span<const std::byte> b) {
  return {b.begin(), b.end()};
}
inline blob to_blob(std::string_view sv) { return to_blob(as_span(sv)); }

template <typename T>
concept Encoder = requires(const T &t, std::span<const std::byte> in) {
  { t.encode(in) } -> std::convertible_to<blob>;
};

template <typename T>
concept StreamEncoder = requires(T &t, std::span<const std::byte> in) {
  { t.encode(in) } -> std::convertible_to<blob>;
};

template <typename T>
concept Decoder = requires(const T &t, std::span<const std::byte> in) {
  { t.decode(in) } -> std::convertible_to<blob>;
};

template <typename T>
concept StreamDecoder = requires(T &t, std::span<const std::byte> in) {
  { t.decode(in) } -> std::convertible_to<blob>;
};

template <typename T>
concept Codec = Encoder<T> && Decoder<T>;

template <typename T>
concept StreamCodec = StreamEncoder<T> && StreamDecoder<T>;

template <typename T>
concept NamedCodec = Codec<T> && requires(const T &t) {
  { t.name() } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept StreamNamedCodec = StreamCodec<T> && requires(const T &t) {
  { t.name() } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept Reader = requires(T &r, std::span<std::byte> buf) {
  { r.read(buf) } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept Writer = requires(T &w, std::span<const std::byte> buf) {
  { w.write(buf) } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept ReaderWriter = Reader<T> && Writer<T>;

template <typename F>
concept ChunkConsumer = requires(F &f, std::span<const std::byte> chunk) {
  { f(chunk) };
};

template <typename Transport>
concept FileTransport = requires(const path &p, std::span<const std::byte> d) {
  { Transport::load(p) } -> std::same_as<std::expected<blob, error>>;
  { Transport::save(p, d) } -> std::same_as<std::expected<void, error>>;
};

} // namespace files
