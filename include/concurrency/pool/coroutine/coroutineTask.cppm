module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:task;

import std.compat;
import concurrency.pool.coroutine.policy;
import :state;

export namespace concurrency::pool::coroutine {

template <policy::Suspend SP, typename T> class FROZENSTARCRYSTAL_CORE_API CoroutineTask {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

private:
  SharedHandle handle_;

  handle_type typed_handle() const {
    return handle_type::from_address(handle_->handle.address());
  }

public:
  explicit CoroutineTask(handle_type handle) noexcept
      : handle_(make_shared_handle(handle)) {}

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
      handle_->handle.resume();
    }
  }

  T get() {
    auto h = std::move(handle_);

    auto typed = handle_type::from_address(h->handle.address());
    auto &promise = typed.promise();

    if (h && !h->handle.done() && !promise.initialSuspend) {
      h->handle.resume();
    }
    promise.doneSignal.wait();

    if (promise.exception) {
      std::rethrow_exception(promise.exception);
    }

    T value = std::move(*promise.result);
    return value;
  }

  struct awaiter {
    SharedHandle handle_;

    explicit awaiter(SharedHandle h) noexcept : handle_(std::move(h)) {}

    [[nodiscard]] bool await_ready() const noexcept {
      return !handle_ || handle_->handle.done();
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> awaiting) noexcept {
      auto typed = handle_type::from_address(handle_->handle.address());
      auto &promise = typed.promise();
      promise.continuation = awaiting;

      if (!promise.initialSuspend) {
        return handle_->handle;
      }
      return std::noop_coroutine();
    }

    T await_resume() {
      auto typed = handle_type::from_address(handle_->handle.address());
      auto &p = typed.promise();
      if (p.exception) {
        std::rethrow_exception(p.exception);
      }

      auto value = std::move(*p.result);
      return value;
    }
  };

  auto operator co_await() && noexcept { return awaiter{std::move(handle_)}; }

  struct promise_type {
    std::optional<T> result;
    std::exception_ptr exception;
    std::coroutine_handle<> continuation = nullptr;
    std::latch doneSignal{1};
    bool done = false;
    bool initialSuspend = false;

    CoroutineTask get_return_object() noexcept {
      return CoroutineTask{handle_type::from_promise(*this)};
    }

    struct InitialAwaiter {
      promise_type &p;
      constexpr auto await_ready() const noexcept {
        if constexpr (SP == policy::Suspend::Always) {
          // return std::suspend_always{};
          return false;
        }
        if constexpr (SP == policy::Suspend::Never) {
          // return std::suspend_never{};
          return true;
        }
      }
      void await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {}
      void await_resume() const noexcept { p.initialSuspend = true; }
    };

    constexpr auto initial_suspend() noexcept { return InitialAwaiter{*this}; };

    auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(handle_type h) noexcept {
          auto &p = h.promise();

          if (!p.done) {
            p.doneSignal.count_down();
            p.done = true;
          }

          if (p.continuation) {
            return p.continuation;
          }

          return std::noop_coroutine();
        }

        void await_resume() noexcept {}
      };
      return final_awaiter{};
    }

    template <typename U>
      requires std::convertible_to<U, T>
    void
    return_value(U &&value) noexcept(std::is_nothrow_constructible_v<T, U &&>) {
      result.emplace(std::forward<U>(value));
    }

    void unhandled_exception() noexcept {
      exception = std::current_exception();
    }
  };
};

} // namespace concurrency::pool::coroutine
