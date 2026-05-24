module;

export module resource:policy;

import std;

export namespace resource {

template <typename Tag, typename Resource> struct UniquePtrPolicy {
  using StoredType = std::unique_ptr<Resource>;
  using IntermediaryToStore = std::unique_ptr<Resource>;
  using InputType = std::unique_ptr<Resource>;
  using ReturnType = Resource *;
  using ExtractType = std::unique_ptr<Resource>;

  template <typename... Args>
  static IntermediaryToStore make_resource(Args &&...args) {
    return std::make_unique<Resource>(std::forward<Args>(args)...);
  }

  static bool add_to_map(std::unordered_map<const Tag *, StoredType> &map,
                         const Tag *tag, InputType resource) {
    return map.try_emplace(tag, std::move(resource)).second;
  }

  static ReturnType get_ptr(const StoredType &stored) { return stored.get(); }

  static ExtractType extract(StoredType &stored) { return std::move(stored); }
};

template <typename Tag, typename Resource> struct SharedPtrPolicy {
  using StoredType = std::shared_ptr<Resource>;
  using IntermediaryToStore = std::shared_ptr<Resource>;
  using InputType = std::shared_ptr<Resource>;
  using ReturnType = Resource *;
  using ExtractType = std::shared_ptr<Resource>;

  template <typename... Args>
  static IntermediaryToStore make_resource(Args &&...args) {
    return std::make_shared<Resource>(std::forward<Args>(args)...);
  }

  static bool add_to_map(std::unordered_map<const Tag *, StoredType> &map,
                         const Tag *tag, InputType resource) {
    return map.try_emplace(tag, std::move(resource)).second;
  }

  static ReturnType get_ptr(const StoredType &stored) { return stored.get(); }

  static ExtractType extract(StoredType &stored) { return std::move(stored); }
};

template <typename Tag, typename Resource> struct WeakPtrPolicy {
  using StoredType = std::weak_ptr<Resource>;
  using IntermediaryToStore = std::shared_ptr<Resource>;
  using InputType = std::shared_ptr<Resource>;
  using ReturnType = std::shared_ptr<Resource>;
  using ExtractType = std::shared_ptr<Resource>;

  template <typename... Args>
  static IntermediaryToStore make_resource(Args &&...args) {
    return std::make_shared<Resource>(std::forward<Args>(args)...);
  }

  static bool add_to_map(std::unordered_map<const Tag *, StoredType> &map,
                         const Tag *tag, InputType resource) {
    auto it = map.find(tag);
    if (it != map.end()) {
      if (!it->second.expired()) {
        return false;
      }
      map.erase(it);
    }
    map.try_emplace(tag, resource);
    return true;
  }

  static ReturnType get_ptr(const StoredType &stored) { return stored.lock(); }

  static ExtractType extract(StoredType &stored) {
    auto shared = stored.lock();
    stored.reset();
    return shared;
  }
};

template <typename Tag, typename Resource, typename Policy>
concept OwnerShipPolicy =
    std::is_same_v<Policy, UniquePtrPolicy<Tag, Resource>> ||
    std::is_same_v<Policy, SharedPtrPolicy<Tag, Resource>> ||
    std::is_same_v<Policy, WeakPtrPolicy<Tag, Resource>>;

} // namespace resource
