#include <cassert>
import concurrency_helper;

void test_fifo() {
  TEST("fifo");

  concurrency::queues::FifoTaskQueue queue;

  std::atomic<int> count{0};

  queue.push([&count]() { count.fetch_add(1); });
  queue.push([&count]() { count.fetch_add(2); });
  queue.push([&count]() { count.fetch_add(3); });

  concurrency::queues::Task t;

  std::stop_source ss;
  std::stop_token st = ss.get_token();

  assert(queue.try_pop(t, st));
  t();
  assert(count == 1);

  assert(queue.try_pop(t, st));
  t();
  assert(count == 3);

  assert(queue.try_pop(t, st));
  t();
  assert(count == 6);

  std::jthread jt([&queue, &st] {
    concurrency::queues::Task t;
    assert(queue.try_pop(t, st));
    t();
  });
  assert(count == 6);

  assert(jt.joinable());

  queue.push([&count]() { count.fetch_add(1); });
  queue.push([&count]() { count.fetch_add(2); });

  jt.join();
  assert(count == 7);

  ss.request_stop();

  assert(!queue.try_pop(t, st));
  assert(count == 7);

  PASS();
}

int main() {
  std::println("=== Concurrency queues Tests ===");

  static auto tests = []() {
    std::ranges::for_each(std::views::iota(0, 50),
                          [](uint32_t) { test_fifo(); });
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
