#include <cassert>
import concurrency_helper;

// ─── Loop<Task>: round-robin, plain pool ─────────────────────────────

void test_loop_round_robin() {
  ct::TEST("loop<Task>: round robin");
  concurrency::queues::Loop<ct::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::vector<int> order;
  q.push([&] { order.push_back(1); });
  q.push([&] { order.push_back(2); });
  q.push([&] { order.push_back(3); });

  for (int round = 0; round < 3; ++round)
    for (int i = 0; i < 3; ++i) {
      std::shared_ptr<ct::Task> sp;
      assert(q.peek(sp, st));
      (*sp)();
      q.release(*sp);
    }

  assert((order == std::vector<int>{1, 2, 3, 1, 2, 3, 1, 2, 3}));
  ss.request_stop();
  ct::PASS();
}

void test_loop_single() {
  ct::TEST("loop<Task>: single");
  concurrency::queues::Loop<ct::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};
  q.push([&] { count.fetch_add(1); });

  for (int i = 0; i < 5; ++i) {
    std::shared_ptr<ct::Task> sp;
    assert(q.peek(sp, st));
    (*sp)();
    q.release(*sp);
  }
  assert(count.load() == 5);
  ss.request_stop();
  ct::PASS();
}

void test_loop_clear() {
  ct::TEST("loop<Task>: clear");
  concurrency::queues::Loop<ct::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  q.push([] {});
  q.push([] {});
  q.push([] {});
  assert(!q.empty());
  q.clear();
  assert(q.empty());

  std::vector<int> order;
  q.push([&] { order.push_back(10); });
  q.push([&] { order.push_back(20); });

  for (int i = 0; i < 2; ++i) {
    std::shared_ptr<ct::Task> sp;
    assert(q.peek(sp, st));
    (*sp)();
    q.release(*sp);
  }
  assert((order == std::vector<int>{10, 20}));
  ss.request_stop();
  ct::PASS();
}

void test_loop_remove() {
  ct::TEST("loop<Task>: remove");
  concurrency::queues::Loop<ct::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::array<std::atomic<int>, 3> hits{};
  q.push([&] { hits[0].fetch_add(1); });
  q.push([&] { hits[1].fetch_add(1); });
  q.push([&] { hits[2].fetch_add(1); });

  assert(q.remove(1));
  assert(!q.remove(999));

  for (int i = 0; i < 2; ++i) {
    std::shared_ptr<ct::Task> sp;
    assert(q.peek(sp, st));
    (*sp)();
    q.release(*sp);
  }
  assert(hits[0].load() == 1);
  assert(hits[1].load() == 0);
  assert(hits[2].load() == 1);

  ss.request_stop();
  ct::PASS();
}

void test_loop_stop_is_immediate() {
  ct::TEST("loop<Task>: stop is immediate");
  concurrency::queues::Loop<ct::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> hits{0};
  q.push([&] { hits.fetch_add(1); });
  q.push([&] { hits.fetch_add(10); });

  std::shared_ptr<ct::Task> sp;
  assert(q.peek(sp, st));
  (*sp)();
  assert(hits.load() == 1);

  ss.request_stop();
  assert(!q.peek(sp, st));
  assert(!q.empty()); // stop doesn't clear the queue
  assert(hits.load() == 1);
  ct::PASS();
}

void test_loop_pool_repeats() {
  ct::TEST("loop<Task>: pool repeats");
  concurrency::pool::ThreadPool<ct::LoopTask, concurrency::queues::Task> t(1);
  auto count = std::make_shared<std::atomic<int>>(0);
  t.submit_detach([count] { count->fetch_add(1, std::memory_order_relaxed); });

  for (int i = 0; i < 100 && count->load() < 5; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(count->load() >= 5);

  t.queue()->clear();
  ct::PASS();
}

// ─── Loop<OneShotLoopEntry>: retires after one call, coroutine-capable ─

void test_loop_oneshot_alone() {
  ct::TEST("loop<OneShot>: standalone");
  concurrency::queues::Loop<ct::OneShotLoopEntry> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> hits{0};
  q.push_transient([&] { hits.fetch_add(1); });

  std::shared_ptr<ct::OneShotLoopEntry> e;
  assert(q.peek(e, st));
  concurrency::queues::invoke_entry(*e);
  assert(hits == 1);
  assert(q.empty());
  ct::PASS();
}

void test_loop_oneshot_coroutines() {
  ct::coro::run_all_policies<ct::LoopOneShot, ct::OneShotLoopEntry>(
      "loop<OneShot>: coroutine");
}

// ─── Loop<PayloadLoopEntry>: payload + retire ────────────────────────

void test_loop_payload_coroutines() {
  ct::coro::run_all_policies<ct::LoopPayload, ct::PayloadLoopEntry>(
      "loop<Payload>: coroutine");
}

int main() {
  std::println("=== Loop tests ===");

  test_loop_round_robin();
  test_loop_single();
  test_loop_clear();
  test_loop_remove();
  test_loop_stop_is_immediate();
  test_loop_oneshot_alone();

  std::array<std::jthread, 4> threads;
  for (auto &th : threads) {
    th = std::jthread([] {
      test_loop_pool_repeats();
      test_loop_oneshot_coroutines();
      test_loop_payload_coroutines();
    });
  }
  for (auto &th : threads)
    th.join();

  test_loop_pool_repeats();
  test_loop_oneshot_coroutines();
  test_loop_payload_coroutines();

  std::println("\n{}/{} passed", ct::tests_passed.load(), ct::tests_run.load());
  return ct::tests_passed == ct::tests_run ? 0 : 1;
}
