#include <cassert>
import concurrency_helper;

// ─── Generic tests: work for any (TQ, Pushed, B) ───────────────────────

template <typename TQ, typename Pushed, concurrency::queues::Behaviour B>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_create() {
  TEST("create");

  concurrency::pool::ThreadPool<TQ, Pushed, B> t{};
  assert(t.size() == std::thread::hardware_concurrency());
  assert(t.behaviour() == B);

  t.resize(2);
  assert(t.size() == 2);

  t.resize(10);
  assert(t.size() == 10);

  concurrency::pool::ThreadPool<TQ, Pushed, B> tp(5);
  assert(tp.size() == 5);

  PASS();
}

template <typename TQ, typename Pushed, concurrency::queues::Behaviour B>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_empty() {
  TEST("wait empty");
  concurrency::pool::ThreadPool<TQ, Pushed, B> t(2);
  t.wait();
  t.wait();
  PASS();
}

template <typename TQ, typename Pushed, concurrency::queues::Behaviour B>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_for_submit_detach() {
  TEST("wait for submit_detach");

  concurrency::pool::ThreadPool<TQ, Pushed, B> t(4);
  constexpr int kTasks = 64;
  std::atomic<int> completed{0};

  std::ranges::for_each(std::views::iota(0, kTasks), [&t, &completed](int) {
    t.submit_detach([&completed] {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
      completed.fetch_add(1, std::memory_order_relaxed);
    });
  });

  t.wait();
  assert(completed.load() == kTasks);
  PASS();
}

