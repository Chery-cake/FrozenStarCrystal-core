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

// ─── Runtime tests ──────────────────────────────────────────────────────

void test_priority_order() {
  TEST("priority order");

  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;

  // Pushed out of order on purpose.
  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});
  queue.push(PriorityEntry{[&order] { order.push_back(5); }, 5});
  queue.push(PriorityEntry{[&order] { order.push_back(3); }, 3});
  queue.push(PriorityEntry{[&order] { order.push_back(7); }, 7});

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
  queue.push(PriorityEntry{[&count] { count.fetch_add(1); }, 5});
  queue.push(PriorityEntry{[&count] { count.fetch_add(2); }, 5});
  queue.push(PriorityEntry{[&count] { count.fetch_add(4); }, 5});
  queue.push(PriorityEntry{[&count] { count.fetch_add(100); }, 1});

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

  queue.push(PriorityEntry{[&count] { count.fetch_add(1); }, 1});
  queue.push(PriorityEntry{[&count] { count.fetch_add(2); }, 2});
  queue.push(PriorityEntry{[&count] { count.fetch_add(3); }, 3});

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

  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});
  queue.push(PriorityEntry{[&order] { order.push_back(3); }, 3});
  queue.push(PriorityEntry{[&order] { order.push_back(2); }, 2});

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

  queue.push(PriorityEntry{[] {}, 1});
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
    queue.push(PriorityEntry{[&order, i] { order.push_back(i); }, i});
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

  std::shared_ptr<concurrency::queues::Task> sp;
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 3; ++i) {
      assert(queue.peek(sp, st));
      assert(sp);
      (*sp)();
    }
  }

  // Cursor advances 0,1,2,0,1,2,0,1,2 → 1,2,3 three times
  assert((order == std::vector<int>{1, 2, 3, 1, 2, 3, 1, 2, 3}));

  ss.request_stop();
  assert(!queue.peek(sp, st));

  PASS();
}

void test_loop_single() {
  TEST("loop single");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};
  queue.push([&count] { count.fetch_add(1); });

  std::shared_ptr<concurrency::queues::Task> sp;
  // The same slot is peeked every time — cursor wraps at size 1.
  for (int i = 0; i < 5; ++i) {
    assert(queue.peek(sp, st));
    assert(sp);
    (*sp)();
  }
  assert(count.load() == 5);

  ss.request_stop();
  assert(!queue.peek(sp, st));

  PASS();
}

void test_loop_empty() {
  TEST("loop empty");

  concurrency::queues::Loop queue;
  std::stop_source ss;
  auto st = ss.get_token();

  assert(queue.empty());

  std::shared_ptr<concurrency::queues::Task> sp;
  // Empty queue + stop requested → false immediately.
  ss.request_stop();
  assert(!queue.peek(sp, st));

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

  std::shared_ptr<concurrency::queues::Task> sp;

  // Drain both entries before stopping. Loop's stop is immediate,
  // not drain-on-stop — the stop token only takes effect at the top
  // of peek(), so both peeks must happen first.
  assert(queue.peek(sp, st));
  (*sp)();
  assert(order == std::vector<int>{10});

  assert(queue.peek(sp, st));
  (*sp)();
  assert((order == std::vector<int>{10, 20}));

  // Now stop: peek must return false regardless of remaining entries
  // (there are none here, but the point is that stop short-circuits
  // before any fast-path attempt).
  ss.request_stop();
  assert(!queue.peek(sp, st));

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
  std::shared_ptr<concurrency::queues::Task> sp;
  for (int i = 0; i < 2; ++i) {
    assert(queue.peek(sp, st));
    (*sp)();
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
  assert(!queue.peek(sp, st));

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

  std::shared_ptr<concurrency::queues::Task> sp;

  // Before stop: peek succeeds and hands out tasks.
  assert(queue.peek(sp, st));
  (*sp)();
  assert(hits.load() == 1);

  // Request stop. Entries are still in the queue — Loop never consumes
  // them — but peek must now return false unconditionally.
  ss.request_stop();

  assert(!queue.peek(sp, st));
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
      std::shared_ptr<concurrency::queues::Task> sp;
      assert(queue.peek(sp, st));
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

// ─── Aging tests ───────────────────────────────────────────────────────

void test_aging_disabled_matches_pure_priority() {
  TEST("aging disabled matches pure priority");

  // PriorityQueue uses the default NoAging. Regression guard: adding
  // aging support must not change the pop order of the default queue.
  PriorityQueue queue;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});
  queue.push(PriorityEntry{[&order] { order.push_back(5); }, 5});
  queue.push(PriorityEntry{[&order] { order.push_back(3); }, 3});
  queue.push(PriorityEntry{[&order] { order.push_back(7); }, 7});

  concurrency::queues::Task t;
  for (int i = 0; i < 4; ++i) {
    assert(queue.try_pop(t, st));
    t();
  }
  assert(order == std::vector<int>({7, 5, 3, 1}));

  DisabledQueue queue2;
  std::stop_source ss2;
  auto st2 = ss2.get_token();

  std::vector<int> order2;
  queue2.push(PriorityEntry{[&order2] { order2.push_back(1); }, 1});
  queue2.push(PriorityEntry{[&order2] { order2.push_back(5); }, 5});
  queue2.push(PriorityEntry{[&order2] { order2.push_back(3); }, 3});
  queue2.push(PriorityEntry{[&order2] { order2.push_back(7); }, 7});

  concurrency::queues::Task t2;
  for (int i = 0; i < 4; ++i) {
    assert(queue2.try_pop(t2, st2));
    t2();
  }
  assert(order2 == std::vector<int>({7, 5, 3, 1}));
  PASS();
}

