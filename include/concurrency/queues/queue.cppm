module;

export module concurrency.queues:queue;

import std.compat;

export namespace concurrency::queues {

using Task = std::move_only_function<void()>;

enum class Priority : uint8_t { High, Normal, Low };

} // namespace concurrency::queues

namespace concurrency::queues {

// --- Base: operations *every* queue has, regardless of behavior ---
template <typename Q>
concept Base = requires(Q &q, Task &t) {
  { q.push(std::move(t), Priority::Normal) } -> std::same_as<void>;
  { q.notify_all() } -> std::same_as<void>;
  { q.empty() } -> std::same_as<bool>;
};

// --- Behavior-specific concepts ---
template <typename Q>
concept Consuming =
    Base<Q> && requires(Q &q, Task &t, const std::stop_token &st) {
      { q.try_pop(t, st) } -> std::same_as<bool>;
    };

template <typename Q>
concept Looping =
    Base<Q> && requires(Q &q, Task &t, const std::stop_token &st) {
      { q.peek(t, st) } -> std::same_as<bool>;
      { q.rewind() } -> std::same_as<void>;
    };

} // namespace concurrency::queues

export namespace concurrency::queues {

// --- The main concept: any of the behaviors ---
template <typename Q>
concept TaskQueue = Consuming<Q> || Looping<Q>;

enum class QueueBehaviour : uint8_t {
  Consuming,
  Looping

};

} // namespace concurrency::queues
