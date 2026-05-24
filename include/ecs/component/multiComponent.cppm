module;

export module ecs.component:multiComponent;

import std.compat;
import ecs.component.detail;

namespace ecs::component {

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
