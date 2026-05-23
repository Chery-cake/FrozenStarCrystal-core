import std.compat;

auto main(int argc, char *argv[]) -> int {

  std::println("argc: ", argc);

  std::ranges::for_each(std::views::iota(argc, 0), [&argv](uint32_t i) {
    std::println("argv[{}]: {}", i, argv[i]);
  });

  return 0;
}
