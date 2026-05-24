import std.compat;

#include <cstdio>
#include <cstdlib>

auto main(int argc, char **argv) -> int {

  try {
    std::println("argc: {}", argc);

    std::ranges::for_each(std::views::iota(0, argc), [&argv](uint32_t i) {
      std::println("argv[{}]: {}", i, argv[i]);
    });
  } catch (const std::exception &e) {
    std::fputs("Exception: ", stderr);
    std::fputs(e.what(), stderr);
    std::fputc('\n', stderr);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
