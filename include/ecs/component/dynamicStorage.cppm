module;

export module ecs.component:dynamicStorage;

import std.compat;
import ecs.component.detail;

export namespace ecs::component {

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

} // namespace ecs::component
