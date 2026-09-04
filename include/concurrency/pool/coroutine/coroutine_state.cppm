module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:state;

import std.compat;

import concurrency.queues;

export namespace concurrency::pool::coroutine {

inline thread_local struct std::shared_ptr<struct CoroutineState>
    current_state = nullptr;

struct FROZENSTARCRYSTAL_CORE_API CoroutineState {
  std::coroutine_handle<> handle;
  std::mutex mtx;
  std::condition_variable cv;
  bool done = false;
  bool has_awaiter = false;
  std::coroutine_handle<> continuation = nullptr; // outer coroutine to resume
  std::shared_ptr<CoroutineState> continuation_state =
      nullptr; // state of outer coroutine
  queues::TaskQueue *scheduler_queue =
      nullptr; // queue to resume continuation on

  explicit CoroutineState(std::coroutine_handle<> h) : handle(h) {}
  ~CoroutineState() {
    if (handle) {
      handle.destroy();
    }
  }

  void mark_completed() {
    std::lock_guard lock(mtx);
    done = true;
    cv.notify_all();
  }

  void wait_completion() {
    std::unique_lock lock(mtx);
    cv.wait(lock, [&d = done] { return d; });
  }

  void destroy_handle() {
    std::lock_guard lock(mtx);
    if (handle) {
      handle.destroy();
      handle = nullptr;
    }
  }
};

using SharedHandle = std::shared_ptr<CoroutineState>;

inline SharedHandle make_shared_handle(std::coroutine_handle<> h) {
  return std::make_shared<CoroutineState>(h);
}

} // namespace concurrency::pool::coroutine
