module;

export module concurrency.pool:manager_impl;

import std.compat;
import resource;
import signals;
import :threadPool;
import :manager;

export namespace concurrency::pool {

inline bool Manager::createPool(const Pool *tag, size_t num_threads) {
    std::unique_lock lock(mutex_);
    // registry_.emplace returns false if tag already exists
    size_t threads =
        num_threads == 0 ? std::thread::hardware_concurrency() : num_threads;

    bool added = registry_.emplace(tag, threads);
    if (added) {
        // Retrieve the freshly created pool and notify listeners
        if (auto *pool = registry_.get(tag)) {
            onPoolAdded.emit(tag, pool);
        }
    }

    return added;
}

inline bool Manager::removePool(const Pool *tag) {
    // First get the pool pointer for the signal
    auto *pool = registry_.get(tag);
    if (pool == nullptr) {
        return false;
    }
    std::unique_lock lock(mutex_);

    onPoolRemoved.emit(tag, pool); // notify before actual removal
    return registry_.remove(
        tag); // registry remove also triggers its own signal
}

inline bool Manager::split(const Pool *source, const Pool *new_tag,
                           size_t threads_to_extract) {
    auto *src_pool = getPool(source);
    if (src_pool == nullptr || src_pool->size() <= threads_to_extract) {
        return false;
    }
    std::unique_lock lock(mutex_);

    size_t old_size = src_pool->size();
    // Reduce source pool – this emits the resized signal via resizePool()
    if (!resizePool(source, old_size - threads_to_extract)) {
        return false;
    }

    // Create the new pool – createPool will emit onPoolAdded
    return createPool(new_tag, threads_to_extract);
}

inline ThreadPool *Manager::getPool(const Pool *tag) {
    return registry_.get(tag);
}

inline bool Manager::resizePool(const Pool *tag, size_t new_size) {
    auto *pool = registry_.get(tag);
    if (pool == nullptr || pool->size() == new_size || new_size == 0) {
        return false;
    }
    std::unique_lock lock(mutex_);

    size_t old_size = pool->size();
    pool->resize(new_size);
    onPoolResized.emit(tag, old_size, new_size);
    return true;
}

} // namespace concurrency::pool
