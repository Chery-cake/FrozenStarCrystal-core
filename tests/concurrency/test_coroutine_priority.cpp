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

template <typename TQ, typename Pushed> static void suite() {
  run_basic<TQ, Pushed>();
  run_value<TQ, Pushed>();
  run_nested<TQ, Pushed>();
  run_priority_arg<TQ, Pushed>();
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

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
