module;

export module concurrency.pool:threadPool_impl;

import std.compat;
import :threadPool;
import concurrency.queues;
import concurrency.pool.coroutine;

export namespace concurrency::pool {

template <queues::TaskQueue TQ>
inline ThreadPool<TQ>::ThreadPool(size_t num_threads) {
  queue_ = std::make_unique<TQ>();

  size_t threads =
      (num_threads == 0) ? std::thread::hardware_concurrency() : num_threads;

  threads_.reserve(threads);

  std::ranges::for_each(
      std::views::iota(0U, threads),
      [&threads = threads_, &queue = queue_](size_t) {
        threads.emplace_back(
            [&queue](const std::stop_token &st) { worker_loop(st, *queue); });
      });
}

template <queues::TaskQueue TQ> inline ThreadPool<TQ>::~ThreadPool() {
  {
    std::unique_lock lock(mtx_);
    std::ranges::for_each(threads_, [](std::jthread &t) { t.request_stop(); });
  }

  if (queue_) {
    queue_->notify_all();
  }
  threads_.clear();
}

template <queues::TaskQueue TQ>
inline void ThreadPool<TQ>::worker_loop(const std::stop_token &stoken,
                                        TQ &queue) {
  struct WorkerGuard {
    ~WorkerGuard() { coroutine::isPoolWorker = false; }

  } guard;
  coroutine::isPoolWorker = true;

  queues::Task task;
  while (queue.try_pop(task, stoken)) {
    if (task) {
      task();
    }
  }
}

template <queues::TaskQueue TQ>
template <typename F>
  requires std::is_invocable_v<F>
void ThreadPool<TQ>::submit_detach(F &&f) {
  active_tasks_.fetch_add(1, std::memory_order_relaxed);

  try {
    queue_->push([this, f = std::forward<F>(f)]() mutable {
      struct Guard {
        ThreadPool *pool;
        ~Guard() { pool->task_finished(); }
      } guard{this};
      try {
        f();
      } catch (const std::exception &e) {
        // TODO: route to a global error handler / log sink
        std::println(std::cerr, "submit_detach: task threw: {}", e.what());
      } catch (...) {
        std::println(std::cerr, "submit_detach: task threw unknown exception");
        // TODO handle exceptions
      }
    });
  } catch (const std::exception &e) { // TODO make it rethorw the exception
                                      // after decreasing the cunter
    active_tasks_.fetch_sub(1, std::memory_order_relaxed);
    std::println(std::cerr, "submit_detach: queue threw: {}", e.what());
  } catch (...) {
    active_tasks_.fetch_sub(1, std::memory_order_relaxed);
    std::println(std::cerr, "submit_detach: queue threw unknown exception");
  }
}

template <queues::TaskQueue TQ>
template <typename F, typename... Args>
  requires std::is_invocable_v<F, Args...>
std::future<std::invoke_result_t<F, Args...>>
ThreadPool<TQ>::submit(F &&f, Args &&...args) {
  using Ret = std::invoke_result_t<F, Args...>;

  auto task = std::make_shared<std::packaged_task<Ret()>>(
      [f = std::forward<F>(f),
       ... a = std::forward<Args>(args)]() mutable -> Ret {
        return std::invoke(std::move(f), std::move(a)...);
      });

  std::future<Ret> fut = task->get_future();
  submit_detach([task] { (*task)(); });
  return fut;
}

template <queues::TaskQueue TQ>
template <coroutine::policy::Queue QP>
inline coroutine::Scheduler<TQ, QP> ThreadPool<TQ>::schedule() noexcept {
  return coroutine::Scheduler<TQ, QP>(*queue_);
}
template <queues::TaskQueue TQ>
template <coroutine::policy::Queue QP>
inline coroutine::Scheduler<TQ, QP>
ThreadPool<TQ>::schedule(TQ *queue) noexcept {
  return coroutine::Scheduler<TQ, QP>(*queue);
}

template <queues::TaskQueue TQ>
inline void ThreadPool<TQ>::resize(size_t new_size) {
  std::unique_lock lock(mtx_);
  size_t current = threads_.size();

  if (new_size > current) {
    threads_.reserve(new_size);

    std::ranges::for_each(
        std::views::iota(current, new_size),
        [&threads = threads_, &queue = queue_](size_t) {
          threads.emplace_back(
              [&queue](const std::stop_token &st) { worker_loop(st, *queue); });
        });
    return;
  }

  if (new_size < current) {
    std::ranges::for_each(threads_ | std::views::drop(new_size),
                          [](std::jthread &t) { t.request_stop(); });

    if (queue_) {
      queue_->notify_all();
    }

    threads_.erase(threads_.begin() + static_cast<std::ptrdiff_t>(new_size),
                   threads_.end());
    threads_.resize(new_size);
  }
}

} // namespace concurrency::pool
