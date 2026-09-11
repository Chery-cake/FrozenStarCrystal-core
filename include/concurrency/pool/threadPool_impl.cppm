module;

export module concurrency.pool:threadPool_impl;

import std.compat;
import :threadPool;
import concurrency.queues;
import concurrency.pool.coroutine;

export namespace concurrency::pool {

inline ThreadPool::ThreadPool(const Pool &pool, size_t num_threads) {
  switch (pool.queueKind) {
  default:
  case queues::QueueKind::FIFO:
    queue_ = std::make_unique<queues::FifoTaskQueue>();
    break;
  }

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

inline ThreadPool::~ThreadPool() {
  {
    std::unique_lock lock(mtx_);
    std::ranges::for_each(threads_, [](std::jthread &t) { t.request_stop(); });
  }

  if (queue_) {
    queue_->notify_all();
  }
  threads_.clear();
}

inline void ThreadPool::worker_loop(const std::stop_token &stoken,
                                    queues::TaskQueue &queue) {
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

template <typename F>
  requires std::is_invocable_v<F>
void ThreadPool::submit_detach(F &&f) {
  active_tasks_.fetch_add(1, std::memory_order_relaxed);

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
}

template <typename F, typename... Args>
  requires std::is_invocable_v<F, Args...>
std::future<std::invoke_result_t<F, Args...>>
ThreadPool::submit(F &&f, Args &&...args) {
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

template <coroutine::policy::Queue QP>
inline coroutine::Scheduler<QP> ThreadPool::schedule() noexcept {
  return coroutine::Scheduler<QP>(*queue_);
}
template <coroutine::policy::Queue QP>
inline coroutine::Scheduler<QP>
ThreadPool::schedule(queues::TaskQueue *queue) noexcept {
  return coroutine::Scheduler<QP>(*queue);
}

inline void ThreadPool::resize(size_t new_size) {
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
