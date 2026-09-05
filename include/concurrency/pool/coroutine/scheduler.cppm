module;

#include "FrozenStarCrystal-core_export.h"

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

  void await_suspend(std::coroutine_handle<> h) {
    // Capture the state of the coroutine that is about to suspend.
    auto state = current_state;

    if (state) {
      std::lock_guard lock(state->mtx);
      state->scheduler_queue = &queue_;
    }

    queue_.push([h, state, this]() mutable {
      h.resume();

      if (state && state->done) {
        schedule_continuation(state, &queue_);
      }
    });
  }

  void await_resume() noexcept {};
};

} // namespace concurrency::pool::coroutine
