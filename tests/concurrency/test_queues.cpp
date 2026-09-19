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
static_assert(concurrency::queues::Queue<PriorityQueue, PriorityEntry>);
