module;

#include "FrozenStarCrystal-core_export.h"
#include <cstddef>

export module concurrency.pool:threadPool;

import std.compat;
import concurrency.queues;
import concurrency.pool.coroutine;

export namespace concurrency::pool {

template <queues::TaskQueue TQ> class FROZENSTARCRYSTAL_CORE_API ThreadPool {
private:
  std::unique_ptr<TQ> queue_;
  std::vector<std::jthread> threads_;
  std::atomic<size_t> active_tasks_{0};

  mutable std::mutex mtx_;

  static void worker_loop(const std::stop_token &stoken, TQ &queue);

  void task_finished() {
    if (active_tasks_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      active_tasks_.notify_all();
    }
  }

public:
  ThreadPool(size_t threads = 0);
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  template <typename F>
    requires std::is_invocable_v<F>
  void submit_detach(F &&f);

  template <typename F, typename... Args>
    requires std::is_invocable_v<F, Args...>
  std::future<std::invoke_result_t<F, Args...>> submit(F &&f, Args &&...args);

  template <coroutine::policy::Queue QP = coroutine::policy::Queue::Inline>
  coroutine::Scheduler<TQ, QP> schedule() noexcept;
  template <coroutine::policy::Queue QP = coroutine::policy::Queue::Enqueue>
  static coroutine::Scheduler<TQ, QP> schedule(TQ *queue) noexcept;

  void wait() {
    if (coroutine::isPoolWorker) {
      throw std::logic_error("ThreadPool::wait() called from worker thread");
    }

    size_t n = active_tasks_.load(std::memory_order_acquire);
    while (n != 0) {
      active_tasks_.wait(n, std::memory_order_acquire);
      n = active_tasks_.load(std::memory_order_acquire);
    }
  }

  void resize(size_t new_size);
  [[nodiscard]] size_t size() const noexcept {
    // TODO fix possible deadlock
    std::unique_lock lock(mtx_);
    return threads_.size();
  }

  [[nodiscard]] TQ *queue() { return queue_.get(); }
  [[nodiscard]] const TQ *queue() const { return queue_.get(); }
};

} // namespace concurrency::pool
