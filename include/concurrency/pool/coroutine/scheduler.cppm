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

template <policy::Queue QP> struct FROZENSTARCRYSTAL_CORE_API Scheduler {
private:
  queues::TaskQueue &queue_;

public:
  explicit Scheduler(queues::TaskQueue &queue) : queue_(queue) {};

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
      queues::TaskQueue *expected = nullptr;
      state->scheduler_queue.compare_exchange_strong(expected, &queue_,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed);
    }

    queue_.push([state]() mutable { state->do_resume(); });
  }

  void await_resume() noexcept {};
};

} // namespace concurrency::pool::coroutine
