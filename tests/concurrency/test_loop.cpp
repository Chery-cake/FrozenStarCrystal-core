#include <cassert>
import concurrency_helper;

// ─── Loop<Task>: round-robin, plain pool ─────────────────────────────

void test_loop_round_robin() { /* cursor wraps, same entry re-peeked */ }
void test_loop_single() { /* size 1, cursor pin */ }
void test_loop_clear() { /* clear resets cursor */ }
void test_loop_remove() { /* swap-and-pop */ }
void test_loop_stop_is_immediate() { /* stop doesn't drain */ }

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
