#pragma once
#include "resource_registry.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace core {

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
bool ResourceRegistry<Tag, Asset, Policy>::add(
    const Tag *tag, typename Policy::InputType asset) {
  Asset *assetPtr = nullptr;
  std::shared_ptr<Asset> keepAlive;

  if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
    keepAlive = asset;
    assetPtr = keepAlive.get();
  } else {
    assetPtr = Policy::get_ptr(asset);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Policy::add_to_map(assets_, tag, std::move(asset)))
      return false;
  }

  assetAdded_.emit(tag, assetPtr);

  return true;
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
template <typename... Args>
bool ResourceRegistry<Tag, Asset, Policy>::emplace(const Tag *tag,
                                                   Args &&...args) {
  return add(tag, Policy::make_asset(*tag, std::forward<Args>(args)...));
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
bool ResourceRegistry<Tag, Asset, Policy>::set(
    const Tag *tag, typename Policy::InputType asset) {
  Asset *oldPtr = nullptr;
  Asset *newPtr = nullptr;
  bool existed = false;

  std::shared_ptr<Asset> keepOld, keepNew;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    existed = (it != assets_.end());

    if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
      if (existed) {
        keepOld = it->second.lock();
        oldPtr = keepOld.get();
      }

      assets_[tag] = std::move(asset);
      keepNew = assets_[tag].lock();
      newPtr = keepNew.get();
    } else {
      if (existed) {
        oldPtr = Policy::get_ptr(it->second);
      }

      assets_[tag] = std::move(asset);
      newPtr = Policy::get_ptr(assets_[tag]);
    }
  }

  // Invoke remove callbacks first if replacing
  if (oldPtr) {
    assetRemoved_.emit(tag, oldPtr);
  }

  // Then invoke add callbacks
  assetAdded_.emit(tag, newPtr);

  return !existed;
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
typename Policy::ReturnType
ResourceRegistry<Tag, Asset, Policy>::get(const Tag *tag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = assets_.find(tag);
  if (it == assets_.end())
    return nullptr;

  // Return the appropriate type depending on the policy.
  if constexpr (std::is_same_v<Policy, UniquePtrPolicy<Tag, Asset>>) {
    // ReturnType = Asset*
    return Policy::get_ptr(it->second);
  } else if constexpr (std::is_same_v<Policy, SharedPtrPolicy<Tag, Asset>>) {
    // ReturnType = std::shared_ptr<Asset>
    return it->second; // copy the existing shared_ptr
  } else {
    // WeakPtrPolicy: ReturnType = std::shared_ptr<Asset>
    return Policy::get_ptr(it->second); // locks and returns shared_ptr
  }
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
ResourceRegistry<Tag, Asset, Policy>::Entry
ResourceRegistry<Tag, Asset, Policy>::getEntry(const Tag *tag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = assets_.find(tag);
  if (it == assets_.end()) {
    return Entry{nullptr, nullptr};
  }

  if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
    auto shared = it->second.lock();
    return Entry{tag, shared.get()};
  } else {
    return Entry{tag, Policy::get_ptr(it->second)};
  }
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
bool ResourceRegistry<Tag, Asset, Policy>::contains(const Tag *tag) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assets_.find(tag) != assets_.end();
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
bool ResourceRegistry<Tag, Asset, Policy>::remove(const Tag *tag) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = assets_.find(tag);
  if (it == assets_.end()) {
    return false;
  }

  if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
    auto shared = it->second.lock();
    if (shared) {
      assetRemoved_.emit(tag, shared.get());
    }
    assets_.erase(it);
  } else {
    auto extracted = Policy::extract(it->second);
    assetRemoved_.emit(tag, extracted.get());
    assets_.erase(it);
  }

  return true;
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
typename Policy::ExtractType
ResourceRegistry<Tag, Asset, Policy>::extract(const Tag *tag) {
  Asset *assetPtr = nullptr;
  typename Policy::ExtractType result;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(tag);
    if (it != assets_.end()) {
      if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
        result = it->second.lock();
        if (result) {
          assetPtr = result.get();
        }
      } else {
        assetPtr = Policy::get_ptr(it->second);
        result = Policy::extract(it->second);
      }
      assets_.erase(it);
    }
  }

  if (result) {
    assetRemoved_.emit(tag, assetPtr);
  }

  return result;
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
template <typename Func>
void ResourceRegistry<Tag, Asset, Policy>::forEach(Func &&func) const {
  auto entries = getAll();

  std::ranges::for_each(
      entries, [&func](const Entry &entry) { func(entry.tag, entry.asset); });
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
std::vector<typename ResourceRegistry<Tag, Asset, Policy>::Entry>
ResourceRegistry<Tag, Asset, Policy>::getAll() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Entry> entries;
  entries.reserve(assets_.size());

  if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
    std::ranges::transform(assets_, std::back_inserter(entries),
                           [](const auto &pair) {
                             auto shared = pair.second.lock();
                             return Entry{pair.first, shared.get()};
                           });
  } else {
    std::ranges::transform(
        assets_, std::back_inserter(entries), [](const auto &pair) {
          return Entry{pair.first, Policy::get_ptr(pair.second)};
        });
  }
  return entries;
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
void ResourceRegistry<Tag, Asset, Policy>::clear() {
  decltype(assets_) localAssets;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    localAssets.swap(assets_);
  }

  if constexpr (std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>) {
    std::ranges::for_each(localAssets, [&](const auto &pair) {
      auto shared = pair.second.lock();
      if (shared)
        assetRemoved_.emit(pair.first, shared.get());
    });
  } else {
    std::ranges::for_each(localAssets, [&](const auto &pair) {
      assetRemoved_.emit(pair.first, pair.second.get());
    });
  }
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
size_t ResourceRegistry<Tag, Asset, Policy>::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assets_.size();
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
bool ResourceRegistry<Tag, Asset, Policy>::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return assets_.empty();
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
signal::Signal<
    typename ResourceRegistry<Tag, Asset, Policy>::SignalCall>::ConnectResult
ResourceRegistry<Tag, Asset, Policy>::onAdd(SignalSlot signalSlot) {
  return assetAdded_.connect(std::move(signalSlot));
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
signal::Signal<
    typename ResourceRegistry<Tag, Asset, Policy>::SignalCall>::ConnectResult
ResourceRegistry<Tag, Asset, Policy>::onRemove(SignalSlot signalSlot) {
  return assetRemoved_.connect(std::move(signalSlot));
}

template <typename Tag, typename Asset, typename Policy>
  requires OwnerShipPolicy<Tag, Asset, Policy>
void ResourceRegistry<Tag, Asset, Policy>::clearCallbacks() {
  std::lock_guard<std::mutex> lock(mutex_);
  assetAdded_.clear();
  assetRemoved_.clear();
}

} // namespace core