void test_aging_old_low_outranks_new_high() {
  TEST("aging: old low-priority outranks new high-priority");

  int64_t now = 0;
  AgedPriorityQueue queue{PriorityCompare{}, TestAging{now}};
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});
  now = 100;
  queue.push(PriorityEntry{[&order] { order.push_back(10); }, 10});

  concurrency::queues::Task t;
  assert(queue.try_pop(t, st));
  t();
  assert(queue.try_pop(t, st));
  t();
  assert(order == std::vector<int>({1, 10}));
  PASS();
}

void test_aging_recent_high_outranks_old_low() {
  TEST("aging: recent high-priority outranks old low-priority");

  int64_t now = 0;
  AgedPriorityQueue queue{PriorityCompare{}, TestAging{now}};
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});
  now = 2;
  queue.push(PriorityEntry{[&order] { order.push_back(10); }, 10});

  concurrency::queues::Task t;
  assert(queue.try_pop(t, st));
  t();
  assert(queue.try_pop(t, st));
  t();
  assert(order == std::vector<int>({10, 1}));
  PASS();
}

void test_aging_equal_deadline_falls_back_to_priority() {
  TEST("aging: equal deadline falls back to priority");

  int64_t now = 0;
  AgedPriorityQueue queue{PriorityCompare{}, TestAging{now}};
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});
  now = 9;
  queue.push(PriorityEntry{[&order] { order.push_back(10); }, 10});

  concurrency::queues::Task t;
  assert(queue.try_pop(t, st));
  t();
  assert(queue.try_pop(t, st));
  t();
  assert(order == std::vector<int>({10, 1}));
  PASS();
}

void test_aging_full_order() {
  TEST("aging: pop order equals deadline sort");

  int64_t now = 0;
  AgedPriorityQueue queue{PriorityCompare{}, TestAging{now}};
  std::stop_source ss;
  auto st = ss.get_token();

  struct Rec {
    int priority;
    int64_t deadline;
    int id;
  };
  std::vector<Rec> expected;
  std::vector<int> order;

  for (int i = 0; i < 30; ++i) {
    const int p = (i * 7 % 10) + 1;
    expected.push_back({p, now + (11 - p), i});
    now += 2;
    queue.push(PriorityEntry{[&order, i] { order.push_back(i); }, p});
  }

  std::sort(expected.begin(), expected.end(), [](const Rec &a, const Rec &b) {
    if (a.deadline != b.deadline)
      return a.deadline < b.deadline;
    return a.priority > b.priority;
  });

  concurrency::queues::Task t;
  for (size_t i = 0; i < expected.size(); ++i) {
    assert(queue.try_pop(t, st));
    t();
    assert(order[i] == expected[i].id);
  }
  PASS();
}

void test_aging_duplicates_and_stop_drain() {
  TEST("aging: stop drain still hands out aged order");

  int64_t now = 0;
  AgedPriorityQueue queue{PriorityCompare{}, TestAging{now}};
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  queue.push(PriorityEntry{[&order] { order.push_back(10); }, 10});
  now = 5;
  queue.push(PriorityEntry{[&order] { order.push_back(5); }, 5});
  now = 10;
  queue.push(PriorityEntry{[&order] { order.push_back(1); }, 1});

  ss.request_stop();

  concurrency::queues::Task t;
  for (int i = 0; i < 3; ++i) {
    assert(queue.try_pop(t, st));
    t();
  }
  assert(!queue.try_pop(t, st));
  assert(order == std::vector<int>({10, 5, 1}));
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

      test_aging_disabled_matches_pure_priority();
      test_aging_old_low_outranks_new_high();
      test_aging_recent_high_outranks_old_low();
      test_aging_equal_deadline_falls_back_to_priority();
      test_aging_full_order();
      test_aging_duplicates_and_stop_drain();

      test_loop_round_robin();
      test_loop_single();
      test_loop_empty();
      test_loop_clear();
      test_loop_remove();
      test_loop_stop_drain();
      test_loop_concurrent_distinct();
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

template <typename E, template <typename> typename C, typename Cmp>
concept CanInstantiatePriority =
    requires { typename concurrency::queues::Priority<E, C, Cmp>; };

static_assert(!CanInstantiatePriority<int, std::vector, std::greater<int>>);
static_assert(
    CanInstantiatePriority<PriorityEntry, std::vector, PriorityCompare>);
static_assert(concurrency::queues::Queue<PriorityQueue, PriorityEntry>);

static_assert(concurrency::queues::Queue<concurrency::queues::Loop>);
