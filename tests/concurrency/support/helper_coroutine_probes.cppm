module;

#include <cassert>

export module concurrency_test.coroutine_probes;

export import std.compat;
export import concurrency;
export import concurrency_test.framework;

export namespace ct::coro {

namespace cr = concurrency::pool::coroutine;
using concurrency::queues::PayloadEntryFrom;
using concurrency::queues::SchedulableQueue;
using concurrency::queues::Task;
using cr::policy::Queue;
using cr::policy::Suspend;

template <typename TQ, typename Pushed, Queue QP, Suspend SP>
  requires(SchedulableQueue<TQ, Pushed>)
auto probe_void(concurrency::pool::ThreadPool<TQ, Pushed> &t,
                std::optional<bool> &result)
    -> cr::CoroutineTask<TQ, Pushed, SP, void> {
  const bool before = cr::isPoolWorker;
  co_await t.template schedule<QP>();
  result = (!before && cr::isPoolWorker);
}

template <typename TQ, typename Pushed, Queue QP, Suspend SP>
  requires(SchedulableQueue<TQ, Pushed>)
auto probe_bool(concurrency::pool::ThreadPool<TQ, Pushed> &t)
    -> cr::CoroutineTask<TQ, Pushed, SP, bool> {
  const bool before = cr::isPoolWorker;
  co_await t.template schedule<QP>();
  co_return (!before && cr::isPoolWorker);
}

template <typename TQ, typename Pushed, Queue QP, Suspend SP>
  requires(SchedulableQueue<TQ, Pushed>)
auto nested_void(concurrency::pool::ThreadPool<TQ, Pushed> &t, bool &flag)
    -> cr::CoroutineTask<TQ, Pushed, SP, void> {
  flag = co_await probe_bool<TQ, Pushed, QP, SP>(t);
}

template <typename TQ, typename Pushed, Queue QP, Suspend SP>
  requires(SchedulableQueue<TQ, Pushed>)
auto chain(concurrency::pool::ThreadPool<TQ, Pushed> &t, int depth,
           int &counter) -> cr::CoroutineTask<TQ, Pushed, SP, void> {
  if (depth > 0) {
    co_await t.template schedule<QP>();
    co_await chain<TQ, Pushed, QP, SP>(t, depth - 1, counter);
    counter++;
  }
}

template <typename TQ, typename Pushed, Queue QP, Suspend SP>
  requires(SchedulableQueue<TQ, Pushed>)
auto external(concurrency::pool::ThreadPool<TQ, Pushed> &t)
    -> cr::CoroutineTask<TQ, Pushed, SP, bool> {
  co_await t.template schedule<QP>();
  co_await ExternalEventAwaiter{};
  co_return true;
}

template <typename TQ, typename Pushed, Queue QP, Suspend SP, typename... Args>
  requires(SchedulableQueue<TQ, Pushed> &&
           PayloadEntryFrom<TQ, Pushed, std::decay_t<Args>...>)
auto probe_with_payload(concurrency::pool::ThreadPool<TQ, Pushed> &t,
                        bool &flag, Args &&...args)
    -> cr::CoroutineTask<TQ, Pushed, SP, void> {
  const bool before = cr::isPoolWorker;
  co_await t.template schedule<QP>(std::forward<Args>(args)...);
  flag = (!before && cr::isPoolWorker);
}

template <typename TQ, typename Pushed, Suspend SP>
  requires(SchedulableQueue<TQ, Pushed>)
auto probe_initial_suspend(bool &flag)
    -> cr::CoroutineTask<TQ, Pushed, SP, void> {
  flag = true;
  co_return;
}

// ─── One entry point for all policy combinations ─────────────────────

template <typename TQ, typename Pushed, Queue QP, Suspend SP>
void run_suite(std::string_view label) {
  using Pool = concurrency::pool::ThreadPool<TQ, Pushed>;
  constexpr bool payload_queue = concurrency::queues::PayloadEntry<TQ, Pushed>;

  TEST(label);
  Pool t(2);

  // 1. void probe
  {
    std::optional<bool> result;
    auto task = probe_void<TQ, Pushed, QP, SP>(t, result);
    task.get();
    assert(result.has_value() && *result);
  }

  // 2. bool probe
  assert((probe_bool<TQ, Pushed, QP, SP>(t).get()));

  // 3. nested
  {
    bool flag = false;
    auto task = nested_void<TQ, Pushed, QP, SP>(t, flag);
    task.get();
    assert(flag);
  }

  // 4. chain
  {
    int counter = 0;
    auto task = chain<TQ, Pushed, QP, SP>(t, 5, counter);
    task.get();
    assert(counter == 5);
  }

  // 5. external resume
  assert((external<TQ, Pushed, QP, SP>(t).get()));

  // 6. initial suspend semantics
  {
    bool flag = false;
    auto task = probe_initial_suspend<TQ, Pushed, SP>(flag);
    if constexpr (SP == Suspend::Always) {
      assert(!flag);
      task.start();
      assert(flag);
    } else {
      assert(flag);
    }
    assert(task.done());
  }

  // 7. payload path (only for payload-capable queues)
  if constexpr (payload_queue) {
    using Payload = concurrency::queues::PayloadOf<TQ, Pushed>;
    bool flag = false;
    auto task = probe_with_payload<TQ, Pushed, QP, SP>(t, flag, Payload{});
    task.get();
    assert(flag);
  }

  PASS();
}

template <typename TQ, typename Pushed>
void run_all_policies(std::string_view base) {
  run_suite<TQ, Pushed, Queue::Inline, Suspend::Always>(std::string(base) +
                                                        " [I/A]");
  run_suite<TQ, Pushed, Queue::Inline, Suspend::Never>(std::string(base) +
                                                       " [I/N]");
  run_suite<TQ, Pushed, Queue::Enqueue, Suspend::Always>(std::string(base) +
                                                         " [E/A]");
  run_suite<TQ, Pushed, Queue::Enqueue, Suspend::Never>(std::string(base) +
                                                        " [E/N]");
}

} // namespace ct::coro
