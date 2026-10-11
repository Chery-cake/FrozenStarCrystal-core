module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:structs;

import std.compat;

import concurrency.pool.coroutine.policy;
import concurrency.queues;
import :state;

namespace concurrency::pool::coroutine {

template <typename TQ, typename Pushed, typename State, typename... Args>
  requires(queues::PayloadEntry<TQ, Pushed> &&
           queues::BraceConstructible<typename Pushed::Payload,
                                      std::decay_t<Args>...>)
void enqueue_payload(State &state, TQ &queue, queues::Task t, Args &&...args) {
  using Payload = typename Pushed::Payload;
  Payload payload{std::forward<Args>(args)...};
  state->inherited_payload = payload;
  queue.push(queue.make(std::move(t), std::move(payload)));
}

} // namespace concurrency::pool::coroutine

export namespace concurrency::pool::coroutine {

template <typename TQ, typename Pushed, typename State, typename ArgsTuple>
void enqueue(State &state, TQ &queue, queues::Task t, ArgsTuple &&args) {
  std::apply(
      [&](auto &&...a) {
        constexpr size_t nargs = sizeof...(a);

        if constexpr (queues::TransientCapableQueue<TQ, Pushed>) {
          if constexpr (queues::TransientPayloadCapable<TQ, Pushed>) {
            using Payload = queues::PayloadOf<TQ, Pushed>;

            if (state->inherited_payload) {
              queue.push_transient(std::move(t), *state->inherited_payload);
            } else if constexpr (nargs > 0 &&
                                 queues::BraceConstructible<
                                     Payload, std::decay_t<decltype(a)>...>) {
              Payload p{std::forward<decltype(a)>(a)...};
              state->inherited_payload = p;
              queue.push_transient(std::move(t), std::move(p));
            } else if constexpr (std::default_initializable<Payload>) {
              queue.push_transient(std::move(t), Payload{});
            } else {
              static_assert(!std::is_same_v<Payload, Payload>,
                            "Transient entry carries a payload, but none "
                            "creation method is available; call "
                            "with_payload(...) or pass args to schedule(...).");
            }
          } else {
            queue.push_transient(std::move(t));
          }
          return;
        }

        if constexpr (queues::PayloadEntryFrom<TQ, Pushed,
                                               std::decay_t<decltype(a)>...> &&
                      nargs > 0) {
          enqueue_payload<TQ, Pushed>(state, queue, std::move(t),
                                      std::forward<decltype(a)>(a)...);
          return;
        }

        if constexpr (queues::PayloadEntry<TQ, Pushed>) {
          if (state->inherited_payload) {
            queue.push(queue.make(std::move(t), *state->inherited_payload));
          } else if constexpr (queues::PayloadEntryWithDefaultFactory<TQ,
                                                                      Pushed>) {
            queue.push(queue.make(std::move(t)));
          } else if constexpr (std::default_initializable<
                                   queues::PayloadOf<TQ, Pushed>>) {
            queue.push(
                queue.make(std::move(t), queues::PayloadOf<TQ, Pushed>{}));
          } else {
            static_assert(
                queues::PayloadEntryWithDefaultFactory<TQ, Pushed>,
                "No inherited payload and the queue does not provide a "
                "default entry factory; call with_payload(...) before "
                "scheduling.");
          }
        } else if constexpr (nargs > 0 &&
                             queues::DefaultConstructibleEntry<
                                 TQ, Pushed, std::decay_t<decltype(a)>...>) {
          queue.push(Pushed{std::move(t), std::forward<decltype(a)>(a)...});
        } else if constexpr (queues::DefaultConstructibleEntry<TQ, Pushed>) {
          queue.push(Pushed{std::move(t)});
        } else {
          static_assert(
              queues::DefaultConstructibleEntry<TQ, Pushed,
                                                std::decay_t<decltype(a)>...>,
              "No entry constructible from the given "
              "arguments; provide a "
              "Pushed constructor that accepts them.");
        }
      },
      std::forward<ArgsTuple>(args));
}
template <typename TQ, typename Pushed, typename State>
void enqueue(State &state, TQ &queue, queues::Task t) {
  enqueue<TQ, Pushed>(state, queue, std::move(t), std::tuple<>{});
}

template <typename TQ, typename Pushed>
  requires(queues::SchedulableQueue<TQ, Pushed>)
inline void schedule_continuation(const SharedHandle<TQ, Pushed> &state,
                                  TQ *queue) {
  if (!state) {
    return;
  }

  std::coroutine_handle<> cont;
  std::shared_ptr<CoroutineState<TQ, Pushed>> cont_state;

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
  TQ *target_queue = queue;
  if (target_queue == nullptr && cont_state) {
    std::lock_guard lock(cont_state->mtx);
    target_queue = cont_state->scheduler_queue.load(std::memory_order_acquire);
  }

  if (target_queue != nullptr) {

    queues::Task t = [cont_state, target_queue]() mutable {
      cont_state->do_resume();
      if (cont_state->done_executing()) {
        schedule_continuation<TQ, Pushed>(cont_state, target_queue);
      }
    };

    enqueue<TQ, Pushed>(cont_state, *target_queue, std::move(t));
  }
};

template <typename T, typename TQ, typename Pushed,
          template <typename, typename, policy::Suspend, typename> class Task,
          policy::Suspend SP>
  requires(queues::SchedulableQueue<TQ, Pushed>)
struct promise_type;

// Trait and concept
template <typename T> struct is_promise_type : std::false_type {};

template <typename T, typename TQ, typename Pushed,
          template <typename, typename, policy::Suspend, typename> class Task,
          policy::Suspend SP>
  requires(queues::SchedulableQueue<TQ, Pushed>)
struct is_promise_type<promise_type<T, TQ, Pushed, Task, SP>> : std::true_type {
};

template <typename T>
concept PromiseType = is_promise_type<T>::value;

// Final suspend
template <PromiseType promise, typename TQ, typename Pushed>
  requires(queues::SchedulableQueue<TQ, Pushed>)
struct FROZENSTARCRYSTAL_CORE_API FinalAwaiter {
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
    std::shared_ptr<CoroutineState<TQ, Pushed>> cont_state;

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

