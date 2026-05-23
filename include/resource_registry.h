#ifndef RESOURCE_REGISTRY_H_
#define RESOURCE_REGISTRY_H_

#include "signal.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace core {

// Default policy – exactly what the current implementation does.
template <typename Tag, typename Asset> struct UniquePtrPolicy {
  using StoredType = std::unique_ptr<Asset>;
  using InputType = std::unique_ptr<Asset>;
  using ReturnType = Asset *;
  using ExtractType = std::unique_ptr<Asset>;

  template <typename... Args> static StoredType make_asset(Args &&...args) {
    return std::make_unique<Asset>(std::forward<Args>(args)...);
  }

  static bool add_to_map(std::unordered_map<const Tag *, StoredType> &map,
                         const Tag *tag, InputType asset) {
    return map.try_emplace(tag, std::move(asset)).second;
  }

  static Asset *get_ptr(const StoredType &stored) { return stored.get(); }

  static ExtractType extract(StoredType &stored) { return std::move(stored); }

  // For iteration: returns a raw pointer that is guaranteed to live
  // during the callback (the map owns it).
  static Asset *view_for_callback(const StoredType &stored) {
    return stored.get();
  }
};

// Shared ptr
template <typename Tag, typename Asset> struct SharedPtrPolicy {
  using StoredType = std::shared_ptr<Asset>;
  using InputType = std::shared_ptr<Asset>;
  using ReturnType = std::shared_ptr<Asset>;
  using ExtractType = std::shared_ptr<Asset>;

  template <typename... Args> static StoredType make_asset(Args &&...args) {
    return std::make_shared<Asset>(std::forward<Args>(args)...);
  }

  static bool add_to_map(std::unordered_map<const Tag *, StoredType> &map,
                         const Tag *tag, InputType asset) {
    return map.try_emplace(tag, std::move(asset)).second;
  }

  static Asset *get_ptr(const StoredType &stored) { return stored.get(); }

  static ExtractType extract(StoredType &stored) { return std::move(stored); }

  // For iteration: returns a raw pointer that is guaranteed to live
  // during the callback (the map owns it).
  static Asset *view_for_callback(const StoredType &stored) {
    return stored.get();
  }
};

// Weak‑ptr cache policy – stores a non‑owning weak_ptr.
template <typename Tag, typename Asset> struct WeakPtrPolicy {
  using StoredType = std::weak_ptr<Asset>;
  using InputType = std::shared_ptr<Asset>;
  using ReturnType = std::shared_ptr<Asset>;
  using ExtractType = std::shared_ptr<Asset>;

  template <typename... Args> static StoredType make_asset(Args &&...args) {
    return std::make_shared<Asset>(std::forward<Args>(args)...);
  }

  static bool add_to_map(std::unordered_map<const Tag *, StoredType> &map,
                         const Tag *tag, InputType asset) {
    auto it = map.find(tag);
    if (it != map.end()) {
      if (!it->second.expired()) {
        return false;
      }
      map.erase(it);
    }
    map.try_emplace(tag, asset);
    return true;
  }

  // `get` returns a shared_ptr – keeps the object alive.
  static std::shared_ptr<Asset> get_ptr(const StoredType &stored) {
    return stored.lock();
  }

  static ExtractType extract(StoredType &stored) {
    auto shared = stored.lock();
    stored.reset(); // invalidate weak reference
    return shared;
  }

  // For iteration: lock and return raw pointer; the caller must ensure
  // the returned shared_ptr lives during the callback.
  // We'll handle that inside the registry's forEach.
  static Asset *view_for_callback(const StoredType &stored) {
    // This is unsafe used alone; the registry must hold a temporary
    // shared_ptr. See forEach implementation below.
    return nullptr;
  }
};

template <typename Tag, typename Asset, typename Policy>
concept OwnerShipPolicy = std::is_same_v<Policy, UniquePtrPolicy<Tag, Asset>> ||
                          std::is_same_v<Policy, SharedPtrPolicy<Tag, Asset>> ||
                          std::is_same_v<Policy, WeakPtrPolicy<Tag, Asset>>;

