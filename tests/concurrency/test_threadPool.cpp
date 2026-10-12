#include <cassert>
import concurrency_helper;

// ─── Generic suite ──────────────────────────────────────────────────

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_create() {
  ct::TEST("create");
  concurrency::pool::ThreadPool<TQ, Pushed> t{};
  assert(t.size() == std::thread::hardware_concurrency());
  assert((t.behaviour() == concurrency::queues::behaviour_of<TQ, Pushed>()));
  t.resize(2);
  assert(t.size() == 2);
  t.resize(10);
  assert(t.size() == 10);
  concurrency::pool::ThreadPool<TQ, Pushed> tp(5);
  assert(tp.size() == 5);
  ct::PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_empty() {
  ct::TEST("wait empty");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  t.wait();
  t.wait();
  ct::PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_for_submit_detach() {
  ct::TEST("wait for submit_detach");

  if constexpr (concurrency::queues::Looping<TQ, Pushed>) {
    // Looping queues don't drain in wait(); skip.
    return;
  }

  concurrency::pool::ThreadPool<TQ, Pushed> t(4);
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
  ct::PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_repeated() {
  ct::TEST("wait repeated");
  if constexpr (concurrency::queues::Looping<TQ, Pushed>)
    return;

  concurrency::pool::ThreadPool<TQ, Pushed> t{4};
  constexpr int kIters = 500;

  std::ranges::for_each(std::views::iota(0, kIters), [&](int) {
    std::atomic<int> ran{0};
    t.submit_detach([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
    t.wait();
    assert(ran.load() == 1);
  });
  ct::PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_wait_concurrent_submit() {
  ct::TEST("wait concurrent submit");
  if constexpr (concurrency::queues::Looping<TQ, Pushed>)
    return;

  concurrency::pool::ThreadPool<TQ, Pushed> t{4};
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
  ct::PASS();
}

// ─── Generic suite runner ───────────────────────────────────────────

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
static void generic_suite() {
  test_create<TQ, Pushed>();
  test_wait_empty<TQ, Pushed>();
  test_wait_for_submit_detach<TQ, Pushed>();
  test_wait_repeated<TQ, Pushed>();
  test_wait_concurrent_submit<TQ, Pushed>();
}

// ─── Futured submit (only when Pushed == Task) ──────────────────────

template <concurrency::queues::Queue TQ>
  requires(std::same_as<typename TQ::entry_type, ct::Task> ||
           std::same_as<TQ, ct::Fifo>)
void test_submit() {
  ct::TEST("submit");
  concurrency::pool::ThreadPool<TQ> t{};

  auto f = [](int x) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(100));
    return x;
  };

  std::vector<std::future<int>> fi;
  std::ranges::for_each(std::views::iota(0, 12), [&t, f, &fi](uint32_t i) {
    fi.push_back(t.submit(f, i));
  });

  int expected = 0, got = 0;
  for (uint32_t i = 0; i < 12; ++i) {
    got += fi[i].get();
    expected += i;
  }
  assert(got == expected);

  std::atomic<int> result = 0;
  auto f2 = [](std::atomic<int> &r) { r += 1; };
  std::ranges::for_each(std::views::iota(0, 12), [&t, f2, &result](uint32_t) {
    t.submit(f2, std::ref(result));
  });
  t.wait();
  assert(result.load() == 12);
  ct::PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_with_futures() {
  ct::TEST("wait with futures");
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
  ct::PASS();
}

template <concurrency::queues::Queue TQ> void test_wait_from_worker_throws() {
  ct::TEST("wait from worker throws");
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
  ct::PASS();
}

// ─── Priority-specific ──────────────────────────────────────────────

void test_priority_submit_order() {
  ct::TEST("priority submit order");

  concurrency::pool::ThreadPool<ct::BasicPriorityQueue, ct::PriorityEntry> t(1);
  std::atomic<bool> gate_entered{false}, release{false};

  t.submit_detach(
      [&] {
        gate_entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
      },
      1000);
  while (!gate_entered.load(std::memory_order_acquire))
    std::this_thread::yield();

  std::mutex mtx;
  std::vector<int> order;
  order.reserve(10);
  for (int p : {1, 5, 3, 8, 2, 10, 4, 9, 6, 7}) {
    t.submit_detach(
        [&, p] {
          std::lock_guard lk(mtx);
          order.push_back(p);
        },
        p);
  }

  release.store(true, std::memory_order_release);
  t.wait();

  assert(order.size() == 10);
  for (size_t i = 1; i < order.size(); ++i)
    assert(order[i - 1] > order[i]);
  assert(order.front() == 10);
  assert(order.back() == 1);
  ct::PASS();
}

void test_priority_payload_order() {
  ct::TEST("priority payload order");

  concurrency::pool::ThreadPool<ct::PayloadPriorityQueue,
                                ct::PayloadPriorityEntry>
      t(1);
  std::atomic<bool> gate_entered{false}, release{false};

  t.submit_detach(
      [&] {
        gate_entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
      },
      ct::PriorityPayload{1000, 0});
  while (!gate_entered.load(std::memory_order_acquire))
    std::this_thread::yield();

  std::mutex mtx;
  std::vector<int> order;
  for (int p : {1, 5, 3, 8}) {
    t.submit_detach(
        [&, p] {
          std::lock_guard lk(mtx);
          order.push_back(p);
        },
        ct::PriorityPayload{p, 0});
  }

  release.store(true, std::memory_order_release);
  t.wait();

  assert(order.size() == 4);
  assert(order.front() == 8);
  assert(order.back() == 1);
  ct::PASS();
}

void test_priority_duplicates() {
  ct::TEST("priority duplicates");
  concurrency::pool::ThreadPool<ct::BasicPriorityQueue, ct::PriorityEntry> t(1);
  std::atomic<int> count{0};
  for (int i = 0; i < 3; ++i)
    t.submit_detach([&count] { count.fetch_add(2); }, 5);
  t.submit_detach([&count] { count.fetch_add(100); }, 1);
  t.wait();
  assert(count.load() == 2 + 2 + 2 + 100);
  ct::PASS();
}

// ─── Loop-specific ──────────────────────────────────────────────────

void test_loop_repeats() {
  ct::TEST("loop repeats");
  concurrency::pool::ThreadPool<ct::Loop, ct::Task> t(1);
  auto count = std::make_shared<std::atomic<int>>(0);
  t.submit_detach([count] { count->fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 100 && count->load() < 5; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(count->load() >= 5);

  t.queue()->clear();
  ct::PASS();
}

void test_loop_multiple_entries() {
  ct::TEST("loop multiple entries");
  concurrency::pool::ThreadPool<ct::Loop, ct::Task> t(1);
  auto a = std::make_shared<std::atomic<int>>(0);
  auto b = std::make_shared<std::atomic<int>>(0);

  t.submit_detach([a] { a->fetch_add(1, std::memory_order_relaxed); });
  t.submit_detach([b] { b->fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 200 && (a->load() < 3 || b->load() < 3); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(a->load() >= 3);
  assert(b->load() >= 3);

  t.queue()->clear();
  ct::PASS();
}

// ─── Payload-specific pool behavior ─────────────────────────────────

void test_fifo_payload_pool() {
  ct::TEST("fifo payload pool");
  concurrency::pool::ThreadPool<ct::FifoPayload, ct::SimplePayloadEntry> t(4);
  std::atomic<int> sum{0};
  for (int i = 0; i < 8; ++i)
    t.submit_detach([&, i] { sum.fetch_add(i); }, ct::SimplePayload{i});
  t.wait();
  assert(sum.load() == 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7);
  ct::PASS();
}

// ─── Drivers ────────────────────────────────────────────────────────

static void fifo_task_suite() { generic_suite<ct::Fifo, ct::Task>(); }
static void fifo_payload_suite() {
  generic_suite<ct::FifoPayload, ct::SimplePayloadEntry>();
}
static void priority_suite() {
  generic_suite<ct::BasicPriorityQueue, ct::PriorityEntry>();
}
static void priority_payload_suite() {
  generic_suite<ct::PayloadPriorityQueue, ct::PayloadPriorityEntry>();
}
static void loop_task_suite() {
  // Skip generic_suite for looping (wait() doesn't drain).
  test_create<ct::Loop, ct::Task>();
}

static void fifo_specific_suite() {
  test_submit<ct::Fifo>();
  test_wait_with_futures<ct::Fifo>();
  test_wait_from_worker_throws<ct::Fifo>();
  test_fifo_payload_pool();
}

static void priority_specific_suite() {
  test_priority_submit_order();
  test_priority_duplicates();
  test_priority_payload_order();
}

static void loop_specific_suite() {
  test_loop_repeats();
  test_loop_multiple_entries();
}

template <auto Suite> static void ex(uint32_t repeats) {
  std::ranges::for_each(std::views::iota(0U, repeats),
                        [](uint32_t) { Suite(); });
}

int main() {
  std::println("=== Concurrency Thread Pool Tests ===");

  std::array<std::jthread, 8> threads;
  threads[0] = std::jthread(ex<fifo_task_suite>, 5);
  threads[1] = std::jthread(ex<fifo_payload_suite>, 5);
  threads[2] = std::jthread(ex<priority_suite>, 5);
  threads[3] = std::jthread(ex<priority_payload_suite>, 5);
  threads[4] = std::jthread(ex<loop_task_suite>, 5);
  threads[5] = std::jthread(ex<fifo_specific_suite>, 5);
  threads[6] = std::jthread(ex<priority_specific_suite>, 5);
  threads[7] = std::jthread(ex<loop_specific_suite>, 5);
  for (auto &t : threads)
    t.join();

  // Main-thread run.
  fifo_task_suite();
  fifo_payload_suite();
  priority_suite();
  priority_payload_suite();
  loop_task_suite();
  fifo_specific_suite();
  priority_specific_suite();
  loop_specific_suite();

  std::println("\n{}/{} tests passed", ct::tests_passed.load(),
               ct::tests_run.load());
  return (ct::tests_passed == ct::tests_run) ? 0 : 1;
}
