#include <cassert>
import concurrency_helper;

// ─── Coroutine used by the aging test ────────────────────────────────

concurrency::pool::coroutine::CoroutineTask<
    ct::TimedPriorityQueue, ct::TimedEntry,
    concurrency::pool::coroutine::policy::Suspend::Always, void>
coro_aging(
    concurrency::pool::ThreadPool<ct::TimedPriorityQueue, ct::TimedEntry> &t,
    int priority, int64_t arrival, std::vector<int> &order, int id) {
  co_await t.schedule(priority, arrival);
  order.push_back(id);
}

// ─── 1. Ordering / heap invariants ───────────────────────────────────

void test_priority_order() {
  ct::TEST("priority order");
  ct::BasicPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  q.push(ct::PriorityEntry{[&] { order.push_back(1); }, 1});
  q.push(ct::PriorityEntry{[&] { order.push_back(5); }, 5});
  q.push(ct::PriorityEntry{[&] { order.push_back(3); }, 3});
  q.push(ct::PriorityEntry{[&] { order.push_back(7); }, 7});

  concurrency::queues::Task t;
  for (int i = 0; i < 4; ++i) {
    assert(q.try_pop(t, st));
    t();
  }
  assert((order == std::vector<int>{7, 5, 3, 1}));

  ss.request_stop();
  assert(!q.try_pop(t, st));
  ct::PASS();
}

void test_priority_many() {
  ct::TEST("priority many");
  ct::BasicPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  constexpr int kN = 500;
  std::vector<int> order;
  order.reserve(kN);
  for (int i = kN - 1; i >= 0; --i) {
    q.push(ct::PriorityEntry{[&order, i] { order.push_back(i); }, i});
  }

  concurrency::queues::Task t;
  for (int i = 0; i < kN; ++i) {
    assert(q.try_pop(t, st));
    t();
  }

  assert(static_cast<int>(order.size()) == kN);
  for (int i = 0; i + 1 < kN; ++i) {
    assert(order[i] > order[i + 1]);
  }
  assert(order.front() == kN - 1);
  assert(order.back() == 0);
  ct::PASS();
}

// ─── 2. Score variations ─────────────────────────────────────────────

void test_priority_custom_class() {
  ct::TEST("priority custom class");
  ct::CustomPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  q.push(ct::CustomEntry{[&] { order.push_back(1); }, {1}});
  q.push(ct::CustomEntry{[&] { order.push_back(5); }, {5}});
  q.push(ct::CustomEntry{[&] { order.push_back(3); }, {3}});

  concurrency::queues::Task t;
  for (int i = 0; i < 3; ++i) {
    assert(q.try_pop(t, st));
    t();
  }
  assert((order == std::vector<int>{5, 3, 1}));
  ct::PASS();
}

void test_priority_computed_score() {
  ct::TEST("priority computed score");
  ct::ComputedPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  q.push(ct::ComputedEntry{[&] { order.push_back(1); }, 1, 0});  // 1
  q.push(ct::ComputedEntry{[&] { order.push_back(2); }, 0, 5});  // 5
  q.push(ct::ComputedEntry{[&] { order.push_back(3); }, 1, 5});  // 6
  q.push(ct::ComputedEntry{[&] { order.push_back(4); }, 10, 0}); // 10

  concurrency::queues::Task t;
  for (int i = 0; i < 4; ++i) {
    assert(q.try_pop(t, st));
    t();
  }
  assert((order == std::vector<int>{4, 3, 2, 1}));
  ct::PASS();
}

// ─── 3. Aging ────────────────────────────────────────────────────────

void test_priority_deadline_aging() {
  ct::TEST("priority deadline aging");
  int64_t now = 0;
  ct::DeadlinePriorityQueue q{ct::DeadlineAging{now}};
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;

  // Pushed at now=0: deadline = 0 + 10 = 10, score = (-10, 1)
  q.push(ct::PriorityEntry{[&] { order.push_back(1); }, 1});

  // Pushed at now=100: deadline = 100 + 1 = 101, score = (-101, 10)
  now = 100;
  q.push(ct::PriorityEntry{[&] { order.push_back(10); }, 10});

  // (-10, 1) > (-101, 10) → the older, low-priority entry wins.
  concurrency::queues::Task t;
  assert(q.try_pop(t, st));
  t();
  assert(q.try_pop(t, st));
  t();
  assert((order == std::vector<int>{1, 10}));
  ct::PASS();
}

void test_priority_timed_aging() {
  ct::TEST("priority timed aging");
  ct::g_aging_now = 0;
  ct::TimedPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;

  // Push-time scores: entry1 = 1, entry10 = 10 → entry10 currently on top.
  q.push(ct::TimedEntry{[&] { order.push_back(1); }, 1, 0});
  ct::g_aging_now = 100;
  q.push(ct::TimedEntry{[&] { order.push_back(10); }, 10, 100});

  // Advance time and re-score everything. Aging flips the order:
  //   entry1 : 1 + 300/10 = 31
  //   entry10: 10 + 200/10 = 30
  ct::g_aging_now = 300;
  q.reprioritize_all();

  concurrency::queues::Task t;
  assert(q.try_pop(t, st));
  t();
  assert(q.try_pop(t, st));
  t();
  assert((order == std::vector<int>{1, 10}));
  ct::PASS();
}

