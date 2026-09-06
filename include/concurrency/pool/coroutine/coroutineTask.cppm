module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:task;

import std.compat;

import concurrency.pool.coroutine.policy;
import :state;
import :structs;

export namespace concurrency::pool::coroutine {

template <policy::Suspend SP, typename T>
class FROZENSTARCRYSTAL_CORE_API CoroutineTask {
public:
  // Use the standalone promise_type and awaiter
  using promise_type = promise_type<T, CoroutineTask, SP>;
  using handle_type = std::coroutine_handle<promise_type>;
  using awaiter_type = awaiter<T, CoroutineTask, SP>;

private:
  SharedHandle handle_;

  handle_type typed_handle() const {
    return handle_type::from_address(handle_->handle.address());
  }

public:
  explicit CoroutineTask(handle_type handle) noexcept
      : handle_(make_shared_handle(handle)) {
    // Set the state pointer in the promise for later access.
    handle.promise().state = handle_;
    if constexpr (SP == policy::Suspend::Never) {
      handle.promise().started = true;
      current_state = handle_;
    }
  }

  CoroutineTask(const CoroutineTask &) = delete;
  CoroutineTask &operator=(const CoroutineTask &) = delete;

  CoroutineTask(CoroutineTask &&other) noexcept = default;
  CoroutineTask &operator=(CoroutineTask &&other) noexcept = default;

  ~CoroutineTask() = default;

  [[nodiscard]] bool valid() const noexcept {
    return handle_ && handle_->handle;
  }
  [[nodiscard]] bool done() const noexcept {
    return !valid() || handle_->handle.done();
  }

  void start() {
    if (handle_ && !handle_->handle.done()) {
      auto typed = handle_type::from_address(handle_->handle.address());
      auto &promise = typed.promise();

      promise.skip_initial_suspend = true;

      handle_->handle.resume();
    }
  }

  T get() {
    if (!handle_) {
      // TODO
      // deal with it
      // throw or assert
    }

    auto typed = handle_type::from_address(handle_->handle.address());
    auto &promise = typed.promise();

    if (handle_->handle.done()) {
      handle_->mark_completed();
    }

    if (!handle_->handle.done() && !promise.started) {
      promise.started = true;
      promise.skip_initial_suspend = true;
      handle_->handle.resume();
      if (handle_->handle.done()) {
        handle_->mark_completed();
      }
    }

    handle_->wait_completion();

    if (promise.exception) {
      std::rethrow_exception(promise.exception);
    }

    if constexpr (!std::is_void_v<T>) {
      return std::move(*promise.result);
    }
  }

  auto operator co_await() && noexcept {
    return awaiter_type{std::move(handle_)};
  }
};

} // namespace concurrency::pool::coroutine
