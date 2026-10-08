module;

export module concurrency.queues:queue;

import std.compat;

export namespace concurrency::queues {

using Task = std::move_only_function<void()>;

// --- Base: operations *every* queue has, regardless of behavior ---
template <typename Q, typename Pushed = Task>
concept Base = requires(Q &q, Pushed &p) {
  { q.push(std::move(p)) } -> std::same_as<void>;
  { q.notify_all() } -> std::same_as<void>;
  { q.empty() } -> std::same_as<bool>;
};

// --- Behavior-specific concepts ---
template <typename Q, typename Pushed>
concept Consuming =
    Base<Q, Pushed> && requires(Q &q, Task &t, const std::stop_token &st) {
      { q.try_pop(t, st) } -> std::same_as<bool>;
    };

template <typename Q, typename Pushed>
concept Looping =
    Base<Q, Pushed> && requires(Q &q, std::shared_ptr<Task> &t,
                                const std::stop_token &st, size_t i) {
      { q.peek(t, st) } -> std::same_as<bool>;
      { q.clear() } -> std::same_as<void>;
      { q.remove(i) } -> std::same_as<bool>;
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

// A queue that exposes make(Task) and, when the entry has a Payload,
// make(Task, Payload).

template <typename T, typename... Args>
concept BraceConstructible =
    requires(Args &&...args) { T{std::forward<Args>(args)...}; };
template <typename Pushed>
concept HasPayload = requires { typename Pushed::Payload; };

template <typename TQ, typename Pushed>
concept PayloadEntry = HasPayload<Pushed> && requires(TQ q, const Pushed cp,
                                                      Task t) {
  Queue<TQ, Pushed>;
  { cp.payload() } -> std::same_as<typename Pushed::Payload>;
  {
    q.make(std::move(t), std::move(std::declval<typename Pushed::Payload>()))
  } -> std::same_as<Pushed>;
};

template <typename TQ, typename Pushed>
concept PayloadEntryWithDefaultFactory =
    PayloadEntry<TQ, Pushed> && requires(TQ q, Task t) {
      { q.make(std::move(t)) } -> std::same_as<Pushed>;
    };

template <typename TQ, typename Pushed, typename... Args>
concept DefaultConstructibleEntry =
    requires { BraceConstructible<Pushed, Task, Args...>; };

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

} // namespace concurrency::queues
