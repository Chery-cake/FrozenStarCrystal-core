#pragma once

import std.compat;

namespace ecs::component::detail {

template <typename... Args> struct ComponentDependencies {
  using required = std::tuple<>;
};

/* Example specialisation for Renderable<N>
template <std::size_t N>
struct ComponentDependencies<Renderable<N>> {
using required = std::tuple<Transform<N>>;
};*/

// Check if a type T is present in Pack...
template <typename T, typename... Pack>
constexpr bool is_in_pack_v = (std::same_as<T, Pack> || ...);

// Check that all dependencies of every component in the pack are satisfied
template <typename... Components> constexpr bool all_dependencies_satisfied() {
  // For each component type...
  return ([]<typename Comp> {
    using Reqs = typename ComponentDependencies<Comp>::required;
    // For each required type in the tuple, verify it's in Components...
    auto check_one = []<typename Req>() {
      return is_in_pack_v<Req, Components...>;
    };
    // Use a simple recursive template to iterate tuple types
    return std::apply(
        [check_one](auto... req_types) {
          return (check_one.template operator()<decltype(req_types)>() && ...);
        },
        Reqs{});
  }.template operator()<Components>() &&
          ...);
}

} // namespace ecs::component::detail

namespace ecs::entity {

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

namespace ecs::component {

struct DynamicStorage {
private:
  template <typename Tuple> struct check_requirements;

  template <typename... Reqs> struct check_requirements<std::tuple<Reqs...>> {
    static bool all_exist(const DynamicStorage &store) {
      return (store.has_unsafe<Reqs>() && ...);
    }
  };

  mutable std::shared_mutex mtx_;
  std::unordered_map<std::type_index, std::any> components;

  // single internal add – no locking, called from locked contexts
  template <typename T> void add_impl(T &&value) {
    using Comp = std::decay_t<T>;
    components[typeid(Comp)] = std::forward<T>(value);
  }

  template <typename T> bool has_unsafe() const {
    return components.contains(typeid(T));
  }

  template <typename T> bool meets_dependencies_unsafe() const {
    using Reqs = typename detail::ComponentDependencies<T>::required;
    return check_requirements<Reqs>::all_exist(*this);
  }

public:
  DynamicStorage() = default;

  // --- Query (shared lock) ---

  template <typename T> bool has() const {
    std::shared_lock lock(mtx_);
    return components.contains(typeid(T));
  }

  template <typename T> const T *get() const {
    std::shared_lock lock(mtx_);
    auto it = components.find(typeid(T));
    return (it != components.end()) ? std::any_cast<T>(&it->second) : nullptr;
  }
  template <typename T> T *get() {
    std::shared_lock lock(mtx_);
    auto it = components.find(typeid(T));
    return (it != components.end()) ? std::any_cast<T>(&it->second) : nullptr;
  }

  template <typename T> bool meets_dependencies() const {
    std::shared_lock lock(mtx_);
    return meets_dependencies_unsafe<T>();
  }

  // --- Mutation (exclusive lock) ---

  template <typename T> bool add_check(T &&value) {
    using Comp = std::decay_t<T>;
    std::unique_lock lock(mtx_);
    if (!meets_dependencies_unsafe<Comp>()) // no re-lock
      return false;
    add_impl(std::forward<T>(value));
    return true;
  }

  template <typename T> void add_throw(T &&value) {
    using Comp = std::decay_t<T>;
    std::unique_lock lock(mtx_);
    if (!meets_dependencies_unsafe<Comp>())
      throw std::runtime_error("Missing dependencies");
    add_impl(std::forward<T>(value));
  }

  template <typename T> void add_unchecked(T &&value) {
    std::unique_lock lock(mtx_);
    add_impl(std::forward<T>(value));
  }

  /**@brief
   * If the dependency is missing, throw an error
   */
  template <typename T, typename... Args> T &emplace(Args &&...args) {
    using Comp = std::decay_t<T>;
    std::unique_lock lock(mtx_);
    if (!meets_dependencies_unsafe<Comp>())
      throw std::runtime_error("Missing dependencies");
    auto [it, _] = components.try_emplace(
        typeid(T),
        std::any(std::in_place_type<T>, std::forward<Args>(args)...));
    return std::any_cast<T &>(it->second);
  }

  template <typename T> void remove() {
    std::unique_lock lock(mtx_);
    components.erase(typeid(T));
  }

  void clear() {
    std::unique_lock lock(mtx_);
    components.clear();
  }

  bool erase(const std::type_index &tid) {
    std::unique_lock lock(mtx_);
    return components.erase(tid) > 0;
  }
};

template <typename T, size_t N> struct MultiComponent {
  std::array<T, N> components;

  MultiComponent() = default;

  template <typename... Args>
    requires(sizeof...(Args) == N)
  MultiComponent(Args &&...args) : components(std::forward<Args>(args)...) {}

  T &operator[](size_t i) { return components[i]; }
  const T &operator[](size_t i) const { return components[i]; }

  auto begin() { return components.begin(); }
  auto begin() const { return components.begin(); }

  auto end() { return components.end(); }
  auto end() const { return components.end(); }

  [[nodiscard]] size_t size() const { return N; }
};

template <typename T, size_t N>
struct detail::ComponentDependencies<MultiComponent<T, N>> {
  using required = typename detail::ComponentDependencies<T>::required;
};

template <typename T> struct DynamicMultiComponent {
  std::vector<T> components;

  DynamicMultiComponent() = default;

  DynamicMultiComponent(std::initializer_list<T> list) : components(list) {}

  explicit DynamicMultiComponent(std::vector<T> &&vec)
      : components(std::move(vec)) {}

  void add(const T &value) { components.push_back(value); }
  void add(T &&value) { components.push_back(std::forward<T>(value)); }

  T &operator[](size_t i) { return components[i]; }
  const T &operator[](size_t i) const { return components[i]; }

  auto begin() { return components.begin(); }
  auto begin() const { return components.begin(); }

  auto end() { return components.end(); }
  auto end() const { return components.end(); }

  [[nodiscard]] size_t size() const { return components.size(); }
};

template <typename T>
struct detail::ComponentDependencies<DynamicMultiComponent<T>> {
  using required = typename detail::ComponentDependencies<T>::required;
};

} // namespace ecs::component
