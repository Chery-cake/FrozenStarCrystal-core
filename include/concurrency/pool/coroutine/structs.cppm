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
  if (!state) {
    return;
  }

  std::coroutine_handle<> cont;
  std::shared_ptr<CoroutineState> cont_state;

  {
    std::lock_guard lock(state->mtx);

    if (state->awaiter_state != AwaiterState::Waiting || !state->continuation) {
      return;
    }

    cont = state->continuation;
    cont_state = state->continuation_state;
    state->awaiter_state =
        AwaiterState::None; // mark that continuation has been taken
    state->continuation = nullptr;
    state->continuation_state = nullptr;
  }

  // Determine the target queue for resuming the continuation.
  // Prefer the queue passed from the current coroutine’s scheduler.
  // Fall back to the continuation’s own scheduler_queue if available.
  queues::TaskQueue *target_queue = queue;
  if (target_queue == nullptr && cont_state) {
    std::lock_guard lock(cont_state->mtx);
    target_queue = cont_state->scheduler_queue.load(std::memory_order_acquire);
  }

  if (target_queue != nullptr) {
    target_queue->push([cont_state, target_queue]() mutable {
      cont_state->do_resume();
      if (cont_state->done_executing()) {
        schedule_continuation(cont_state, target_queue);
      }
    });
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

// Final suspend
template <PromiseType promise> struct FROZENSTARCRYSTAL_CORE_API FinalAwaiter {
  promise &p;

  [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }

  [[nodiscard]] std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> /*unused*/) const noexcept {
    auto state = p.state;
    if (!state) {
      return std::noop_coroutine();
    }

    p.state = nullptr; // release the promise's own reference

    state->mark_executed();

    std::coroutine_handle<> cont;
    std::shared_ptr<CoroutineState> cont_state;

    {
      std::lock_guard lock(state->mtx);
      if (state->awaiter_state == AwaiterState::Waiting) {
        cont = state->continuation;
        cont_state = state->continuation_state;
        state->awaiter_state = AwaiterState::None;
        state->continuation = nullptr;
        state->continuation_state = nullptr;
      }
    }

    if (!cont) {
      return std::noop_coroutine();
    }

    queues::TaskQueue *queue =
        state->scheduler_queue.load(std::memory_order_acquire);
    if (queue != nullptr) {
      queue->push([cont_state, queue]() {
        cont_state->do_resume();
        if (cont_state->done_executing()) {
          schedule_continuation(cont_state, queue);
        }
      });
      return std::noop_coroutine();
    }

    // TODO
    // restructure so this return is guarantee to not be used anymore
    // symetric transfer basically isn't being used here
    return cont;
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
    auto h = handle_type::from_promise(*this);
    state = make_shared_handle(h);
    if constexpr (SP == policy::Suspend::Never) {
      started = true;
    }
    return task_type{state};
  }

  constexpr auto initial_suspend() noexcept {
    if constexpr (SP == policy::Suspend::Always) {
      return std::suspend_always{};
    }
    if constexpr (SP == policy::Suspend::Never) {
      return std::suspend_never{};
    }
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
    auto h = handle_type::from_promise(*this);
    state = make_shared_handle(h);
    if constexpr (SP == policy::Suspend::Never) {
      started = true;
    }
    return task_type{state};
  }

  constexpr auto initial_suspend() noexcept {
    if constexpr (SP == policy::Suspend::Always) {
      return std::suspend_always{};
    }
    if constexpr (SP == policy::Suspend::Never) {
      return std::suspend_never{};
    }
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
    return !handle_ || handle_->done_executing();
  }

  template <typename OuterPromise>
  bool await_suspend(std::coroutine_handle<OuterPromise> awaiting) noexcept {

    if (!handle_) {
      return false;
    }

    auto typed = handle_type::from_address(handle_->handle.address());
    auto &promise = typed.promise();

    if (handle_->done_executing()) {
      return false;
    }

    {
      std::lock_guard lock(handle_->mtx);

      handle_->awaiter_state = AwaiterState::Registering;
      handle_->continuation = awaiting;
      handle_->continuation_state = awaiting.promise().state;
    }

    if (!promise.started) {
      promise.started = true;

      queues::TaskQueue *queue =
          handle_->scheduler_queue.load(std::memory_order_acquire);

      if (queue == nullptr) {
        auto outer_state = awaiting.promise().state;
        if (outer_state) {
          queue = outer_state->scheduler_queue.load(std::memory_order_acquire);
        }
      }

      if (queue != nullptr) {
        queues::TaskQueue *expected = nullptr;
        handle_->scheduler_queue.compare_exchange_strong(
            expected, queue, std::memory_order_release,
            std::memory_order_relaxed);
        queue->push([h = handle_]() mutable { h->do_resume(); });
      } else {
        handle_->do_resume();
      }
    }

    {
      std::lock_guard lock(handle_->mtx);
      if (handle_->executed.load(std::memory_order_acquire)) {
        handle_->awaiter_state = AwaiterState::None;
        handle_->continuation = nullptr;
        handle_->continuation_state = nullptr;
        return false;
      }
      handle_->awaiter_state = AwaiterState::Waiting;
    }

    return true;
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
