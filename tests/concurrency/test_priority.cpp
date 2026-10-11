#include <cassert>
import concurrency_helper;

// 1. Ordering / heap invariants on BasicPriorityQueue
void test_priority_order() { /* 7,5,3,1; duplicates; stop-drain */ }
void test_priority_many() { /* 500 reverse-pushed, strictly descending */ }

// 2. Score variations
void test_priority_custom_class() { /* CustomPriorityQueue */ }
void test_priority_computed_score() { /* entry with priority() method */ }

// 3. Aging
void test_priority_deadline_aging() {
  // DeadlinePriorityQueue with a mutable int64_t now
}
void test_priority_timed_aging() {
  // TimedPriorityQueue + reprioritize_all / reprioritize(handle)
}

// 4. Payload queue, standalone
void test_payload_priority_order() { /* PayloadPriorityQueue, no pool */ }
void test_payload_priority_make() { /* queue.make(t, p) */ }

// 5. Pool + priority
void test_priority_pool_order() { /* ThreadPool + submit_detach with priority
                                     args */
}

// 6. Coroutine matrix — all 4 policies, both entries
void test_priority_coroutines_all_policies() {
  ct::coro::run_all_policies<ct::BasicPriorityQueue, ct::PriorityEntry>(
      "priority: coroutine");
  ct::coro::run_all_policies<ct::PayloadPriorityQueue,
                             ct::PayloadPriorityEntry>(
      "priority-payload: coroutine");
}

// 7. Coroutine aging — a single-threaded test that gates the worker,
//    schedules coroutines with different ages, reprioritizes, releases,
//    and checks execution order.
void test_priority_coroutine_aging() { /* adapted from the current file */ }

int main() {
  std::println("=== Priority tests ===");

  // Standalone tests are sequential (aging uses a global clock).
  test_priority_order();
  test_priority_many();
  test_priority_custom_class();
  test_priority_computed_score();
  test_priority_deadline_aging();
  test_priority_timed_aging();
  test_payload_priority_order();
  test_payload_priority_make();
  test_priority_coroutine_aging();

  // Pool/coroutine tests are independent, so parallelize them.
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
