module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:loop;

import std.compat;
import :queue;

export namespace concurrency::queues {

struct FROZENSTARCRYSTAL_CORE_API Loop {
private:
  std::vector<std::shared_ptr<Task>> queue;
  mutable std::mutex mutex;

  size_t index = 0;
  std::atomic<size_t> size{0};
  std::atomic<uint64_t> wake{0};

public:
  void push(Task task) {
    std::unique_lock lock(mutex);
    queue.push_back(std::make_shared<Task>(std::move(task)));
    lock.unlock();
    size.fetch_add(1, std::memory_order_release);
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_all();
  }

  bool peek(std::shared_ptr<Task> &task, const std::stop_token &stoken) {
    std::optional<std::stop_callback<std::function<void()>>> cb;
    while (true) {
      // --- If stopped, exit. ---
      if (stoken.stop_requested()) {
        return false;
      }

      // --- Fast path: cursor advance + hand out a weak ref. ---
      if (size.load(std::memory_order_relaxed) != 0) {
        std::unique_lock lock(mutex);
        if (!queue.empty()) {
          size_t i = index;
          index = (i + 1 == queue.size()) ? 0 : i + 1;

          task = queue[i]; // copy the weak_ptr — entry stays put
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
      while (size.load(std::memory_order_acquire) == 0 &&
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
      index = 0;
    }
    size.store(0, std::memory_order_release);
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_all();
  }

  bool remove(size_t indice) {
    std::scoped_lock lock(mutex);
    if (indice >= queue.size()) {
      return false;
    }
    if (indice < queue.size() - 1) {
      queue[indice] = std::move(queue.back());
    }
    queue.pop_back();

    if (queue.empty()) {
      index = 0;
    } else if (index >= queue.size()) {
      index = 0;
    }

    size.fetch_sub(1, std::memory_order_acquire);
    return true;
  }

  void notify_all() { wake.notify_all(); }
  bool empty() { return size.load(std::memory_order_acquire) == 0; }
};

} // namespace concurrency::queues
