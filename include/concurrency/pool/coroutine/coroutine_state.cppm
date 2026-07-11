module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.pool.coroutine:state;

import std.compat;

export namespace concurrency::pool::coroutine {

struct FROZENSTARCRYSTAL_CORE_API CoroutineState {
  std::coroutine_handle<> handle;
  explicit CoroutineState(std::coroutine_handle<> h) : handle(h) {}
  ~CoroutineState() {
    if (handle) {
      handle.destroy();
    }
  }
};

using SharedHandle = std::shared_ptr<CoroutineState>;

inline SharedHandle make_shared_handle(std::coroutine_handle<> h) {
  return std::make_shared<CoroutineState>(h);
}

} // namespace concurrency::pool::coroutine
