module;

export module concurrency.pool:manager_impl;

import std.compat;
import resource;
import signals;
import :threadPool;
import :manager;

export namespace concurrency::pool {

template <typename TQ, typename Pushed = queues::Task,
          queues::Behaviour Behaviour>
  requires(queues::Queue<TQ, Pushed>)
inline bool Manager::createPool(const Pool *tag, size_t num_threads) {
  std::unique_lock lock(mutex_);
  // registry_.emplace returns false if tag already exists
  size_t threads =
      num_threads == 0 ? std::thread::hardware_concurrency() : num_threads;

  auto pool = std::make_shared<ThreadPool<TQ, Pushed, Behaviour>>(threads);

  bool added = registry_.add(tag, std::move(pool));
  if (added) {
    // Retrieve the freshly created pool and notify listeners
    if (auto *raw = registry_.get(tag)) {
      onPoolAdded.emit(tag, raw);
    }
  }

  return added;
}

inline bool Manager::removePool(const Pool *tag) {
  std::unique_lock lock(mutex_);

  // First get the pool pointer for the signal
  auto *pool = registry_.get(tag);
  if (pool == nullptr) {
    return false;
  }

  onPoolRemoved.emit(tag, pool); // notify before actual removal
  return registry_.remove(tag);  // registry remove also triggers its own signal
}

inline bool Manager::resizePool(const Pool *tag, size_t new_size) {
  std::unique_lock lock(mutex_);

  auto *pool = registry_.get(tag);
  if (pool == nullptr || pool->size() == new_size || new_size == 0) {
    return false;
  }

  size_t old_size = pool->size();
  pool->resize(new_size);
  onPoolResized.emit(tag, old_size, new_size);
  return true;
}

template <typename TQ, typename Pushed = queues::Task,
          queues::Behaviour Behaviour>
  requires(queues::Queue<TQ, Pushed>)
inline bool Manager::split(const Pool *source, const Pool *new_tag,
                           size_t threads_to_extract) {
  std::unique_lock lock(mutex_);

  ThreadPoolBase *src_pool = registry_.get(source);
  if (src_pool == nullptr || src_pool->size() <= threads_to_extract) {
    return false;
  }

  size_t old_size = src_pool->size();
  // Reduce source pool – this emits the resized signal via resizePool()
  if (!resizePool(source, old_size - threads_to_extract)) {
    return false;
  }

  // Create the new pool – createPool will emit onPoolAdded
  return createPool<TQ, Pushed, Behaviour>(new_tag, threads_to_extract);
}

} // namespace concurrency::pool
