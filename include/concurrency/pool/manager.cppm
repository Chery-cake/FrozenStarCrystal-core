module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool:manager;

import std.compat;
import resource;
import signals;
import :threadPool;

export namespace concurrency::pool {

using PoolRegistry =
    resource::Registry<Pool, ThreadPool,
                       resource::UniquePtrPolicy<Pool, ThreadPool>>;

using PoolSignal = signals::Signals<void(const Pool *, ThreadPool *)>;
using ResizeSignal = signals::Signals<void(const Pool *, size_t, size_t)>;

class FROZENSTARCRYSTAL_CORE_API Manager {
private:
  PoolRegistry registry_;

  std::recursive_mutex mutex_;

public:
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
  bool createPool(const Pool *tag, size_t num_threads = 0);
  bool removePool(const Pool *tag);

  // Split (causes a resize on the source pool and creation of a new one)
  bool split(const Pool *source, const Pool *new_tag,
             size_t threads_to_extract);

  // Access
  ThreadPool *getPool(const Pool *tag);

  // Resize an existing pool directly (also accessible through
  // ThreadPool::resize, but the manager version emits the signal)
  bool resizePool(const Pool *tag, size_t new_size);
};

} // namespace concurrency::pool
