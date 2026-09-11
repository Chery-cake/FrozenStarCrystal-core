module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:state;

import std.compat;

import concurrency.queues;

export namespace concurrency::pool::coroutine {

enum class AwaiterState : uint8_t {
  None,        // no awaiter
  Registering, // awaiter in await_suspend
  Waiting,     // awaiter suspended
};

struct FROZENSTARCRYSTAL_CORE_API CoroutineState
    : std::enable_shared_from_this<CoroutineState> {

  std::coroutine_handle<> handle;

  // continuation coordination
  std::mutex mtx;
  // TODO
  // find a way to turn awaiter_state into a atomic, so that state checks can
  // be lock free, making that only changing the continuation handles will
  // need a mutex
  AwaiterState awaiter_state = AwaiterState::None;
  std::coroutine_handle<> continuation = nullptr; // outer coroutine to resume
  std::shared_ptr<CoroutineState> continuation_state =
      nullptr; // state of outer coroutine
  std::atomic<queues::TaskQueue *> scheduler_queue{
      nullptr}; // queue to resume continuation on

  std::atomic<bool> executed{false};

  explicit CoroutineState(std::coroutine_handle<> h) : handle(h) {}
  ~CoroutineState() {
    if (handle) {
      handle.destroy();
      handle = nullptr;
    }
  }

  void do_resume() {
    // keep state alive
    auto self = shared_from_this();

    handle.resume();
  }

  void mark_executed() {
    executed.store(true, std::memory_order_release);
    executed.notify_all();
  }

  void wait_execution() const {
    executed.wait(false, std::memory_order_acquire);
  }

  bool done_executing() const {
    return executed.load(std::memory_order_acquire);
  }
};

using SharedHandle = std::shared_ptr<CoroutineState>;

inline SharedHandle make_shared_handle(std::coroutine_handle<> h) {
  return std::make_shared<CoroutineState>(h);
}

} // namespace concurrency::pool::coroutine
