module;

export module ecs.entity;

import std.compat;
import ecs.component.detail;

export namespace ecs::entity {

template <typename... Components> class Tuple {
  static_assert(
      component::detail::all_dependencies_satisfied<Components...>(),
      "Tuple entity is missing one or more required component dependencies.");

public:
  template <typename... Args>
    requires(sizeof...(Args) == sizeof...(Components))
  Tuple(Args &&...args);

  Tuple()
    requires(std::is_default_constructible_v<Components> && ...)
  = default;

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

template <typename Derived, typename... Components> class Linear;

template <typename Derived, typename First, typename... Rest>
class Linear<Derived, First, Rest...> : public First,
                                        public Linear<Derived, Rest...> {
  static_assert(component::detail::all_dependencies_satisfied<First, Rest...>(),
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

template <typename Derived, typename... Components> class Virtual;

template <typename Derived, typename First, typename... Rest>
class Virtual<Derived, First, Rest...>
    : public virtual First, public virtual Virtual<Derived, Rest...> {
  static_assert(component::detail::all_dependencies_satisfied<First, Rest...>(),
                "Virtual entity is missing required component dependencies.");

public:
  template <typename F, typename... R> Virtual(F &&first, R &&...rest);

  Virtual()
    requires(std::is_default_constructible_v<First> && ... &&
             std::is_default_constructible_v<Rest>)
  = default;

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  T &get();

  template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
  const T &get() const;
};

template <typename Derived> class Virtual<Derived> {};

////////////////////////////////////////////////////

// Primary template: two different template-template parameters → false
template <template <typename...> class A, template <typename...> class B>
struct is_same_template : std::false_type {};

// Specialisation: same parameter → true
template <template <typename...> class A>
struct is_same_template<A, A> : std::true_type {};

// Convenience variable template
template <template <typename...> class A, template <typename...> class B>
inline constexpr bool is_same_template_v = is_same_template<A, B>::value;

template <template <typename...> class Entity>
concept IsEntityTemplate =
    is_same_template_v<Entity, Tuple> || is_same_template_v<Entity, Linear> ||
    is_same_template_v<Entity, Virtual>;

template <template <typename, typename...> class Entity>
concept IsEntityTuple = is_same_template_v<Entity, Tuple>;

} // namespace ecs::entity
