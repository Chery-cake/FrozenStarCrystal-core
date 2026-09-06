module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:structs;

import std.compat;

import concurrency.pool.coroutine.policy;
import concurrency.queues;
import :state;

export namespace concurrency::pool::coroutine {

// TODO
// rework the suspension mechanism to always use symmetric transfer when no
// queue is available
//
// always use symmetric transfer, falling to the scheduller first

inline void schedule_continuation(const SharedHandle &state,
                                  queues::TaskQueue *queue) {
  if (!state) {
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

  // Determine the target queue for resuming the continuation.
  // Prefer the queue passed from the current coroutine’s scheduler.
  // Fall back to the continuation’s own scheduler_queue if available.
  queues::TaskQueue *target_queue = queue;
  if (target_queue == nullptr && cont_state) {
    std::lock_guard lock(cont_state->mtx);
    target_queue = cont_state->scheduler_queue;
  }

  if (target_queue != nullptr) {
    target_queue->push([cont, cont_state, target_queue]() mutable {
      cont.resume();
      if (cont_state && cont_state->done) {
        schedule_continuation(cont_state, target_queue);
      }
    });
  } else {
    // No scheduler queue was used; resume the continuation immediately.
    cont.resume();
    if (cont_state && cont_state->done) {
      schedule_continuation(cont_state, nullptr);
    }
  }
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
      return p.skip_initial_suspend;
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

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {
    if (p.state) {
      p.state->mark_completed();
      queues::TaskQueue *queue = p.state->scheduler_queue;
      if (queue != nullptr) {
        // Schedule continuation asynchronously
        schedule_continuation(p.state, queue);
        p.state = nullptr;
        return std::noop_coroutine();
      }
      // No queue: use symmetric transfer
      std::coroutine_handle<> cont;
      std::shared_ptr<CoroutineState> cont_state;
      {
        std::lock_guard lock(p.state->mtx);
        if (p.state->has_awaiter && p.state->continuation) {
          cont = p.state->continuation;
          cont_state = p.state->continuation_state;
          p.state->has_awaiter = false;
          p.state->continuation = nullptr;
          p.state->continuation_state = nullptr;
        }
      }
      p.state = nullptr; // release reference
      if (cont) {
        return cont; // transfer control to outer coroutine
      }
      return std::noop_coroutine();
    }
    return std::noop_coroutine();
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
  bool skip_initial_suspend = false; // TODO find a way to remove this flag

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
  bool skip_initial_suspend = false; // TODO find a way to remove this flag

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
      promise.skip_initial_suspend = true;
      handle_->handle.resume();
    }

    // If inner suspended, outer will be resumed later by scheduler.
    return std::noop_coroutine();
  }

  // await_resume: returns T for non-void, void for void
  decltype(auto) await_resume() {
    auto typed = handle_type::from_address(handle_->handle.address());
    auto &p = typed.promise();

    std::exception_ptr exc = p.exception;

    if constexpr (!std::is_void_v<T>) {
      if (exc) {
        std::rethrow_exception(exc);
      }

      T value = std::move(*p.result);
      return value;
    } else {
      if (exc) {
        std::rethrow_exception(exc);
      }
    }
  }
};

} // namespace concurrency::pool::coroutine
