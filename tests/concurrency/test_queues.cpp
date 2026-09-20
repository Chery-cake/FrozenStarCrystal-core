#include <cassert>
import concurrency_helper;

void test_fifo() {
  TEST("fifo");

  concurrency::queues::Fifo queue;

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

  assert(queue.try_pop(t, st));
  t();
  assert(count == 9);

  assert(!queue.try_pop(t, st));
  assert(count == 9);

  PASS();
}

// ─── Test entry, comparator, and queue alias ────────────────────────────
struct PriorityEntry {
  int priority;
  concurrency::queues::Task task;
};

struct PriorityCompare {
  bool operator()(const PriorityEntry &a,
                  const PriorityEntry &b) const noexcept {
    return a.priority < b.priority; // max-heap on `priority`
  }
};

using PriorityQueue =
    concurrency::queues::Priority<PriorityEntry, std::vector<PriorityEntry>,
                                  PriorityCompare>;

// ─── Runtime tests ──────────────────────────────────────────────────────

void test_priority_order() {
  TEST("priority order");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;

  // Pushed out of order on purpose.
  queue.push(PriorityEntry{1, [&order] { order.push_back(1); }});
  queue.push(PriorityEntry{5, [&order] { order.push_back(5); }});
  queue.push(PriorityEntry{3, [&order] { order.push_back(3); }});
  queue.push(PriorityEntry{7, [&order] { order.push_back(7); }});

  concurrency::queues::Task t;
  for (int i = 0; i < 4; ++i) {
    assert(queue.try_pop(t, st));
    t();
  }

  assert(order == std::vector<int>({7, 5, 3, 1}));

  // Drain on stop: queue is empty, so this returns false.
  ss.request_stop();
  assert(!queue.try_pop(t, st));

  PASS();
}

void test_priority_duplicates() {
  TEST("priority duplicates");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};

  // Three items at priority 5 (order among them is unspecified — the
  // heap isn't stable), one at priority 1.
  queue.push(PriorityEntry{5, [&count] { count.fetch_add(1); }});
  queue.push(PriorityEntry{5, [&count] { count.fetch_add(2); }});
  queue.push(PriorityEntry{5, [&count] { count.fetch_add(4); }});
  queue.push(PriorityEntry{1, [&count] { count.fetch_add(100); }});

  concurrency::queues::Task t;

  // First three pops must be the priority-5 entries. Sum is order-free.
  for (int i = 0; i < 3; ++i) {
    assert(queue.try_pop(t, st));
    t();
  }
  assert(count.load() == 1 + 2 + 4);

  // Last pop is the priority-1 entry.
  assert(queue.try_pop(t, st));
  t();
  assert(count.load() == 1 + 2 + 4 + 100);

  ss.request_stop();
  assert(!queue.try_pop(t, st));

  PASS();
}

void test_priority_thread() {
  TEST("priority thread");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};

  queue.push(PriorityEntry{1, [&count] { count.fetch_add(1); }});
  queue.push(PriorityEntry{2, [&count] { count.fetch_add(2); }});
  queue.push(PriorityEntry{3, [&count] { count.fetch_add(3); }});

  // Worker pops the highest-priority task (3) and runs it.
  std::jthread jt([&queue, &st] {
    concurrency::queues::Task t;
    assert(queue.try_pop(t, st));
    t();
  });
  jt.join();
  assert(count.load() == 3);

  // Main thread drains the remaining two in priority order: 2, then 1.
  concurrency::queues::Task t;
  assert(queue.try_pop(t, st));
  t();
  assert(count.load() == 5);

  assert(queue.try_pop(t, st));
  t();
  assert(count.load() == 6);

  ss.request_stop();
  assert(!queue.try_pop(t, st));

  PASS();
}

void test_priority_stop_drain() {
  TEST("priority stop drain");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;

  queue.push(PriorityEntry{1, [&order] { order.push_back(1); }});
  queue.push(PriorityEntry{3, [&order] { order.push_back(3); }});
  queue.push(PriorityEntry{2, [&order] { order.push_back(2); }});

  // Request stop BEFORE draining: try_pop must still hand out queued
  // items, in priority order, before returning false.
  ss.request_stop();

  concurrency::queues::Task t;
  assert(queue.try_pop(t, st));
  t();
  assert(queue.try_pop(t, st));
  t();
  assert(queue.try_pop(t, st));
  t();

  assert(!queue.try_pop(t, st));
  assert(order == std::vector<int>({3, 2, 1}));

  PASS();
}

