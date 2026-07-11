module;

#include "FrozenStarCrystal-core_export.h"

export module ecs.entity:entities;

import std.compat;
import ecs.component;

export namespace ecs::entity {

template <typename... Components> class FROZENSTARCRYSTAL_CORE_API Tuple {
  static_assert(
      component::all_dependencies_satisfied<Components...>(),
      "Tuple entity is missing one or more required component dependencies.");

public:
  Tuple()
    requires(std::is_default_constructible_v<Components> && ...)
  = default;

  template <typename... Args>
    requires(sizeof...(Args) == sizeof...(Components))
  Tuple(Args &&...args);

  template <typename T>
    requires(std::same_as<T, Components> || ...)
  T &get();

  template <typename T>
    requires(std::same_as<T, Components> || ...)
  const T &get() const;

private:
  std::tuple<Components...> components_;
};

//////////////////////////////////////

template <typename Derived, typename... Components>
class FROZENSTARCRYSTAL_CORE_API Linear;

template <typename Derived, typename First, typename... Rest>
class Linear<Derived, First, Rest...> : public First,
                                        public Linear<Derived, Rest...> {
  static_assert(component::all_dependencies_satisfied<First, Rest...>(),
                "Linear entity is missing required component dependencies.");

public:
  Linear()
    requires(std::is_default_constructible_v<First> && ... &&
             std::is_default_constructible_v<Rest>)
  = default;

  template <typename F, typename... R> Linear(F &&first, R &&...rest);

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  T &get();

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  const T &get() const;
};

template <typename Derived> class Linear<Derived> {};

//////////////////////////////////////

template <typename Derived, typename... Components>
class FROZENSTARCRYSTAL_CORE_API Virtual;

template <typename Derived, typename First, typename... Rest>
class Virtual<Derived, First, Rest...>
    : public virtual First, public virtual Virtual<Derived, Rest...> {
  static_assert(component::all_dependencies_satisfied<First, Rest...>(),
                "Virtual entity is missing required component dependencies.");

public:
  Virtual()
    requires(std::is_default_constructible_v<First> && ... &&
             std::is_default_constructible_v<Rest>)
  = default;

  template <typename F, typename... R> Virtual(F &&first, R &&...rest);

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  T &get();

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  const T &get() const;
};

template <typename Derived> class Virtual<Derived> {};

} // namespace ecs::entity
