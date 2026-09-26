module;

#include "FrozenStarCrystal-core_export.h"
#include <cstddef>

export module concurrency.pool:threadPool;

import std.compat;
import concurrency.queues;
import concurrency.pool.coroutine;

export namespace concurrency::pool {

class FROZENSTARCRYSTAL_CORE_API ThreadPoolBase {
protected:
  std::vector<std::jthread> threads_;
  std::atomic<size_t> active_tasks_{0};

  mutable std::mutex mtx_;

  void task_finished() {
    if (active_tasks_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      active_tasks_.notify_all();
    }
  }

public:
  ThreadPoolBase() = default;
  virtual ~ThreadPoolBase() = default;

  ThreadPoolBase(const ThreadPoolBase &) = delete;
  ThreadPoolBase &operator=(const ThreadPoolBase &) = delete;
  ThreadPoolBase(ThreadPoolBase &&) = delete;
  ThreadPoolBase &operator=(ThreadPoolBase &&) = delete;

  // Type-erased surface used by Manager and callers who only need
  // "any pool".
  [[nodiscard]] virtual size_t size() const noexcept = 0;
  virtual void resize(size_t new_size) = 0;
  virtual void wait() = 0;
  [[nodiscard]] virtual queues::Behaviour behaviour() const noexcept = 0;

  // virtual void notify_all() = 0;

  // Fire-and-forget, no push args. Use getPool<TQ, Pushed, B>() to
  // reach the templated submit_detach when you need to forward
  // {priority, ...} style arguments.
  template <typename F, typename... Args> // TODO force implementation
    requires std::is_invocable_v<F, Args...>
  std::future<std::invoke_result_t<F, Args...>> submit(F &&f, Args &&...args);
  virtual void submit_detach_erased(std::move_only_function<void()> f) = 0;
};

template <typename TQ, typename Pushed = queues::Task,
          queues::Behaviour Behaviour = queues::Behaviour::Consuming>
  requires(queues::Queue<TQ, Pushed>)
class FROZENSTARCRYSTAL_CORE_API ThreadPool : public ThreadPoolBase {
private:
  std::unique_ptr<TQ> queue_;

  static void worker_loop(const std::stop_token &stoken, TQ &queue);

public:
  ThreadPool(size_t threads = 0);
  ~ThreadPool() override;

  // TODO check if needed here
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  template <typename F, typename... PushArgs>
    requires std::is_invocable_v<F> &&
             requires(queues::Task t, PushArgs &&...a) {
               Pushed{std::move(t), std::forward<PushArgs>(a)...};
             }
  void submit_detach(F &&f, PushArgs &&...pushArgs);
  void submit_detach_erased(std::move_only_function<void()> f) override {
    static_assert(std::is_constructible_v<Pushed, queues::Task>);
    submit_detach(std::move(f));
  }

  template <typename F, typename... Args>
    requires std::is_invocable_v<F, Args...>
  std::future<std::invoke_result_t<F, Args...>> submit(F &&f, Args &&...args);

  template <coroutine::policy::Queue QP = coroutine::policy::Queue::Inline>
  coroutine::Scheduler<TQ, QP> schedule() noexcept;
  template <coroutine::policy::Queue QP = coroutine::policy::Queue::Enqueue>
  static coroutine::Scheduler<TQ, QP> schedule(TQ *queue) noexcept;

  void wait() override {
    if (coroutine::isPoolWorker) {
      throw std::logic_error("ThreadPool::wait() called from worker thread");
    }

    if constexpr (Behaviour == queues::Behaviour::Looping) {
      // TODO
      // check if it should wait for the queue to be cleared or just
      // return imidiatle
      return;
    } else {
      size_t n = active_tasks_.load(std::memory_order_acquire);
      while (n != 0) {
        active_tasks_.wait(n, std::memory_order_acquire);
        n = active_tasks_.load(std::memory_order_acquire);
      }
    }
  }

  void resize(size_t new_size) override;
  [[nodiscard]] size_t size() const noexcept override {
    // TODO fix possible deadlock
    std::unique_lock lock(mtx_);
    return threads_.size();
  }

  [[nodiscard]] TQ *queue() { return queue_.get(); }
  [[nodiscard]] const TQ *queue() const { return queue_.get(); }

  [[nodiscard]] queues::Behaviour behaviour() const noexcept override {
    return Behaviour;
  }
};

} // namespace concurrency::pool
