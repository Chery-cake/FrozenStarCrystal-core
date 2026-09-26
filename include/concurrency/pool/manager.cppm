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
  // queues::Behaviour queueBehaviour = queues::Behaviour::Consuming;
  // was turned into a template
  // TODO
  // find something to add here besides the name

  constexpr auto operator<=>(const Pool &) const noexcept = default;
};

using PoolRegistry =
    resource::Registry<Pool, ThreadPoolBase,
                       resource::SharedPtrPolicy<Pool, ThreadPoolBase>>;

using PoolSignal = signals::Signals<void(const Pool *, ThreadPoolBase *)>;
using ResizeSignal = signals::Signals<void(const Pool *, size_t, size_t)>;

class FROZENSTARCRYSTAL_CORE_API Manager {
private:
  PoolRegistry registry_;

  std::recursive_mutex mutex_;

public:
  Manager() = default;
  ~Manager() = default;

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&) = delete;
  Manager &operator=(Manager &&) = delete;

  // Signal objects – public so listeners can connect
  PoolSignal onPoolAdded;
  PoolSignal onPoolRemoved;
  ResizeSignal onPoolResized;

  // Convenience: disconnect all manager signals at once
  void clearSignals() {
    onPoolAdded.clear();
    onPoolRemoved.clear();
    onPoolResized.clear();
  }

  // Creation and destruction
  template <typename TQ, typename Pushed = queues::Task,
            queues::Behaviour Behaviour = queues::Behaviour::Consuming>
    requires(queues::Queue<TQ, Pushed>)
  bool createPool(const Pool *tag, size_t num_threads = 0);
  bool removePool(const Pool *tag);
  bool resizePool(const Pool *tag, size_t new_size);

  // Split (causes a resize on the source pool and creation of a new one)
  template <typename TQ, typename Pushed = queues::Task,
            queues::Behaviour Behaviour = queues::Behaviour::Consuming>
    requires(queues::Queue<TQ, Pushed>)
  bool split(const Pool *source, const Pool *new_tag,
             size_t threads_to_extract);

  // Access
  template <typename TQ, typename Pushed = queues::Task,
            queues::Behaviour Behaviour = queues::Behaviour::Consuming>
    requires(queues::Queue<TQ, Pushed>)
  std::shared_ptr<ThreadPool<TQ, Pushed, Behaviour>> getPool(const Pool *tag) {
    return std::dynamic_pointer_cast<ThreadPool<TQ, Pushed, Behaviour>>(
        registry_.getStored(tag));
  }

  template <typename TQ, typename Pushed = queues::Task,
            queues::Behaviour Behaviour = queues::Behaviour::Consuming>
    requires(queues::Queue<TQ, Pushed>)
  ThreadPool<TQ, Pushed, Behaviour> *getPoolRaw(const Pool *tag) {
    return dynamic_cast<ThreadPool<TQ, Pushed, Behaviour> *>(
        registry_.get(tag));
  }
  std::shared_ptr<ThreadPoolBase> getPoolBase(const Pool *tag) {
    return registry_.getStored(tag);
  }
};

} // namespace concurrency::pool
