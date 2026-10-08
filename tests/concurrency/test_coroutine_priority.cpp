#include <cassert>
import concurrency_helper;

using Task = concurrency::queues::Task;
using Suspend = concurrency::pool::coroutine::policy::Suspend;

template <typename TQ, typename Pushed, Suspend SP, typename T>
using CT = concurrency::pool::coroutine::CoroutineTask<TQ, Pushed, SP, T>;

// ─── Coroutines ─────────────────────────────────────────────────────────

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, void>
sched_probe(concurrency::pool::ThreadPool<TQ, Pushed> &t,
            std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, bool>
sched_returns(concurrency::pool::ThreadPool<TQ, Pushed> &t) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  co_return (!before && concurrency::pool::coroutine::isPoolWorker);
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, void>
nested_outer(concurrency::pool::ThreadPool<TQ, Pushed> &t, bool &flag) {
  bool r = co_await sched_returns(t);
  flag = r;
  co_return;
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, void>
sched_with_priority(concurrency::pool::ThreadPool<TQ, Pushed> &t, bool &flag) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule(5);
  flag = (!before && concurrency::pool::coroutine::isPoolWorker);
}

// ─── Tests ──────────────────────────────────────────────────────────────

template <typename TQ, typename Pushed> void run_basic() {
  TEST("priority coroutine basic");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  std::optional<bool> result;
  auto task = sched_probe<TQ, Pushed>(t, result);
  task.get();
  assert(result.has_value() && *result);
  PASS();
}

template <typename TQ, typename Pushed> void run_value() {
  TEST("priority coroutine returns value");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  assert((sched_returns<TQ, Pushed>(t).get()));
  PASS();
}

template <typename TQ, typename Pushed> void run_nested() {
  TEST("priority coroutine nested");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  bool flag = false;
  auto task = nested_outer<TQ, Pushed>(t, flag);
  task.get();
  assert(flag);
  PASS();
}

template <typename TQ, typename Pushed> void run_priority_arg() {
  TEST("priority coroutine with schedule(5)");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  bool flag = false;
  auto task = sched_with_priority<TQ, Pushed>(t, flag);
  task.get();
  assert(flag);
  PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, void>
sched_with_payload(concurrency::pool::ThreadPool<TQ, Pushed> &t, bool &flag) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule(5, 10); // priority=5, data=10
  flag = (!before && concurrency::pool::coroutine::isPoolWorker);
}

template <typename TQ, typename Pushed> void run_payload_schedule() {
  TEST("coroutine with payload schedule");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  bool flag = false;
  auto task = sched_with_payload<TQ, Pushed>(t, flag);
  task.get();
  assert(flag);
  PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, void>
sched_with_inherited_payload(concurrency::pool::ThreadPool<TQ, Pushed> &t,
                             bool &flag) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule(); // no args → uses inherited payload
  flag = (!before && concurrency::pool::coroutine::isPoolWorker);
}

template <typename TQ, typename Pushed> void run_with_payload() {
  TEST("coroutine with_payload");
  concurrency::pool::ThreadPool<TQ, Pushed> t(2);
  bool flag = false;
  auto task = sched_with_inherited_payload<TQ, Pushed>(t, flag);
  task.with_payload(typename Pushed::Payload{5, 10});
  task.get();
  assert(flag);
  PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
CT<TQ, Pushed, Suspend::Always, void>
coro_with_aging(concurrency::pool::ThreadPool<TQ, Pushed> &t, int priority,
                int64_t arrival, std::vector<int> &order, int id) {
  co_await t.schedule(priority, arrival);
  order.push_back(id);
}

void test_coroutine_aging_reprioritize_all() {
  TEST("coroutine aging reprioritize_all");

  // Single-threaded pool so we can control execution order.
  concurrency::pool::ThreadPool<TimedPriorityQueue, TimedEntry> t(1);

  std::atomic<bool> gate_entered{false};
  std::atomic<bool> release{false};

  // Gate task with very high priority – blocks the only worker.
  t.submit_detach(
      [&] {
        gate_entered = true;
        while (!release.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
      },
      1000, 0); // priority=1000, arrival=0

  while (!gate_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  std::vector<int> order;

  // Schedule three coroutines with different priorities and arrival
  // times. They will be enqueued behind the gate.
  g_aging_now = 0;
  auto t1 = coro_with_aging(t, 1, 0, order, 1);
  g_aging_now = 10;
  auto t2 = coro_with_aging(t, 10, 10, order, 2);
  g_aging_now = 20;
  auto t3 = coro_with_aging(t, 5, 20, order, 3);

  // Start them so they suspend on t.schedule() and land in the queue.
  t1.start();
  t2.start();
  t3.start();

  // Advance time and re-age everything in the queue.
  g_aging_now = 300;
  t.queue()->reprioritize_all();

  // Release the gate; the worker will now process the coroutines.
  release.store(true, std::memory_order_release);

  // Block until each coroutine has finished. `CoroutineTask::get()`
  // waits on the state's `executed` flag (acquire), which gives us a
  // proper happens-before for everything the coroutine wrote —
  // including `order.push_back(id)`.
  //
  // Do NOT rely on t.wait() here: only `submit_detach` bumps
  // active_tasks_, so t.wait() returns as soon as the *gate* is done,
  // while the coroutines may still be queued.
  t1.get();
  t2.get();
  t3.get();

  // Expected order after aging:
  //   id 1: priority 1 + (300-0)/10  = 31
  //   id 3: priority 5 + (300-20)/10 = 33
  //   id 2: priority 10 + (300-10)/10 = 39  → highest
  assert(order.size() == 3);
  assert(order[0] == 2);
  assert(order[1] == 3);
  assert(order[2] == 1);
  PASS();
}

template <typename TQ, typename Pushed> static void suite() {
  run_basic<TQ, Pushed>();
  run_value<TQ, Pushed>();
  run_nested<TQ, Pushed>();
  run_priority_arg<TQ, Pushed>();

  if constexpr (concurrency::queues::PayloadEntry<TQ, Pushed>) {
    run_payload_schedule<TQ, Pushed>();
    run_with_payload<TQ, Pushed>();
  }
}

template <typename TQ, typename Pushed> static void ex(uint32_t repeats) {
  std::ranges::for_each(std::views::iota(0U, repeats),
                        [](uint32_t) { suite<TQ, Pushed>(); });
}

int main() {
  std::println("=== Coroutine + Priority queue tests ===");

  std::array<std::jthread, 4> threads;
  threads[0] = std::jthread(ex<PriorityQueue, PriorityEntry>, 25);
  threads[1] = std::jthread(ex<PriorityQueue, PriorityEntry>, 25);
  threads[2] = std::jthread(ex<PriorityQueue, PriorityEntry>, 25);
  threads[3] = std::jthread(ex<PriorityQueue, PriorityEntry>, 25);
  for (auto &t : threads)
    t.join();

  ex<PriorityQueue, PriorityEntry>(25);

  // Payload tests
  ex<PayloadPriorityQueue, PayloadPriorityEntry>(25);

  // Aging on coroutines (sequential, uses global time)
  test_coroutine_aging_reprioritize_all();

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
