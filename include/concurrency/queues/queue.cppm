module;

export module concurrency.queues:queue;

import std.compat;

export namespace concurrency::queues {

using Task = std::move_only_function<void()>;

template <typename E>
concept RetireableLoopEntry = requires(E &e, Task t) {
  { e.invoke() } -> std::same_as<void>;
  { e.should_retire() } -> std::convertible_to<bool>;
  E{std::move(t)};
};

template <typename E> inline void invoke_entry(E &e) {
  if constexpr (std::same_as<E, Task>) {
    e();
  } else {
    e.invoke();
  }
}
template <typename E> inline bool entry_should_retire(const E &e) {
  if constexpr (std::same_as<E, Task>) {
    return false;
  } else {
    return e.should_retire();
  }
}

// --- Base: operations *every* queue has, regardless of behavior ---
template <typename Q, typename Pushed = Task>
concept Base = requires(Q &q, Pushed &p) {
  q.push(std::move(p));
  q.notify_all();
  { q.empty() } -> std::same_as<bool>;
};

// --- Behavior-specific concepts ---
template <typename Q, typename Pushed>
concept Consuming =
    Base<Q, Pushed> && requires(Q &q, Task &t, const std::stop_token &st) {
      { q.try_pop(t, st) } -> std::same_as<bool>;
    };

template <typename Q, typename Pushed>
concept Looping = Base<Q, Pushed> &&
                  requires(Q &q, std::shared_ptr<Pushed> &t,
                           const std::stop_token &st, size_t i, Pushed &p) {
                    { q.peek(t, st) } -> std::same_as<bool>;
                    { q.clear() } -> std::same_as<void>;
                    { q.remove(i) } -> std::same_as<bool>;
                    { q.remove(t) } -> std::same_as<bool>;
                    { q.release(p) } -> std::same_as<void>;
                  };

// --- The main concept: any of the behaviors ---
template <typename Q, typename Pushed = Task>
concept Queue = Consuming<Q, Pushed> || Looping<Q, Pushed>;

enum class Behaviour : uint8_t {
  Consuming,
  Looping,
};

template <typename TQ, typename Pushed> constexpr Behaviour behaviour_of() {
  if constexpr (Consuming<TQ, Pushed>) {
    return Behaviour::Consuming;
  } else if constexpr (Looping<TQ, Pushed>) {
    return Behaviour::Looping;
  }
}

template <typename TQ, typename Pushed>
concept SchedulableQueue = Consuming<TQ, Pushed> || RetireableLoopEntry<Pushed>;

// A queue that exposes make(Task) and, when the entry has a Payload,
// make(Task, Payload).

template <typename T, typename... Args>
concept BraceConstructible =
    requires(Args &&...args) { T{std::forward<Args>(args)...}; };

template <typename Pushed>
concept Payload = requires(const Pushed cp) {
  typename Pushed::Payload;
  { cp.payload() } -> std::same_as<typename Pushed::Payload>;
};

template<typename  P>
concept CallablePayload =Payload<P>&&(
    requires(P p) { std::invoke(std::move(p)); } ||
    requires(P p, Task t) { std::invoke(std::move(p), std::move(t)); });

template <typename TQ, typename Pushed>
concept PayloadEntry =
    Payload<Pushed> && Queue<TQ, Pushed> &&
    requires(TQ q, const Pushed cp, Task t) {
      { cp.payload() } -> std::same_as<typename Pushed::Payload>;
      {
        q.make(std::move(t),
               std::move(std::declval<typename Pushed::Payload>()))
      } -> std::same_as<Pushed>;
    };

template <typename TQ, typename Pushed>
concept PayloadEntryWithDefaultFactory =
    PayloadEntry<TQ, Pushed> && requires(TQ q, Task t) {
      { q.make(std::move(t)) } -> std::same_as<Pushed>;
    };

template <typename TQ, typename Pushed, typename... Args>
concept DefaultConstructibleEntry = BraceConstructible<Pushed, Task, Args...>;

template <typename TQ, typename Pushed> struct PayloadOfImpl {
  using type = std::monostate;
};

template <typename TQ, typename Pushed>
  requires(PayloadEntry<TQ, Pushed>)
struct PayloadOfImpl<TQ, Pushed> {
  using type = typename Pushed::Payload;
};

template <typename TQ, typename Pushed>
using PayloadOf = typename PayloadOfImpl<TQ, Pushed>::type;

template <typename TQ, typename Pushed, typename... Args>
concept PayloadEntryFrom = PayloadEntry<TQ, Pushed> &&
                           BraceConstructible<PayloadOf<TQ, Pushed>, Args...>;

template <typename Entry>
concept GeneralEntry =
    std::same_as<std::remove_cvref_t<Entry>, Task> ||
    requires(std::remove_cvref_t<Entry> en) {
      { std::move(en).task } -> std::convertible_to<Task>;
    } ||
    (Payload<std::remove_cvref_t<Entry>> &&
     requires(typename std::remove_cvref_t<Entry>::Payload p) {
       { std::invoke(std::move(p)) } -> std::same_as<void>;
     });

template <typename Entry>
  requires(GeneralEntry<Entry>)
Task to_task(Entry &&e) {
  using E = std::remove_cvref_t<Entry>;

  if constexpr (std::same_as<E, Task>) {
    return std::forward<Entry>(e);
  } else if constexpr (
      Payload<E> &&
      requires(E en) {
        { std::move(en).task } -> std::convertible_to<Task>;
      } &&
      requires(typename E::Payload p, Task t) {
        { std::invoke(std::move(p), std::move(t)) } -> std::same_as<void>;
      }) {
    return Task{[p = std::forward<Entry>(e).payload(),
                 t = std::forward<Entry>(e).task]() mutable {
      std::invoke(std::move(p), std::move(t));
    }};
  } else if constexpr (Payload<E> && requires(typename E::Payload p) {
                         { std::invoke(std::move(p)) } -> std::same_as<void>;
                       }) {
    return Task{[p = std::forward<Entry>(e).payload()]() mutable {
      std::invoke(std::move(p));
    }};
  } else {
    return std::forward<Entry>(e).task;
  }
}

} // namespace concurrency::queues

// TODO
// make so that the payload can have extra values for the queue or task, like
// priority value for the queue or extra parameters for a overload of the
// function
// make so the payload can carry a function call, that can be called alongside
// the original task or replace it
//
// the payload must be able to run on any queue, fifo priority loop etc
