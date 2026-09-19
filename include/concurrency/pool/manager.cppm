module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool:manager;

import std.compat;

import resource;
import signals;

import concurrency.queues;
import :threadPool;

export namespace concurrency::pool {

struct FROZENSTARCRYSTAL_CORE_API Pool {
  std::string name;
  queues::QueueBehaviour queueBehaviour = queues::QueueBehaviour::Consuming;

  constexpr auto operator<=>(const Pool &) const noexcept = default;
};

template <queues::TaskQueue TQ>
using PoolRegistry =
    resource::Registry<Pool, ThreadPool<TQ>,
                       resource::SharedPtrPolicy<Pool, ThreadPool<TQ>>>;

template <queues::TaskQueue TQ>
using PoolSignal = signals::Signals<void(const Pool *, ThreadPool<TQ> *)>;
using ResizeSignal = signals::Signals<void(const Pool *, size_t, size_t)>;

template <queues::TaskQueue TQ> class FROZENSTARCRYSTAL_CORE_API Manager {
private:
  PoolRegistry<TQ> registry_;

  std::recursive_mutex mutex_;

public:
  Manager() = default;
  ~Manager() = default;

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&) = delete;
  Manager &operator=(Manager &&) = delete;

  // Signal objects – public so listeners can connect
  PoolSignal<TQ> onPoolAdded;
  PoolSignal<TQ> onPoolRemoved;
  ResizeSignal onPoolResized;

  // Convenience: disconnect all manager signals at once
  void clearSignals() {
    onPoolAdded.clear();
    onPoolRemoved.clear();
    onPoolResized.clear();
  }

  // Creation and destruction
  bool createPool(const Pool *tag, size_t num_threads = 0);
  bool removePool(const Pool *tag);

  // Split (causes a resize on the source pool and creation of a new one)
  bool split(const Pool *source, const Pool *new_tag,
             size_t threads_to_extract);

  // Access
  std::weak_ptr<ThreadPool<TQ>> getPool(const Pool *tag);

  // Resize an existing pool directly (also accessible through
  // ThreadPool::resize, but the manager version emits the signal)
  bool resizePool(const Pool *tag, size_t new_size);
};

} // namespace concurrency::pool
