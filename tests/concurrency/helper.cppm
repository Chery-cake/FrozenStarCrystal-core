module;

export module concurrency_helper;

export import std.compat;
export import concurrency;

export {
  std::atomic<int> tests_run{0};
  std::atomic<int> tests_passed{0};

  void TEST(std::string_view name) {
    tests_run.fetch_add(1, std::memory_order_relaxed);
    std::println("[TEST] {} ... ", name);
  }

  void PASS() {
    tests_passed.fetch_add(1, std::memory_order_relaxed);
    std::println("PASSED");
  }
}
