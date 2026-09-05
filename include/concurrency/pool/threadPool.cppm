module;

#include "FrozenStarCrystal-core_export.h"
#include <cstddef>

export module concurrency.pool:threadPool;

import std.compat;
import concurrency.queues;
import concurrency.pool.coroutine;

export namespace concurrency::pool {

struct FROZENSTARCRYSTAL_CORE_API Pool {
  std::string name;
  queues::QueueKind queueKind = queues::QueueKind::FIFO;

  constexpr auto operator<=>(const Pool &) const noexcept = default;
};

class FROZENSTARCRYSTAL_CORE_API ThreadPool {
private:
  std::unique_ptr<queues::TaskQueue> queue_;
  std::vector<std::jthread> threads_;
  std::condition_variable cv_done_;
  size_t active_tasks_ = 0;

  mutable std::mutex threads_mutex_;
  std::mutex tasks_mutex_;

  static void worker_loop(const std::stop_token &stoken,
                          queues::TaskQueue &queue);

public:
  ThreadPool(const Pool &pool, size_t threads = 0);
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  template <typename F, typename... Args>
  std::future<std::invoke_result_t<F, Args...>> submit(F &&f, Args &&...args);

  template <coroutine::policy::Queue QP = coroutine::policy::Queue::Inline>
  coroutine::Scheduler<QP> schedule() noexcept;
  template <coroutine::policy::Queue QP = coroutine::policy::Queue::Enqueue>
  static coroutine::Scheduler<QP> schedule(queues::TaskQueue *queue) noexcept;

  void wait() {
    std::unique_lock lock(tasks_mutex_);
    cv_done_.wait(lock, [&tasks = active_tasks_, &queue = queue_]() {
      return tasks == 0 && queue->empty();
    });
  }

  void resize(size_t new_size);
  [[nodiscard]] size_t size() const noexcept {
    std::unique_lock lock(threads_mutex_);
    return threads_.size();
  }

  [[nodiscard]] queues::TaskQueue *queue() { return queue_.get(); }
  [[nodiscard]] const queues::TaskQueue *queue() const { return queue_.get(); }
};

} // namespace concurrency::pool