/**
 * @brief A combined registry for type-safe asset management with metadata-rich
 * tags.
 *
 * This template class combines the benefits of:
 * - Type-safe tag pointers (compile-time safety, no typos)
 * - Tag structs with metadata (name, path, custom fields)
 * - Asset storage with automatic access to tag metadata
 * - Callbacks for add/remove events
 *
 * @warning Tag instances MUST have static storage duration (constexpr, static,
 *          or global). Using local/temporary tag instances will cause undefined
 *          behavior as their addresses may change between calls.
 *
 * Example usage:
 * @code
 *   // Step 1: Define a tag type with metadata
 *   struct TextureTag {
 *     const char *name;
 *     const char *path;
 *     int width;
 *     int height;
 *
 *     constexpr TextureTag(const char* n, const char* p, int w, int h)
 *       : name(n), path(n), width(w), height(h) {}
 *   };
 *
 *   // Step 2: Define constexpr tag instances with their metadata
 *   constexpr TextureTag GRASS_TEXTURE{"grass", "textures/grass.png", 512,
 *   512};
 *   constexpr TextureTag WATER_TEXTURE{"water", "textures/water.png",
 *   1024, 1024};
 *
 *   // Step 3: Define your asset class
 *   class Texture {
 *   public:
 *     // Can use tag metadata during construction!
 *     explicit Texture(const TextureTag& tag)
 *       : _path(tag.path), _width(tag.width), _height(tag.height) {
 *       // Load texture from tag.path with dimensions tag.width x tag.height
 *     }
 *   private:
 *     std::string _path;
 *     int _width, _height;
 *   };
 *
 *   // Step 4: Create registry and use it
 *   core::ResourceRegistry<TextureTag, Texture> textures;
 *
 *   // Add assets - the tag provides all metadata!
 *   textures.add(&GRASS_TEXTURE, std::make_unique<Texture>(GRASS_TEXTURE));
 *
 *   // Or use the convenience method that creates from tag
 *   textures.emplace(&WATER_TEXTURE);  // Constructs Texture(WATER_TEXTURE)
 *
 *   // Get asset and access tag metadata
 *   if (auto* entry = textures.getEntry(&GRASS_TEXTURE)) {
 *     std::cout << "Loaded: " << entry->tag->name << " from " <<
 *     entry->tag->path;
 *   }
 *
 *   // Iterate over all entries
 *   textures.forEach([](const TextureTag* tag, Texture* asset) {
 *     std::cout << tag->name << ": " << tag->path << std::endl;
 *   });
 *
 *   // Register callbacks
 *   textures.onAdd([](const TextureTag* tag, Texture* asset) {
 *     std::cout << "Added texture: " << tag->name << std::endl;
 *   });
 * @endcode
 *
 * @tparam Tag The tag struct type (should inherit from ResourceTag or have
 *             similar interface)
 * @tparam Asset The asset type to store
 */
template <typename Tag, typename Asset,
          typename Policy = UniquePtrPolicy<Tag, Asset>>
  requires OwnerShipPolicy<Tag, Asset, Policy>
class ResourceRegistry {
private:
  using SignalCall = void(const Tag *,
                          Asset *); // TODO possible improvement type per policy
  using SignalSlot = std::move_only_function<SignalCall>;

  std::unordered_map<const Tag *, typename Policy::StoredType> assets_;
  signal::Signal<SignalCall> assetAdded_;
  signal::Signal<SignalCall> assetRemoved_;
  mutable std::mutex mutex_;

public:
  /**
   * @brief Entry containing both the tag pointer and asset pointer
   */
  struct Entry {
    const Tag *tag;
    Asset *asset;
  };

  ResourceRegistry() = default;
  ~ResourceRegistry() = default;

