module;

export module concurrency.queues:queue;

import std.compat;

export namespace concurrency::queues {

using Task = std::move_only_function<void()>;

} // namespace concurrency::queues

namespace concurrency::queues {

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
    // TODO
    // change Task to be a unique or shared ptr
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

} // namespace concurrency::queues

export namespace concurrency::queues {

// --- The main concept: any of the behaviors ---
template <typename Q, typename Pushed = Task>
concept Queue = Consuming<Q, Pushed> || Looping<Q, Pushed>;

enum class Behaviour : uint8_t {
  Consuming,
  Looping,
};

} // namespace concurrency::queues
