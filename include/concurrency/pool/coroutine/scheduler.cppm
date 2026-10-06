module;

#include "FrozenStarCrystal-core_export.h"
#include <cassert>

export module concurrency.pool.coroutine:scheduler;

import std.compat;
import concurrency.queues;
import concurrency.pool.coroutine.policy;

import :task;
import :state;
import :structs;

export namespace concurrency::pool::coroutine {

inline thread_local bool isPoolWorker = false;

template <typename TQ, typename Pushed, policy::Queue QP, typename... Args>
  requires(queues::Queue<TQ, Pushed>)
struct FROZENSTARCRYSTAL_CORE_API Scheduler {
private:
  TQ &queue_;
  std::tuple<Args...> args_;

public:
  explicit Scheduler(TQ &queue, Args... args)
      : queue_(queue), args_(std::move(args)...) {};

  // Move only
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = default;
  Scheduler(Scheduler &&other) = delete;
  Scheduler &operator=(Scheduler &&other) = default;

  constexpr bool await_ready() noexcept {
    if (!isPoolWorker) {
      return false;
    }
    if constexpr (QP == policy::Queue::Inline) {
      return true;
    }
    if constexpr (QP == policy::Queue::Enqueue) {
      return false;
    }
  };

  template <typename Promise>
  void await_suspend(std::coroutine_handle<Promise> h) {
    // Capture the state of the coroutine that is about to suspend.
    auto state = h.promise().state;
    assert(state && "promise.state must be set before any await");

    if (state) {
      TQ *expected = nullptr;
      state->scheduler_queue.compare_exchange_strong(expected, &queue_,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed);
    }

    std::apply(
        [&s = state, &q = queue_](auto &&...a) {
          queues::Task t = [s]() mutable { s->do_resume(); };
          q.push(Pushed{std::move(t), std::forward<decltype(a)>(a)...});
        },
        std::move(args_));
  }

  void await_resume() noexcept {};
};

} // namespace concurrency::pool::coroutine
