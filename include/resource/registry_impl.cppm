module;

export module resource:registry_impl;

import std.compat;
import :registry;
import signals;

export namespace resource {

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
bool Registry<Tag, Resource, Policy>::add(const Tag *tag,
                                          typename Policy::InputType resource) {
  typename Policy::ReturnType ptr;

  ptr = Policy::get_ptr(resource);

  {
    std::unique_lock lock(mutex_);
    if (!Policy::add_to_map(resources_, tag, std::move(resource))) {
      return false;
    }
  }

  resourceAdded_.emit(tag, ptr);

  return true;
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
template <typename... Args>
bool Registry<Tag, Resource, Policy>::emplace(const Tag *tag, Args &&...args) {
  return add(tag, Policy::make_resource(*tag, std::forward<Args>(args)...));
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
bool Registry<Tag, Resource, Policy>::set(const Tag *tag,
                                          typename Policy::InputType resource) {

  typename Policy::ReturnType oldPtr;
  typename Policy::ReturnType newPtr;
  typename Policy::StoredType old;
  bool existed = false;

  {
    std::unique_lock lock(mutex_);
    auto it = resources_.find(tag);
    existed = (it != resources_.end());

    if (existed) {
      old = std::move(it->second);
      oldPtr = Policy::get_ptr(it->second);
      resources_.erase(it);
    }

    auto [newIt, inserted] = resources_.try_emplace(tag, std::move(resource));
    newPtr = Policy::get_ptr(newIt->second);
  }

  if (oldPtr) {
    resourceRemoved_.emit(tag, oldPtr);
  }

  resourceAdded_.emit(tag, newPtr);

  return !existed;
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
typename Policy::ReturnType
Registry<Tag, Resource, Policy>::get(const Tag *tag) const {
  std::unique_lock lock(mutex_);
  auto it = resources_.find(tag);
  if (it == resources_.end()) {
    return nullptr;
  }

  return Policy::get_ptr(it->second);
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
Registry<Tag, Resource, Policy>::Entry
Registry<Tag, Resource, Policy>::getEntry(const Tag *tag) const {
  std::unique_lock lock(mutex_);
  auto it = resources_.find(tag);
  if (it == resources_.end()) {
    return Entry();
  }

  return Entry{it->first, Policy::get_ptr(it->second)};
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
bool Registry<Tag, Resource, Policy>::contains(const Tag *tag) const {
  std::unique_lock lock(mutex_);
  return resources_.find(tag) != resources_.end();
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
bool Registry<Tag, Resource, Policy>::remove(const Tag *tag) {
  typename Policy::StoredType keepPtr;

  {
    std::unique_lock lock(mutex_);

    auto it = resources_.find(tag);
    if (it == resources_.end()) {
      return false;
    }
    keepPtr = std::move(it->second);
    resources_.erase(it);
  }

  auto ptr = Policy::get_ptr(keepPtr);
  resourceRemoved_.emit(tag, ptr);

  return true;
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
typename Policy::ExtractType
Registry<Tag, Resource, Policy>::extract(const Tag *tag) {
  typename Policy::ReturnType ptr;
  typename Policy::ExtractType result;

  {
    std::unique_lock lock(mutex_);

    auto it = resources_.find(tag);
    if (it != resources_.end()) {
      ptr = Policy::get_ptr(it->second);
      result = Policy::extract(it->second);
      resources_.erase(it);
    }
  }

  if (result) {
    resourceRemoved_.emit(tag, ptr);
  }

  return result;
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
template <typename Func>
  requires std::is_invocable_v<Func, Tag *, typename Policy::StoredType>
void Registry<Tag, Resource, Policy>::forEach(Func &&func) const {
  auto entries = getAll();

  std::ranges::for_each(entries, [&func](const Entry &entry) {
    func(entry.tag, entry.resource);
  });
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
std::vector<typename Registry<Tag, Resource, Policy>::Entry>
Registry<Tag, Resource, Policy>::getAll() const {
  std::unique_lock lock(mutex_);

  std::vector<Entry> entries;
  entries.reserve(resources_.size());

  std::ranges::transform(
      resources_, std::back_inserter(entries), [](const auto &pair) {
        return Entry{pair.first, Policy::get_ptr(pair.second)};
      });

  return entries;
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
void Registry<Tag, Resource, Policy>::clear() {

  decltype(resources_) localResources;

  {
    std::unique_lock lock(mutex_);
    localResources.swap(resources_);
  }

  std::ranges::for_each(
      localResources, [&removed = resourceRemoved_](const auto &pair) {
        removed.emit(pair.first, Policy::get_ptr(pair.second));
      });
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
size_t Registry<Tag, Resource, Policy>::size() const {
  std::unique_lock lock(mutex_);
  return resources_.size();
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
bool Registry<Tag, Resource, Policy>::empty() const {
  std::unique_lock lock(mutex_);
  return resources_.empty();
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
signals::ConnectionResult
Registry<Tag, Resource, Policy>::onAdd(SignalSlot slot) {
  std::unique_lock lock(mutex_);
  return resourceAdded_.connect(std::move(slot));
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
signals::ConnectionResult
Registry<Tag, Resource, Policy>::onRemove(SignalSlot slot) {
  std::unique_lock lock(mutex_);
  return resourceRemoved_.connect(std::move(slot));
}

template <typename Tag, typename Resource, typename Policy>
  requires OwnerShipPolicy<Tag, Resource, Policy>
void Registry<Tag, Resource, Policy>::clearSignals() {
  std::unique_lock lock(mutex_);
  resourceAdded_.clear();
  resourceRemoved_.clear();
}

} // namespace resource