template <typename TQ, typename Pushed, concurrency::queues::Behaviour B>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_repeated() {
  TEST("wait repeated");

  concurrency::pool::ThreadPool<TQ, Pushed, B> t{4};
  constexpr int kIters = 2000;

  std::ranges::for_each(std::views::iota(0, kIters), [&](int) {
    std::atomic<int> ran{0};
    t.submit_detach([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
    t.wait();
    assert(ran.load() == 1);
  });
  PASS();
}

template <typename TQ, typename Pushed, concurrency::queues::Behaviour B>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_concurrent_submit() {
  TEST("wait concurrent submit");

  concurrency::pool::ThreadPool<TQ, Pushed, B> t{4};
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

  assert(completed.load() == kProducers * kPerProducer);
  PASS();
}

// ─── Future-returning submit: only valid when Pushed == Task ───────────
// (submit calls submit_detach with an empty pack, so `Pushed{task}` must
//  be valid. PriorityEntry{int, Task} cannot be built from a bare Task.)

template <concurrency::queues::Queue TQ> void test_submit() {
  TEST("submit");

  concurrency::pool::ThreadPool<TQ> t{};

  auto f = [](int x) {
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

  auto f2 = [](std::atomic<int> &r) { r += 1; };
  result = 0;
  std::ranges::for_each(std::views::iota(0, 12), [&t, f2, &result](uint32_t) {
    t.submit(f2, std::ref(result));
  });
  t.wait();
  assert(result.load() == 12);
  PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_with_futures() {
  TEST("wait with futures");
  concurrency::pool::ThreadPool<TQ> t{4};
  std::atomic<int> sum{0};
  std::ranges::for_each(std::views::iota(0, 32), [&](int i) {
    t.submit([&sum, i] { sum.fetch_add(i, std::memory_order_relaxed); });
  });
  t.wait();
  int expected = 0;
  for (int i = 0; i < 32; ++i)
    expected += i;
  assert(sum.load() == expected);
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
      threw.store(true);
    }
  });
  fut.wait();
  assert(threw.load());
  PASS();
}

// ─── Priority-specific: submit_detach with push args ───────────────────

void test_priority_submit_order() {
  TEST("priority submit order");

  concurrency::pool::ThreadPool<PriorityQueue, PriorityEntry> t(1);

  std::atomic<bool> gate_entered{false};
  std::atomic<bool> release{false};

  // Highest priority so it is popped before anything else.
  t.submit_detach(
      [&gate_entered, &release] {
        gate_entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
      },
      1000);

  // Wait for the gate to actually be running on the worker.
  while (!gate_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  // Worker is now busy; these ten all sit in the queue before it can pop.
  std::mutex order_mtx;
  std::vector<int> order;
  order.reserve(10);

  for (int p : {1, 5, 3, 8, 2, 10, 4, 9, 6, 7}) {
    t.submit_detach(
        [&order, &order_mtx, p] {
          std::lock_guard lk(order_mtx);
          order.push_back(p);
        },
        p);
  }

  release.store(true, std::memory_order_release);
  t.wait();

  assert(order.size() == 10);
  for (size_t i = 1; i < order.size(); ++i) {
    assert(order[i - 1] > order[i]);
  }
  assert(order.front() == 10);
  assert(order.back() == 1);
  PASS();
}

void test_priority_duplicates() {
  TEST("priority duplicates");

  concurrency::pool::ThreadPool<PriorityQueue, PriorityEntry> t(1);
  std::atomic<int> count{0};

  for (int i = 0; i < 3; ++i) {
    t.submit_detach([&count] { count.fetch_add(2); }, 5);
  }
  t.submit_detach([&count] { count.fetch_add(100); }, 1);
  t.wait();
  assert(count.load() == 2 + 2 + 2 + 100);
  PASS();
}

// ─── Looping-specific ──────────────────────────────────────────────────

void test_loop_repeats() {
  TEST("loop repeats");

  concurrency::pool::ThreadPool<concurrency::queues::Loop,
                                concurrency::queues::Task,
                                concurrency::queues::Behaviour::Looping>
      t(1);

  auto count = std::make_shared<std::atomic<int>>(0);
  t.submit_detach([count] { count->fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 100 && count->load() < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(count->load() >= 5);

  t.queue()->clear();
  PASS();
}

void test_loop_multiple_entries() {
  TEST("loop multiple entries");

  concurrency::pool::ThreadPool<concurrency::queues::Loop,
                                concurrency::queues::Task,
                                concurrency::queues::Behaviour::Looping>
      t(1);

  auto a = std::make_shared<std::atomic<int>>(0);
  auto b = std::make_shared<std::atomic<int>>(0);

  t.submit_detach([a] { a->fetch_add(1, std::memory_order_relaxed); });
  t.submit_detach([b] { b->fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 200 && (a->load() < 3 || b->load() < 3); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(a->load() >= 3);
  assert(b->load() >= 3);

  t.queue()->clear();
  // t's destructor joins the worker before this frame's shared_ptrs die.
  PASS();
}

// ─── Drivers ───────────────────────────────────────────────────────────

using Fifo = concurrency::queues::Fifo;
using Loop = concurrency::queues::Loop;
using Task = concurrency::queues::Task;
using B = concurrency::queues::Behaviour;

template <typename TQ, typename Pushed, B Behaviour>
  requires(concurrency::queues::Queue<TQ, Pushed>)
static void generic_suite() {
  test_create<TQ, Pushed, Behaviour>();
  test_wait_empty<TQ, Pushed, Behaviour>();
  test_wait_for_submit_detach<TQ, Pushed, Behaviour>();
  test_wait_repeated<TQ, Pushed, Behaviour>();
  test_wait_concurrent_submit<TQ, Pushed, Behaviour>();
}

static void fifo_suite() {
  generic_suite<Fifo, Task, B::Consuming>();
  test_submit<Fifo>();
  test_wait_with_futures<Fifo>();
  test_wait_from_worker_throws<Fifo>();
}

static void priority_suite() {
  generic_suite<PriorityQueue, PriorityEntry, B::Consuming>();
  test_priority_submit_order();
  test_priority_duplicates();
}

static void loop_suite() {
  test_create<Loop, Task, B::Looping>();
  test_loop_repeats();
  test_loop_multiple_entries();
}

template <auto Suite> static void ex(uint32_t repeats) {
  std::ranges::for_each(std::views::iota(0U, repeats),
                        [](uint32_t) { Suite(); });
}

int main() {
  std::println("=== Concurrency Thread Pool Tests ===");

  std::array<std::jthread, 3> threads;
  threads[0] = std::jthread(ex<fifo_suite>, 10);
  threads[1] = std::jthread(ex<priority_suite>, 10);
  threads[2] = std::jthread(ex<loop_suite>, 10);
  for (auto &t : threads)
    t.join();

  fifo_suite();
  priority_suite();
  loop_suite();

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
