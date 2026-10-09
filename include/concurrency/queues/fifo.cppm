module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:fifo;

import std.compat;
import :queue;

export namespace concurrency::queues {

template <GeneralEntry Entry = Task> struct FROZENSTARCRYSTAL_CORE_API Fifo {
private:
  std::queue<Entry> queue;
  mutable std::mutex mutex;
  std::counting_semaphore<> permits{0};

  std::atomic<size_t> size{0};
  std::atomic<uint64_t> wake{0};

public:
  void push(Entry e) {
    {
      std::scoped_lock lock(mutex);
      queue.push(std::move(e));
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
          t = to_task(std::move(queue.front()));
          queue.pop();
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
          t = to_task(std::move(queue.front()));
          queue.pop();
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

  Entry make(Task t)
    requires(std::same_as<Entry, Task> || BraceConstructible<Entry, Task>)
  {
    if constexpr (std::same_as<Entry, Task>) {
      return std::move(t);
    } else {
      return Entry{std::move(t)};
    }
  }

  template <typename Payload>
  Entry make(Task t, Payload p)
    requires(!std::same_as<Entry, Task> &&
             BraceConstructible<Entry, Task, Payload>)
  {
    return Entry{std::move(t), std::move(p)};
  }
};

} // namespace concurrency::queues
