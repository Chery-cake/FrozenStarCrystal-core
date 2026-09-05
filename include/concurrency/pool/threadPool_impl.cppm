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
    std::unique_lock lock(threads_mutex_);
    std::ranges::for_each(threads_, [](std::jthread &t) { t.request_stop(); });
  }

  if (queue_) {
    queue_->notify_all();
  }
}

inline void ThreadPool::worker_loop(const std::stop_token &stoken,
                                    queues::TaskQueue &queue) {
  struct WorkerGuard {
    ~WorkerGuard() { coroutine::isPoolWorker = false; }

  } guard;
  coroutine::isPoolWorker = true;

  queues::Task task;
  while (!stoken.stop_requested() && queue.try_pop(task, stoken)) {
    if (task) {
      task();
    }
  }
}

template <typename F, typename... Args>
std::future<std::invoke_result_t<F, Args...>>
ThreadPool::submit(F &&f, Args &&...args) {
  using Ret = std::invoke_result_t<F, Args...>;

  auto task = std::make_shared<std::packaged_task<Ret()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<Ret> fut = task->get_future();

  {
    std::lock_guard lock(threads_mutex_);
    ++active_tasks_;
  }

  queue_->push(
      [task, &mtx = tasks_mutex_, &tasks = active_tasks_, &cv = cv_done_]() {
        (*task)();

        std::lock_guard lock(mtx);
        --tasks;
        cv.notify_all();
      });
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
  std::unique_lock lock(threads_mutex_);
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

    threads_.erase(threads_.begin() + static_cast<std::ptrdiff_t>(new_size),
                   threads_.end());
    threads_.resize(new_size);
  }
}

} // namespace concurrency::pool
