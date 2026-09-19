module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:priority;

import std.compat;
import :queue;

namespace concurrency::queues {

template <typename E>
concept TaskEntry = requires(E e, const E ce) {
  { e.task } -> std::same_as<Task &>;
  { ce.task } -> std::same_as<const Task &>;
};

// std::priority_queue's container requirements, restricted to Entry.
template <typename C, typename E>
concept PriorityContainer = requires(C c, const C &cc, E e) {
  typename C::value_type;
  requires std::same_as<typename C::value_type, E>;
  requires std::random_access_iterator<typename C::iterator>;
  { c.push_back(std::move(e)) };
  { c.pop_back() };
  { cc.back() } -> std::same_as<const E &>;
};

template <typename Cmp, typename E>
concept EntryComparator = requires(Cmp cmp, const E &a, const E &b) {
  { cmp(a, b) } -> std::convertible_to<bool>;
};

} // namespace concurrency::queues

export namespace concurrency::queues {

template <TaskEntry Entry, PriorityContainer<Entry> Container,
          EntryComparator<Entry> Compare>
struct FROZENSTARCRYSTAL_CORE_API Priority {
private:
  Container queue;
  Compare cmp;

  mutable std::mutex mutex;
  std::counting_semaphore<> permits{0};

  std::atomic<size_t> size{0};
  std::atomic<uint64_t> wake{0};

public:
  void push(Entry e) {
    {
      std::scoped_lock lock(mutex);
      queue.push_back(std::move(e));
      std::ranges::push_heap(queue, cmp);
    }
    size.fetch_add(1, std::memory_order_release);
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_one();
    permits.release();
  }

  bool try_pop(Task &t, const std::stop_token &stoken) {
    std::optional<std::stop_callback<std::function<void()>>> cb;
    while (true) {

      // --- 1. Try to grab work, regardless of stop state. ---
      if (size.load(std::memory_order_relaxed) != 0 && permits.try_acquire()) {
        std::unique_lock lock(mutex);
        if (!queue.empty()) {
          std::ranges::pop_heap(queue, cmp);
          t = std::move(queue.back().task);
          queue.pop_back();
          lock.unlock();
          size.fetch_sub(1, std::memory_order_release);
          return true;
        }
        lock.unlock();
        permits.release(); // stale permit → hand back
      }

      // --- 2. No work visible. If stopped, do a final drain check
      //        under the lock (size_/permits_ are hints, not truth). ---
      if (stoken.stop_requested()) {
        std::unique_lock lock(mutex);
        if (!queue.empty()) {
          std::ranges::pop_heap(queue, cmp);
          t = std::move(queue.back().task);
          queue.pop_back();
          lock.unlock();
          size.fetch_sub(1, std::memory_order_release);
          permits.try_acquire(); // consume the permit the fast path
                                 // missed
          return true;
        }
        return false; // queue truly empty → done
      }

      // --- 3. Slow path: park until a push or a stop. ---
      if (!cb) {
        cb.emplace(stoken, [this] {
          wake.fetch_add(1, std::memory_order_release);
          wake.notify_all();
        });
      }
      uint64_t w = wake.load(std::memory_order_acquire);
      while (size.load(std::memory_order_acquire) == 0 &&
             !stoken.stop_requested()) {
        wake.wait(w, std::memory_order_acquire);
        w = wake.load(std::memory_order_acquire);
      }
      // Loop back: either work appeared (fast path will hit) or stop
      // was requested (drain check will run, then return false).
    }
  }

  void notify_all() { wake.notify_all(); }

  bool empty() { return size.load(std::memory_order_acquire) == 0; }
};

} // namespace concurrency::queues