  // Delete copy and move operations (mutex is not movable)
  ResourceRegistry(const ResourceRegistry &) = delete;
  ResourceRegistry &operator=(const ResourceRegistry &) = delete;
  ResourceRegistry(ResourceRegistry &&) = delete;
  ResourceRegistry &operator=(ResourceRegistry &&) = delete;

  /**
   * @brief Add an asset to the registry under the given tag
   * @param tag Pointer to the tag instance (contains metadata)
   * @param asset Unique pointer to the asset (ownership transferred)
   * @return true if added, false if tag already exists
   */
  bool add(const Tag *tag, typename Policy::InputType asset);

  /**
   * @brief Construct and add an asset using the tag's metadata
   * @param tag Pointer to the tag instance
   * @param args Additional arguments to forward to Asset constructor after
   * tag
   * @return true if added, false if tag already exists
   */
  template <typename... Args> bool emplace(const Tag *tag, Args &&...args);

  /**
   * @brief Replace an existing asset or add if not present
   * @param tag Pointer to the tag instance
   * @param asset Unique pointer to the asset
   * @return true if a new asset was added, false if an existing asset was
   * replaced
   *
   * @note Add callbacks are invoked only when a new entry is added.
   *       When replacing, remove callbacks are invoked first, then add
   *       callbacks.
   */
  bool set(const Tag *tag, typename Policy::InputType asset);

  /**
   * @brief Get an asset by tag
   * @param tag Pointer to the tag instance
   * @return Pointer to the asset, or nullptr if not found
   */
  typename Policy::ReturnType get(const Tag *tag) const;

  /**
   * @brief Get both the tag and asset as an Entry
   * @param tag Pointer to the tag instance
   * @return Entry with tag and asset pointers, or {nullptr, nullptr} if not
   * found
   */
  Entry getEntry(const Tag *tag) const;

  /**
   * @brief Check if an asset exists for the given tag
   * @param tag Pointer to the tag instance
   * @return true if asset exists
   */
  bool contains(const Tag *tag) const;

  /**
   * @brief Remove an asset from the registry
   * @param tag Pointer to the tag instance
   * @return true if removed, false if tag didn't exist
   */
  bool remove(const Tag *tag);

  /**
   * @brief Remove and return an asset from the registry
   * @param tag Pointer to the tag instance
   * @return Unique pointer to the asset, or nullptr if not found
   */
  typename Policy::ExtractType extract(const Tag *tag);

  /**
   * @brief Iterate over all entries (tag + asset pairs)
   * @param func Function to call for each entry: void(const Tag*, Asset*)
   *
   * @warning The mutex is held for the entire duration of iteration.
   *          Do NOT call other registry methods from within the callback,
   *          as this will cause a deadlock. Use getAll() if you need to
   *          modify the registry during iteration.
   */
  template <typename Func> void forEach(Func &&func) const;

  /**
   * @brief Get all entries as a vector (for iteration outside lock)
   * @return Vector of Entry structs
   */
  std::vector<Entry> getAll() const;

  /**
   * @brief Clear all assets from the registry
   */
  void clear();

  /**
   * @brief Get the number of assets in the registry
   * @return Number of registered assets
   */
  size_t size() const;

  /**
   * @brief Check if the registry is empty
   * @return true if no assets registered
   */
  bool empty() const;

  /**
   * @brief Register a callback for when assets are added
   * @param callback Function to call: void(const Tag*, Asset*)
   */
  signal::Signal<SignalCall>::ConnectResult onAdd(SignalSlot signalSlot);

  /**
   * @brief Register a callback for when assets are removed
   * @param callback Function to call: void(const Tag*)
   */
  signal::Signal<SignalCall>::ConnectResult onRemove(SignalSlot signalSlot);

  /**
   * @brief Clear all callbacks
   */
  void clearCallbacks();
};
}; // namespace core

// template implementation
#include "../src/resource_registry_impl.hpp"

#endif // RESOURCE_REGISTRY_H_
