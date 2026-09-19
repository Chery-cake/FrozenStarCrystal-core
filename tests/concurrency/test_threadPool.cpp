#include <cassert>
import concurrency_helper;

template <concurrency::queues::Queue TQ> void test_create() {
  TEST("create");

  concurrency::pool::ThreadPool<TQ> t{};

  assert(t.size() == std::thread::hardware_concurrency());

  t.resize(2);
  assert(t.size() == 2);

  t.resize(10);
  assert(t.size() == 10);

  concurrency::pool::ThreadPool<TQ> tp(5);
  assert(tp.size() == 5);

  PASS();
}

template <concurrency::queues::Queue TQ> void test_submit() {
  TEST("submit");

  concurrency::pool::ThreadPool<TQ> t{};

  auto f = [](int x) {
    // TODO
    // decide a better time or operation that could lead to a deadlock
    std::this_thread::sleep_for(std::chrono::nanoseconds(100));
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

template <concurrency::queues::Queue TQ> void test_wait_empty() {
  TEST("wait empty");

  concurrency::pool::ThreadPool<TQ> t(2);

  // Nothing submitted: wait must return immediately.
  t.wait();
  t.wait(); // idempotent

  PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_for_submit_detach() {
  TEST("wait for submit_detach");

  concurrency::pool::ThreadPool<TQ> t(4);

  constexpr int kTasks = 64;
  std::atomic<int> completed{0};

  std::ranges::for_each(std::views::iota(0, kTasks), [&t, &completed](int) {
    t.submit_detach([&completed] {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
      completed.fetch_add(1, std::memory_order_relaxed);
    });
  });

  t.wait();
  assert(completed.load(std::memory_order_relaxed) == kTasks);

  PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_with_futures() {
  TEST("wait with futures");

  concurrency::pool::ThreadPool<TQ> t{4};

  std::atomic<int> sum{0};
  std::vector<std::future<void>> futs;

  std::ranges::for_each(std::views::iota(0, 32), [&](int i) {
    futs.push_back(
        t.submit([&sum, i] { sum.fetch_add(i, std::memory_order_relaxed); }));
  });

  t.wait();
  // wait() guarantees the task bodies have executed; we don't need .get().
  int expected = 0;
  for (int i = 0; i < 32; ++i)
    expected += i;
  assert(sum.load(std::memory_order_relaxed) == expected);

  PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_repeated() {
  TEST("wait repeated");

  concurrency::pool::ThreadPool<TQ> t{4};

  // Tight submit+wait loop. This is the pattern that exposes the
  // lost-wakeup race between `task_finished` (atomic decrement + notify)
  // and `wait` (mutex + cv) if the counter is not protected by the mutex.
  constexpr int kIters = 2000;
  std::ranges::for_each(std::views::iota(0, kIters), [&](int) {
    std::atomic<int> ran{0};
    t.submit_detach([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
    t.wait();
    assert(ran.load(std::memory_order_relaxed) == 1);
  });

  PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_concurrent_submit() {
  TEST("wait concurrent submit");

  concurrency::pool::ThreadPool<TQ> t{4};

  constexpr int kProducers = 4;
  constexpr int kPerProducer = 500;
  std::atomic<int> completed{0};

  std::vector<std::jthread> producers;
  producers.reserve(kProducers);
  for (int i = 0; i < kProducers; ++i) {
    producers.emplace_back([&] {
      std::ranges::for_each(std::views::iota(0, kPerProducer), [&](int) {
        t.submit_detach([&completed] {
          completed.fetch_add(1, std::memory_order_relaxed);
        });
      });
    });
  }

  for (auto &th : producers)
    th.join();
  t.wait();

  assert(completed.load(std::memory_order_relaxed) ==
         kProducers * kPerProducer);

  PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_from_worker_throws() {
  TEST("wait from worker throws");

  concurrency::pool::ThreadPool<TQ> t{2};

  std::atomic<bool> threw{false};

  auto fut = t.submit([&t, &threw] {
    try {
      t.wait();
    } catch (const std::logic_error &) {
      threw.store(true, std::memory_order_relaxed);
    }
  });
  fut.wait();

  assert(threw.load(std::memory_order_relaxed));

  PASS();
}

template <concurrency::queues::Queue TQ> static void tests() {
  std::ranges::for_each(std::views::iota(0, 25), [](uint32_t) {
    test_create<TQ>();
    test_submit<TQ>();

    test_wait_empty<TQ>();
    test_wait_for_submit_detach<TQ>();
    test_wait_with_futures<TQ>();
    test_wait_repeated<TQ>();
    test_wait_concurrent_submit<TQ>();
    test_wait_from_worker_throws<TQ>();
  });
};

template <concurrency::queues::Queue TQ> static void ex() {
  {
    std::lock_guard lock(log_mutex);
    std::println("Started id: {}", std::this_thread::get_id());
  }
  tests<TQ>();
};

int main() {
  std::println("=== Concurrency Thread Pool Tests ===");

  std::array<std::jthread, 5> threads;

  std::ranges::for_each(threads, [](std::jthread &th) {
    th = std::jthread(ex<concurrency::queues::Fifo>);
  });

  std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });

  ex<concurrency::queues::Fifo>();

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
