module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:loop;

import std.compat;
import :queue;

export namespace concurrency::queues {

template <typename Entry>
  requires(std::same_as<Entry, Task> || RetireableLoopEntry<Entry>)
struct FROZENSTARCRYSTAL_CORE_API Loop {
public:
  using entry_type = Entry;

private:
  std::vector<std::shared_ptr<Entry>> queue;
  mutable std::mutex mutex;
  std::unordered_set<Entry *> in_flight;

  size_t index = 0;
  std::atomic<size_t> size{0};
  std::atomic<uint64_t> wake{0};

  std::atomic<size_t> available{0};

public:
  void push(Entry entry) {
    {
      std::unique_lock lock(mutex);
      queue.push_back(std::make_shared<Entry>(std::move(entry)));
      size.fetch_add(1, std::memory_order_release);
      available.fetch_add(1, std::memory_order_release);
    }
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_one();
  }

  bool peek(std::shared_ptr<Entry> &entry, const std::stop_token &stoken) {
    std::optional<std::stop_callback<std::function<void()>>> cb;
    while (true) {
      // --- If stopped, exit. ---
      if (stoken.stop_requested()) {
        return false;
      }

      // --- Fast path: cursor advance + hand out a weak ref. ---
      if (available.load(std::memory_order_relaxed) != 0) {
        std::unique_lock lock(mutex);
        size_t scanned = 0;

        while (!queue.empty() && scanned < queue.size()) {
          size_t i = index;
          Entry *raw = queue[i].get();
          if (in_flight.contains(raw)) {
            index = (i + 1 == queue.size()) ? 0 : i + 1;
            ++scanned;
            continue;
          }
          entry = queue[i];
          in_flight.insert(raw);

          if (entry_should_retire(*entry)) {
            in_flight.erase(raw);
            if (i != queue.size() - 1) {
              queue[i] = std::move(queue.back());
              if constexpr (requires { queue[i]->index; }) {
                queue[i]->index = i;
              }
            }
            queue.pop_back();
            size.fetch_sub(1, std::memory_order_release);
            if (queue.empty() || index >= queue.size()) {
              index = 0;
            }
          } else {
            index = (i + 1 == queue.size()) ? 0 : i + 1;
          }
          available.fetch_sub(1, std::memory_order_release);
          lock.unlock();
          return true;
        }
      }

      // --- Park until a push or a stop. ---
      if (!cb) {
        cb.emplace(stoken, [this] {
          wake.fetch_add(1, std::memory_order_release);
          wake.notify_all();
        });
      }
      uint64_t w = wake.load(std::memory_order_acquire);
      while (available.load(std::memory_order_acquire) == 0 &&
             !stoken.stop_requested()) {
        wake.wait(w, std::memory_order_acquire);
        w = wake.load(std::memory_order_acquire);
      }
    }
  }

  void clear() {
    {
      std::scoped_lock lock(mutex);
      queue.clear();
      in_flight.clear();
      index = 0;
      size.store(0, std::memory_order_release);
      available.store(0, std::memory_order_release);
    }
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_all();
  }

  bool remove(size_t indice) {
    std::scoped_lock lock(mutex);
    if (indice >= queue.size()) {
      return false;
    }
    bool was_in_flight = in_flight.erase(queue[indice].get()) > 0;
    if (indice < queue.size() - 1) {
      queue[indice] = std::move(queue.back());
    }
    queue.pop_back();

    if (queue.empty() || index >= queue.size()) {
      index = 0;
    }

    size.fetch_sub(1, std::memory_order_acquire);
    if (!was_in_flight) {
      available.fetch_sub(1, std::memory_order_acquire);
    }
    return true;
  }
  bool remove(const std::shared_ptr<Entry> &entry) {
    std::unique_lock lock(mutex);
    auto it = std::find(queue.begin(), queue.end(), entry);
    if (it == queue.end()) {
      return false;
    }

    auto i = static_cast<size_t>(std::distance(queue.begin(), it));

    bool was_in_flight = in_flight.erase(queue[i].get()) > 0;

    // If Entry tracks its own slot, keep it accurate for the moved one.
    if (i != queue.size() - 1) {
      queue[i] = std::move(queue.back());
      if constexpr (requires { queue[i]->index; }) {
        queue[i]->index = i;
      }
    }
    queue.pop_back();

    if (queue.empty() || index >= queue.size()) {
      index = 0;
    }
    // Note: peek has already advanced `index` past i, so no further fix-up
    // is needed here for the common case.

    size.fetch_sub(1, std::memory_order_release);
    if (!was_in_flight) {
      available.fetch_sub(1, std::memory_order_release);
    }
    return true;
  }

  void release(Entry &e) {
    {
      std::lock_guard lock(mutex);
      if (in_flight.erase(&e) == 0) {
        return;
      }
      available.fetch_add(1, std::memory_order_acq_rel);
    }
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_one();
  }

  void notify_all() { wake.notify_all(); }
  bool empty() { return size.load(std::memory_order_acquire) == 0; }
  // size_t available(){return available.load(std::memory_order_acquire);}

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