void test_priority_reprioritize_single() {
  ct::TEST("priority reprioritize single");
  ct::g_aging_now = 0;
  ct::TimedPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;

  auto h1 = q.push(ct::TimedEntry{[&] { order.push_back(1); }, 1, 0});
  ct::g_aging_now = 100;
  q.push(ct::TimedEntry{[&] { order.push_back(10); }, 10, 100});

  // Only h1 gets re-scored:
  //   h1     : 1 + 200/10 = 21
  //   other  : still 10
  ct::g_aging_now = 200;
  assert(q.reprioritize(h1));

  concurrency::queues::Task t;
  assert(q.try_pop(t, st));
  t();
  assert(q.try_pop(t, st));
  t();
  assert((order == std::vector<int>{1, 10}));
  ct::PASS();
}

// ─── 4. Payload queue, standalone ────────────────────────────────────

void test_payload_priority_order() {
  ct::TEST("payload priority order");
  ct::PayloadPriorityQueue q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  q.push(ct::PayloadPriorityEntry{[&] { order.push_back(1); }, {1, 100}});
  q.push(ct::PayloadPriorityEntry{[&] { order.push_back(5); }, {5, 200}});
  q.push(ct::PayloadPriorityEntry{[&] { order.push_back(3); }, {3, 300}});

  concurrency::queues::Task t;
  for (int i = 0; i < 3; ++i) {
    assert(q.try_pop(t, st));
    t();
  }
  assert((order == std::vector<int>{5, 3, 1}));
  ct::PASS();
}

void test_payload_priority_make() {
  ct::TEST("payload priority make");
  ct::PayloadPriorityQueue q;

  auto e = q.make([] {}, ct::PriorityPayload{7, 42});
  assert(e.priority() == 7);
  assert(e.payload().data == 42);
  ct::PASS();
}

// ─── 5. Pool + priority ──────────────────────────────────────────────

void test_priority_pool_order() {
  ct::TEST("priority pool order");
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
  for (int p : {1, 5, 3, 8}) {
    t.submit_detach(
        [&, p] {
          std::lock_guard lk(mtx);
          order.push_back(p);
        },
        p);
  }

  release.store(true, std::memory_order_release);
  t.wait();

  assert(order.size() == 4);
  assert(order.front() == 8);
  assert(order.back() == 1);
  ct::PASS();
}

// ─── 6. Coroutine matrix — all 4 policies, both entries ──────────────

void test_priority_coroutines_all_policies() {
  ct::coro::run_all_policies<ct::BasicPriorityQueue, ct::PriorityEntry>(
      "priority: coroutine");
  ct::coro::run_all_policies<ct::PayloadPriorityQueue,
                             ct::PayloadPriorityEntry>(
      "priority-payload: coroutine");
}

// ─── 7. Coroutine aging ──────────────────────────────────────────────

void test_priority_coroutine_aging() {
  ct::TEST("priority coroutine aging");
  ct::g_aging_now = 0;
  concurrency::pool::ThreadPool<ct::TimedPriorityQueue, ct::TimedEntry> t(1);

  // Block the single worker.
  std::atomic<bool> gate_entered{false}, release{false};
  t.submit_detach(
      [&] {
        gate_entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
      },
      1000, 0);
  while (!gate_entered.load(std::memory_order_acquire))
    std::this_thread::yield();

  std::vector<int> order;

  // Construct the coroutines first; they don't run yet.
  auto c1 = coro_aging(t, /*priority=*/1, /*arrival=*/0, order, /*id=*/1);
  auto c2 = coro_aging(t, /*priority=*/10, /*arrival=*/100, order, /*id=*/10);

  // Now start them at controlled times so their push-time scores differ.
  ct::g_aging_now = 0;
  c1.start(); // enqueues with score 1
  ct::g_aging_now = 100;
  c2.start(); // enqueues with score 10

  // Advance the clock and re-score the whole queue.
  //   c1: 1 + 300/10 = 31
  //   c2: 10 + 200/10 = 30
  ct::g_aging_now = 300;
  t.queue()->reprioritize_all();

  // Release the worker; it pops the highest-scoring task each time.
  release.store(true, std::memory_order_release);

  c1.get();
  c2.get();

  assert(order.size() == 2);
  assert(order[0] == 1);
  assert(order[1] == 10);
  ct::PASS();
}

// ─── Driver ──────────────────────────────────────────────────────────

int main() {
  std::println("=== Priority tests ===");

  // Sequential tests (aging uses a global clock).
  test_priority_order();
  test_priority_many();
  test_priority_custom_class();
  test_priority_computed_score();
  test_priority_deadline_aging();
  test_priority_timed_aging();
  test_priority_reprioritize_single();
  test_payload_priority_order();
  test_payload_priority_make();
  test_priority_coroutine_aging();

  // Pool/coroutine tests are independent.
  std::array<std::jthread, 4> threads;
  for (auto &th : threads) {
    th = std::jthread([] {
      test_priority_pool_order();
      test_priority_coroutines_all_policies();
    });
  }
  for (auto &th : threads)
    th.join();

  test_priority_pool_order();
  test_priority_coroutines_all_policies();

  std::println("\n{}/{} passed", ct::tests_passed.load(), ct::tests_run.load());
  return ct::tests_passed == ct::tests_run ? 0 : 1;
}
