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
  explicit CoroutineTask(SharedHandle handle) noexcept
      : handle_(std::move(handle)) {}

  CoroutineTask(const CoroutineTask &) = delete;
  CoroutineTask &operator=(const CoroutineTask &) = delete;

  CoroutineTask(CoroutineTask &&other) noexcept = default;
  CoroutineTask &operator=(CoroutineTask &&other) noexcept = default;

  ~CoroutineTask() = default;

  [[nodiscard]] bool valid() const noexcept {
    return handle_ && handle_->handle;
  }
  [[nodiscard]] bool done() const noexcept {
    return !valid() || handle_->done_executing();
  }

  void start() {
    auto typed = handle_type::from_address(handle_->handle.address());
    auto &promise = typed.promise();
    if (!promise.started) {
      promise.started = true;
      handle_->do_resume();
    }
  }

  T get() {
    auto typed = handle_type::from_address(handle_->handle.address());
    auto &promise = typed.promise();

    if constexpr (SP == policy::Suspend::Always) {
      if (!promise.started) {
        promise.started = true;
        handle_->do_resume();
      }
    }

    handle_->wait_execution();

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