void test_priority_empty() {
  TEST("priority empty");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  assert(queue.empty());

  queue.push(PriorityEntry{1, [] {}});
  assert(!queue.empty());

  concurrency::queues::Task t;
  assert(queue.try_pop(t, st));
  assert(queue.empty());

  PASS();
}

void test_priority_many() {
  TEST("priority many");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  constexpr int kN = 500;
  std::vector<int> order;
  order.reserve(kN);

  // Push in reverse so the heap has to do real sift-up work on every push.
  for (int i = kN - 1; i >= 0; --i) {
    queue.push(PriorityEntry{i, [&order, i] { order.push_back(i); }});
  }

  concurrency::queues::Task t;
  for (int i = 0; i < kN; ++i) {
    assert(queue.try_pop(t, st));
    t();
  }

  // Pops must be in strictly descending priority order.
  assert(static_cast<int>(order.size()) == kN);
  for (int i = 0; i + 1 < kN; ++i) {
    assert(order[i] > order[i + 1]);
  }
  assert(order.front() == kN - 1);
  assert(order.back() == 0);

  ss.request_stop();
  assert(!queue.try_pop(t, st));

  PASS();
}

// ─── Loop tests ─────────────────────────────────────────────────────────

void test_loop_round_robin() {
  TEST("loop round robin");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  queue.push([&order] { order.push_back(1); });
  queue.push([&order] { order.push_back(2); });
  queue.push([&order] { order.push_back(3); });

  std::weak_ptr<concurrency::queues::Task> wp;
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 3; ++i) {
      assert(queue.peek(wp, st));
      auto sp = wp.lock();
      assert(sp);
      (*sp)();
    }
  }

  // Cursor advances 0,1,2,0,1,2,0,1,2 → 1,2,3 three times
  assert((order == std::vector<int>{1, 2, 3, 1, 2, 3, 1, 2, 3}));

  ss.request_stop();
  assert(!queue.peek(wp, st));

  PASS();
}

void test_loop_single() {
  TEST("loop single");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};
  queue.push([&count] { count.fetch_add(1); });

  std::weak_ptr<concurrency::queues::Task> wp;
  // The same slot is peeked every time — cursor wraps at size 1.
  for (int i = 0; i < 5; ++i) {
    assert(queue.peek(wp, st));
    auto sp = wp.lock();
    assert(sp);
    (*sp)();
  }
  assert(count.load() == 5);

  ss.request_stop();
  assert(!queue.peek(wp, st));

  PASS();
}

void test_loop_empty() {
  TEST("loop empty");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  assert(queue.empty());

  std::weak_ptr<concurrency::queues::Task> wp;
  // Empty queue + stop requested → false immediately.
  ss.request_stop();
  assert(!queue.peek(wp, st));

  PASS();
}

void test_loop_clear() {
  TEST("loop clear");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  queue.push([] {});
  queue.push([] {});
  queue.push([] {});
  assert(!queue.empty());

  queue.clear();
  assert(queue.empty());

  // Cursor was reset: pushing fresh items yields slot 0 first.
  std::vector<int> order;
  queue.push([&order] { order.push_back(10); });
  queue.push([&order] { order.push_back(20); });

  std::weak_ptr<concurrency::queues::Task> wp;

  // Drain both entries before stopping. Loop's stop is immediate,
  // not drain-on-stop — the stop token only takes effect at the top
  // of peek(), so both peeks must happen first.
  assert(queue.peek(wp, st));
  (*wp.lock())();
  assert(order == std::vector<int>{10});

  assert(queue.peek(wp, st));
  (*wp.lock())();
  assert((order == std::vector<int>{10, 20}));

  // Now stop: peek must return false regardless of remaining entries
  // (there are none here, but the point is that stop short-circuits
  // before any fast-path attempt).
  ss.request_stop();
  assert(!queue.peek(wp, st));

  PASS();
}

