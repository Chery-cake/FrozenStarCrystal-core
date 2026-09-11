module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:queue;

import std.compat;

export namespace concurrency::queues {

using Task = std::move_only_function<void()>;

enum class Priority : uint8_t { High, Normal, Low };

struct FROZENSTARCRYSTAL_CORE_API TaskQueue {
  virtual ~TaskQueue() = default;

  virtual void
  push(Task t, Priority p) = 0; // TODO find a better way to default the value
  void push(Task t) { push(std::move(t), Priority::Normal); }

  virtual bool try_pop(Task &t, const std::stop_token &stoken) = 0;
  virtual void notify_all() = 0;
  virtual bool empty() = 0;
};

} // namespace concurrency::queues
