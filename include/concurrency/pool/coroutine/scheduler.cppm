module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:scheduler;

import std.compat;
import concurrency.queues;
import concurrency.pool.coroutine.policy;

import :task;
import :task_void;
import :state;

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
    queue_.push([h]() mutable { h.resume(); });
  };

  void await_resume() noexcept {};
};

} // namespace concurrency::pool::coroutine
