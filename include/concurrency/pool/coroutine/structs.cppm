module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:structs;

import std.compat;

import concurrency.pool.coroutine.policy;
import concurrency.queues;
import :state;

export namespace concurrency::pool::coroutine {

inline void schedule_continuation(const SharedHandle &state,
                                  queues::TaskQueue *queue) {
  if (!state || queue == nullptr) {
    return;
  }

  std::coroutine_handle<> cont;
  std::shared_ptr<CoroutineState> cont_state;

  {
    std::lock_guard lock(state->mtx);
    if (!state->has_awaiter || !state->continuation) {
      return;
    }
    cont = state->continuation;
    cont_state = state->continuation_state;
    state->has_awaiter = false; // mark that continuation has been taken
    state->continuation = nullptr;
    state->continuation_state = nullptr;
  }

  queue->push([cont, cont_state, queue]() mutable {
    cont.resume();
    if (cont_state && cont_state->done) {
      schedule_continuation(cont_state, queue);
    }
  });
};

template <typename T, template <policy::Suspend, typename> class Task,
          policy::Suspend SP>
struct promise_type;

// Trait and concept
template <typename T> struct is_promise_type : std::false_type {};

template <typename T, template <policy::Suspend, typename> class Task,
          policy::Suspend SP>
struct is_promise_type<promise_type<T, Task, SP>> : std::true_type {};

template <typename T>
concept PromiseType = is_promise_type<T>::value;

// Initial and final suspend
template <PromiseType promise, policy::Suspend SP>
struct FROZENSTARCRYSTAL_CORE_API InitialAwaiter {
  promise &p;
  constexpr auto await_ready() const noexcept {
    if constexpr (SP == policy::Suspend::Always) {
      return false;
    }
    if constexpr (SP == policy::Suspend::Never) {
      return true;
    }
  }
  void await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {}
  void await_resume() const noexcept {
    p.started = true;
    current_state = p.state;
  }
};

template <PromiseType promise> struct FROZENSTARCRYSTAL_CORE_API FinalAwaiter {
  promise &p;

  [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {
    if (p.state) {
      p.state->mark_completed();
      p.state = nullptr;
    }
    // Remain suspended so the coroutine frame is not destroyed yet.
  }
  void await_resume() const noexcept {}
};

template <typename T, template <policy::Suspend, typename> class Task,
          policy::Suspend SP>
struct FROZENSTARCRYSTAL_CORE_API promise_type {
  std::optional<T> result;
  std::exception_ptr exception;
  std::shared_ptr<CoroutineState> state = nullptr;
  bool started = false;

  // Return type of the coroutine
  using task_type = Task<SP, T>;
  using handle_type = std::coroutine_handle<promise_type>;

  task_type get_return_object() noexcept {
    return task_type{handle_type::from_promise(*this)};
  }

  constexpr auto initial_suspend() noexcept {
    return InitialAwaiter<promise_type, SP>{*this};
  }
  constexpr auto final_suspend() noexcept {
    return FinalAwaiter<promise_type>{*this};
  }

  template <typename U>
    requires(!std::is_void_v<T> && std::convertible_to<U, T>)
  void
  return_value(U &&value) noexcept(std::is_nothrow_constructible_v<T, U &&>) {
    result.emplace(std::forward<U>(value));
  }

  void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <template <policy::Suspend, typename> class Task, policy::Suspend SP>
struct FROZENSTARCRYSTAL_CORE_API promise_type<void, Task, SP> {
  std::exception_ptr exception;
  std::shared_ptr<CoroutineState> state = nullptr;
  bool started = false;

  // Return type of the coroutine
  using task_type = Task<SP, void>;
  using handle_type = std::coroutine_handle<promise_type>;

  task_type get_return_object() noexcept {
    return task_type{handle_type::from_promise(*this)};
  }

  constexpr auto initial_suspend() noexcept {
    return InitialAwaiter<promise_type, SP>{*this};
  }
  constexpr auto final_suspend() noexcept {
    return FinalAwaiter<promise_type>{*this};
  }

  void return_void() noexcept {}

  void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <typename T, template <policy::Suspend, typename> class Task,
          policy::Suspend SP>
struct FROZENSTARCRYSTAL_CORE_API awaiter {
  SharedHandle handle_;

  // Derive the promise and handle types from the task type
  using task_type = Task<SP, T>;
  using promise_type = typename task_type::promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit awaiter(SharedHandle h) noexcept : handle_(std::move(h)) {}

  [[nodiscard]] bool await_ready() const noexcept {
    return !handle_ || handle_->handle.done();
  }

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> awaiting) noexcept {

    if (!handle_) {
      return std::noop_coroutine();
    }

    auto typed = handle_type::from_address(handle_->handle.address());
    auto &promise = typed.promise();

    {
      std::lock_guard lock(handle_->mtx);
      handle_->has_awaiter = true;
      handle_->continuation = awaiting;
      handle_->continuation_state = current_state;
    }

    if (!promise.started) {
      promise.started = true;
      handle_->handle.resume();
      if (handle_->handle.done()) {
        handle_->mark_completed();
        schedule_continuation(handle_, handle_->scheduler_queue);
        return std::noop_coroutine(); // outer will be resumed via queue
      }
      // If inner suspended, outer will be resumed later by scheduler.
      return std::noop_coroutine();
    }
    // Inner already started (suspended). Check if it completed in the
    // meantime.
    bool already_done = false;
    {
      std::lock_guard lock(handle_->mtx);
      already_done = handle_->done;
    }
    if (already_done) {
      schedule_continuation(handle_, handle_->scheduler_queue);
    }
    return std::noop_coroutine();
  }

  // await_resume: returns T for non-void, void for void
  decltype(auto) await_resume() {
    auto typed = handle_type::from_address(handle_->handle.address());
    auto &p = typed.promise();

    std::exception_ptr exc = p.exception;

    if constexpr (!std::is_void_v<T>) {
      std::optional<T> result;
      if (p.result) {
        result = std::move(*p.result);
      }
      // Destroy the frame after extracting result
      handle_->destroy_handle();

      if (exc) {
        std::rethrow_exception(exc);
      }

      T value = std::move(*result);
      return value;
    } else {
      handle_->destroy_handle();
      if (exc) {
        std::rethrow_exception(exc);
      }
    }
  }
};

} // namespace concurrency::pool::coroutine
