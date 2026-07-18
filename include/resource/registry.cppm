module;

#include "FrozenStarCrystal-core_export.h"

export module resource:registry;

import std.compat;
import :policy;
import signals;

export namespace resource {

template <typename Tag, typename Resource, typename Policy>
    requires OwnerShipPolicy<Tag, Resource, Policy>
class FROZENSTARCRYSTAL_CORE_API Registry {
  public:
    using SignalCall = void(const Tag *, typename Policy::ReturnType);
    using SignalSlot = std::move_only_function<SignalCall>;

    struct Entry {
        const Tag *tag;
        typename Policy::ReturnType resource;

        constexpr auto operator<=>(const Entry &) const noexcept = default;
    };

  private:
    std::unordered_map<const Tag *, typename Policy::StoredType> resources_;

    signals::Signals<SignalCall> resourceAdded_;
    signals::Signals<SignalCall> resourceRemoved_;

    mutable std::mutex mutex_;

  public:
    Registry() = default;
    virtual ~Registry() = default;

    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;
    Registry(Registry &&) = delete;
    Registry &operator=(Registry &&) = delete;

    bool add(const Tag *tag, typename Policy::InputType resource);

    template <typename... Args>
        requires(!std::is_same_v<Policy, WeakPtrPolicy<Tag, Resource>>)
    bool emplace(const Tag *tag, Args &&...args);

    bool set(const Tag *tag, typename Policy::InputType resource);

    typename Policy::ReturnType get(const Tag *tag) const;

    template <typename = void>
        requires(!std::is_same_v<Policy, UniquePtrPolicy<Tag, Resource>>)
    typename Policy::StoredType getStored(const Tag *tag) const;

    Entry getEntry(const Tag *tag) const;

    bool contains(const Tag *tag) const;

    bool remove(const Tag *tag);

    typename Policy::ExtractType extract(const Tag *tag);

    template <typename Func>
        requires std::is_invocable_v<Func, Tag *, typename Policy::ReturnType>
    void forEach(Func &&func) const;

    std::vector<Entry> getAll() const;

    void clear();

    size_t size() const;

    bool empty() const;

    signals::ConnectionResult onAdd(SignalSlot slot);

    signals::ConnectionResult onRemove(SignalSlot slot);

    void clearSignals();
};

} // namespace resource