    TQ *queue = state->scheduler_queue.load(std::memory_order_acquire);
    if (queue != nullptr) {

      queues::Task t = [cont_state, queue]() {
        cont_state->do_resume();
        if (cont_state->done_executing()) {
          schedule_continuation(cont_state, queue);
        }
      };

      enqueue<TQ, Pushed>(cont_state, *queue, std::move(t));

      return std::noop_coroutine();
    }

    // TODO
    // restructure so this return is guarantee to not be used anymore
    // symetric transfer basically isn't being used here
    return cont;
  }

  void await_resume() const noexcept {}
};

template <typename T, typename TQ, typename Pushed,
          template <typename, typename, policy::Suspend, typename> class Task,
          policy::Suspend SP>
  requires(queues::SchedulableQueue<TQ, Pushed>)
struct FROZENSTARCRYSTAL_CORE_API promise_type {
  std::optional<T> result;
  std::exception_ptr exception;
  std::shared_ptr<CoroutineState<TQ, Pushed>> state = nullptr;
  bool started = false;

  // Return type of the coroutine
  using task_type = Task<TQ, Pushed, SP, T>;
  using handle_type = std::coroutine_handle<promise_type>;

  task_type get_return_object() noexcept {
    auto h = handle_type::from_promise(*this);
    state = make_shared_handle<TQ, Pushed>(h);
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
    return FinalAwaiter<promise_type, TQ, Pushed>{*this};
  }

  template <typename U>
    requires(!std::is_void_v<T> && std::convertible_to<U, T>)
  void
  return_value(U &&value) noexcept(std::is_nothrow_constructible_v<T, U &&>) {
    result.emplace(std::forward<U>(value));
  }

  void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <typename TQ, typename Pushed,
          template <typename, typename, policy::Suspend, typename> class Task,
          policy::Suspend SP>
  requires(queues::SchedulableQueue<TQ, Pushed>)
struct FROZENSTARCRYSTAL_CORE_API promise_type<void, TQ, Pushed, Task, SP> {
  std::exception_ptr exception;
  std::shared_ptr<CoroutineState<TQ, Pushed>> state = nullptr;
  bool started = false;

  // Return type of the coroutine
  using task_type = Task<TQ, Pushed, SP, void>;
  using handle_type = std::coroutine_handle<promise_type>;

  task_type get_return_object() noexcept {
    auto h = handle_type::from_promise(*this);
    state = make_shared_handle<TQ, Pushed>(h);
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
    return FinalAwaiter<promise_type, TQ, Pushed>{*this};
  }

  void return_void() noexcept {}

  void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <typename T, typename TQ, typename Pushed,
          template <typename, typename, policy::Suspend, typename> class Task,
          policy::Suspend SP>
  requires(queues::SchedulableQueue<TQ, Pushed>)
struct FROZENSTARCRYSTAL_CORE_API awaiter {
  SharedHandle<TQ, Pushed> handle_;

  // Derive the promise and handle types from the task type
  using task_type = Task<TQ, Pushed, SP, T>;
  using promise_type = typename task_type::promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit awaiter(SharedHandle<TQ, Pushed> h) noexcept
      : handle_(std::move(h)) {}

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

      TQ *queue = handle_->scheduler_queue.load(std::memory_order_acquire);

      if (queue == nullptr) {
        auto outer_state = awaiting.promise().state;
        if (outer_state) {
          queue = outer_state->scheduler_queue.load(std::memory_order_acquire);
          if constexpr (queues::PayloadEntry<TQ, Pushed>) {
            if (!handle_->inherited_payload) {
              handle_->inherited_payload = outer_state->inherited_payload;
            }
          }
        }
      }

      if (queue != nullptr) {
        TQ *expected = nullptr;
        handle_->scheduler_queue.compare_exchange_strong(
            expected, queue, std::memory_order_release,
            std::memory_order_relaxed);

        queues::Task t = [h = handle_]() mutable { h->do_resume(); };

        enqueue<TQ, Pushed>(handle_, *queue, std::move(t));

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
