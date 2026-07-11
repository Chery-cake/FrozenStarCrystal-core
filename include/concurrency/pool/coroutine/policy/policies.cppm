module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine.policy:policies;

import std.compat;

export namespace concurrency::pool::coroutine::policy {

enum class FROZENSTARCRYSTAL_CORE_API Queue : uint8_t {
  Inline,
  Enqueue,
};

enum class FROZENSTARCRYSTAL_CORE_API Suspend : uint8_t {
  Always,
  Never,
};

} // namespace concurrency::pool::coroutine::policy