void test_loop_remove() {
  TEST("loop remove");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  // Each task writes to its own slot so we can tell which one ran.
  std::array<std::atomic<int>, 3> hits{};
  queue.push([&hits] { hits[0].fetch_add(1); });
  queue.push([&hits] { hits[1].fetch_add(1); });
  queue.push([&hits] { hits[2].fetch_add(1); });

  // Remove index 1 — swap-and-pop moves entry 2 into slot 1.
  assert(queue.remove(1));
  assert(!queue.remove(999));

  // Drain the two remaining entries; task 1 must never have run.
  std::weak_ptr<concurrency::queues::Task> wp;
  for (int i = 0; i < 2; ++i) {
    assert(queue.peek(wp, st));
    (*wp.lock())();
  }
  assert(hits[0].load() == 1);
  assert(hits[1].load() == 0);
  assert(hits[2].load() == 1);

  // After removing all, queue is empty.
  while (!queue.empty()) {
    queue.remove(0);
  }
  assert(queue.empty());

  ss.request_stop();
  assert(!queue.peek(wp, st));

  PASS();
}

void test_loop_stop_drain() {
  TEST("loop stop is immediate");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> hits{0};
  queue.push([&hits] { hits.fetch_add(1); });
  queue.push([&hits] { hits.fetch_add(10); });

  std::weak_ptr<concurrency::queues::Task> wp;

  // Before stop: peek succeeds and hands out tasks.
  assert(queue.peek(wp, st));
  (*wp.lock())();
  assert(hits.load() == 1);

  // Request stop. Entries are still in the queue — Loop never consumes
  // them — but peek must now return false unconditionally.
  ss.request_stop();

  assert(!queue.peek(wp, st));
  assert(!queue.empty());   // entries persist; stop doesn't clear
  assert(hits.load() == 1); // nothing new ran

  PASS();
}

void test_loop_concurrent_distinct() {
  TEST("loop concurrent distinct");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  constexpr int kN = 8;
  std::array<std::atomic<int>, kN> hits{};

  for (int i = 0; i < kN; ++i) {
    queue.push([i, &hits] { hits[i].fetch_add(1); });
  }

  // kN threads each peek exactly once. `index.fetch_add` guarantees they
  // get kN distinct slots modulo queue.size() == kN.
  std::array<std::jthread, kN> threads;
  for (int i = 0; i < kN; ++i) {
    threads[i] = std::jthread([&queue, &st] {
      std::weak_ptr<concurrency::queues::Task> wp;
      assert(queue.peek(wp, st));
      if (auto sp = wp.lock())
        (*sp)();
    });
  }
  for (auto &t : threads)
    t.join();

  for (int i = 0; i < kN; ++i) {
    assert(hits[i].load() == 1);
  }

  ss.request_stop();
  PASS();
}

void test_loop_weak_expires() {
  TEST("loop weak expires after clear");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  queue.push([] {});

  std::weak_ptr<concurrency::queues::Task> wp;
  assert(queue.peek(wp, st));
  assert(!wp.expired());

  // Clear drops the only shared_ptr; the weak ref must expire.
  queue.clear();
  assert(wp.expired());

  assert(queue.empty());
  ss.request_stop();
  assert(!queue.peek(wp, st));

  PASS();
}

int main() {
  std::println("=== Concurrency queues Tests ===");

  static auto tests = [](uint32_t repeats) {
    std::ranges::for_each(std::views::iota(0U, repeats), [](uint32_t) {
      test_fifo();

      test_priority_order();
      test_priority_duplicates();
      test_priority_thread();
      test_priority_stop_drain();
      test_priority_empty();
      test_priority_many();

      test_loop_round_robin();
      test_loop_single();
      test_loop_empty();
      test_loop_clear();
      test_loop_remove();
      test_loop_stop_drain();
      test_loop_concurrent_distinct();
      test_loop_weak_expires();
    });
  };

  static auto ex = [](uint32_t repeats) {
    {
      std::lock_guard lock(log_mutex);
      std::println("Started id: {}", std::this_thread::get_id());
    }
    tests(repeats);
  };

  std::array<std::jthread, 5> threads;

  std::ranges::for_each(threads,
                        [](std::jthread &th) { th = std::jthread(ex, 50); });

  std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });

  ex(50);

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}

static_assert(concurrency::queues::Queue<concurrency::queues::Fifo>);

template <typename E, typename C, typename Cmp>
concept CanInstantiatePriority =
    requires { typename concurrency::queues::Priority<E, C, Cmp>; };

static_assert(
    !CanInstantiatePriority<int, std::vector<int>, std::greater<int>>);
static_assert(CanInstantiatePriority<PriorityEntry, std::vector<PriorityEntry>,
                                     PriorityCompare>);
static_assert(concurrency::queues::Queue<PriorityQueue, PriorityEntry>);

static_assert(concurrency::queues::Queue<concurrency::queues::Loop>);
