module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:fifo;

import std.compat;
import :queue;

export namespace concurrency::queues {

struct FROZENSTARCRYSTAL_CORE_API FifoTaskQueue : public TaskQueue {
private:
  std::queue<Task> queue_;
  std::mutex mutex_;
  std::condition_variable_any cv_;

public:
  void push(Task t) override {
    {
      std::scoped_lock lock(mutex_);
      queue_.push(std::move(t));
    }
    cv_.notify_one();
  }

  bool try_pop(Task &t, const std::stop_token &stoken) override {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, stoken, [&queue = queue_] { return !queue.empty(); });
    if (queue_.empty() || stoken.stop_requested()) {
      return false;
    }

    t = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void notify_all() override { cv_.notify_all(); }

  bool empty() override {
    std::unique_lock lock(mutex_);
    return queue_.empty();
  }
};

} // namespace concurrency::queues
