module;

#include "FrozenStars_export.h"

export module concurrency.queues:queue;

import std.compat;

export namespace concurrency::queues {

using Task = std::move_only_function<void()>;

struct FROZENSTARS_API TaskQueue {
    virtual ~TaskQueue() = default;
    virtual void push(Task t) = 0;
    virtual bool try_pop(Task &t, const std::stop_token &stoken) = 0;
    virtual void notify_all() = 0;
    virtual bool empty() = 0;
};

} // namespace concurrency::queues
