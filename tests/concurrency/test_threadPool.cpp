#include <cassert>
import concurrency_helper;

void test_create() {
  TEST("create");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p);

  assert(t.size() == std::thread::hardware_concurrency());

  t.resize(2);
  assert(t.size() == 2);

  t.resize(10);
  assert(t.size() == 10);

  concurrency::pool::ThreadPool tp(p, 5);
  assert(tp.size() == 5);

  PASS();
}

void test_submit() {
  TEST("submit");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p);

  auto f = [](int x) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return x;
  };

  std::vector<std::future<int>> fi;

  std::ranges::for_each(std::views::iota(0, 12), [&t, f, &fi](uint32_t i) {
    fi.push_back(t.submit(f, i));
  });

  assert(t.size() == std::thread::hardware_concurrency());

  t.resize(2);
  assert(t.size() == 2);
  t.resize(10);
  assert(t.size() == 10);

  std::atomic<int> result = 0;
  int expected = 0;

  std::ranges::for_each(std::views::iota(0, 12),
                        [&fi, &result, &expected](uint32_t i) {
                          result += fi[i].get();
                          expected += i;
                        });

  assert(result == expected);

  t.resize(8);
  assert(t.size() == 8);

  auto f2 = [](std::atomic<int> &r) {
    r += 1;
    assert(true);
  };
  result = 0;
  std::ranges::for_each(std::views::iota(0, 12), [&t, f2, &result](uint32_t) {
    t.submit(f2, std::ref(result));
  });
  t.wait();
  assert(result.load() == 12);

  PASS();
}

int main() {
  std::println("=== Concurrency Thread Pool Tests ===");

  static auto tests = []() {
    std::ranges::for_each(std::views::iota(0, 5), [](uint32_t) {
      test_create();
      test_submit();
    });
  };

  static auto ex = []() {
    std::println("Started id: {}", std::this_thread::get_id());
    tests();
  };

  std::array<std::jthread, 5> threads;

  std::ranges::for_each(threads,
                        [](std::jthread &th) { th = std::jthread(ex); });

  std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });

  ex();

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
