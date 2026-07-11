module;

#include "FrozenStarCrystal-core_export.h"

export module ecs.component:signalHub;

import std.compat;
import signals;

export namespace ecs::component {

struct FROZENSTARCRYSTAL_CORE_API SignalHub {
  signals::SignalHub hub;

  template <typename Signature, typename Slot>
  signals::ScopedConnection<Signature>
  connect(std::shared_ptr<signals::Signals<Signature>> signal, Slot &&slot) {
    return hub.connect(signal, std::forward<Slot>(slot));
  }
};

} // namespace ecs::component
