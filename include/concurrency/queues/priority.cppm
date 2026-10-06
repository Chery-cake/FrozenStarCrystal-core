module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:priority;

import std.compat;
import :queue;

namespace concurrency::queues {

// ---- Entry / priority detection -----------------------------
template <typename E>
concept HasPriorityCall = requires(const E &e) {
  { e.priority() };
};
template <typename E>
concept HasPriorityMember = requires(const E &e) { e.priority; };

template <typename E>
concept PriorityEntry = requires(E e, const E ce) {
  { e.task } -> std::same_as<Task &>;
  { ce.task } -> std::same_as<const Task &>;
} && (HasPriorityCall<E> || HasPriorityMember<E>);

template <PriorityEntry E> decltype(auto) priority_of(const E &e) {
  if constexpr (HasPriorityCall<E>) {
    return e.priority();
  } else {
    return (e.priority);
  }
}

template <PriorityEntry E>
using PriorityOf =
    std::remove_cvref_t<decltype(priority_of(std::declval<const E &>()))>;

// ---- Default comparator -------------------------------------------------
template <PriorityEntry E> struct DefaultPriorityCompare {
  bool operator()(const E &a, const E &b) const {
    return priority_of(a) < priority_of(b);
  }
};

// ---- Container / comparator concepts ------------------------------------
template <typename C, typename E>
concept PriorityContainer = requires(C c, const C &cc, E e) {
  typename C::value_type;
  requires std::same_as<typename C::value_type, E>;
  requires std::random_access_iterator<typename C::iterator>;
  { c.push_back(std::move(e)) };
  { c.pop_back() };
  { cc.back() } -> std::same_as<const E &>;
};

template <typename Cmp, typename E>
concept EntryComparator = requires(Cmp cmp, const E &a, const E &b) {
  { cmp(a, b) } -> std::convertible_to<bool>;
};

// ---- Aging infrastructure ----------------------------------------------

// Empty key + no-op compare => zero overhead when aging is disabled.
struct NoAging {
  using Key = std::monostate;

  Key make_key(const auto &) const noexcept { return {}; }
  [[nodiscard]] static bool outranks(const Key &, const Key &) noexcept {
    return false;
  }
};

template <typename A, typename E>
concept AgingPolicy =
    PriorityEntry<E> &&
    requires(A a, const E &e, typename A::Key k1, typename A::Key k2) {
      typename A::Key;
      { a.make_key(e) } -> std::same_as<typename A::Key>;
      { a.outranks(k1, k2) } -> std::convertible_to<bool>;
    };

// Convenience: pull the key type of any policy without naming the struct.
template <typename A> using AgingKeyOf = typename A::Key;

template <typename A> consteval bool active_aging() {
  // 1. Explicit opt-out/opt-in wins if present.
  if constexpr (requires { A::active; }) {
    static_assert(
        std::same_as<std::remove_cvref_t<decltype(A::active)>, bool>,
        "AgingPolicy::active, if declared, must be `static constexpr "
        "bool`. "
        "Other types (int, char, etc.) are rejected so that a mistyped "
        "`active = 0` doesn't get silently treated as active.");
    return A::active;
  }
  // 2. Otherwise NoAging is the implicit opt-out.
  else {
    return !std::same_as<A, NoAging>;
  }
}
template <typename A>
concept ActiveAging = active_aging<A>();

// Wrapper stored inside the container. Key is [[no_unique_address]] so
// NoAging costs nothing.
template <typename Entry, typename Key> struct AgedNode {
  Entry entry;
  [[no_unique_address]] Key key;
};

// Combined comparator: aging key first, user's Compare as tie-break.
template <typename Compare, typename Aging, typename Node> struct NodeCompare {
  [[no_unique_address]] Compare cmp;
  [[no_unique_address]] Aging aging;

  bool operator()(const Node &a, const Node &b) const {
    if constexpr (ActiveAging<Aging>) {
      if (aging.outranks(b.key, a.key)) {
        return true;
      }
      if (aging.outranks(a.key, b.key)) {
        return false;
      }
    }
    return cmp(a.entry, b.entry);
  }
};

} // namespace concurrency::queues

export namespace concurrency::queues {

template <PriorityEntry Entry, template <typename> typename Container,
          EntryComparator<Entry> Compare = DefaultPriorityCompare<Entry>,
          typename Aging = NoAging>
  requires(PriorityContainer<Container<AgedNode<Entry, AgingKeyOf<Aging>>>,
                             AgedNode<Entry, AgingKeyOf<Aging>>> &&
           AgingPolicy<Aging, Entry>)
struct FROZENSTARCRYSTAL_CORE_API Priority {
private:
  using Key = AgingKeyOf<Aging>;
  using Node = AgedNode<Entry, Key>;
  using NCmp = NodeCompare<Compare, Aging, Node>;

  Container<Node> queue;
  [[no_unique_address]] Compare cmp;
  [[no_unique_address]] Aging aging;

  mutable std::mutex mutex;
  std::counting_semaphore<> permits{0};

  std::atomic<size_t> size{0};
  std::atomic<uint64_t> wake{0};

  NCmp nc() const { return NCmp{cmp, aging}; }

public:
  Priority()
    requires std::default_initializable<Compare> &&
                 std::default_initializable<Aging>
  = default;
  Priority(Compare c, Aging a) : cmp(std::move(c)), aging(std::move(a)) {}

  void push(Entry e) {
    auto key = [&]() -> Key {
      if constexpr (ActiveAging<Aging>) {
        return aging.make_key(e);
      } else {
        return Key{};
      }
    }();
    Node node{std::move(e), std::move(key)};
    {
      std::scoped_lock lock(mutex);
      queue.push_back(std::move(node));
      std::ranges::push_heap(queue, nc());
    }
    size.fetch_add(1, std::memory_order_release);
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_one();
    permits.release();
  }

  bool try_pop(Task &t, const std::stop_token &stoken) {
    std::optional<std::stop_callback<std::function<void()>>> cb;
    while (true) {

      // --- 1. Try to grab work, regardless of stop state. ---
      if (size.load(std::memory_order_relaxed) != 0 && permits.try_acquire()) {
        std::unique_lock lock(mutex);
        if (!queue.empty()) {
          std::ranges::pop_heap(queue, nc());
          t = std::move(queue.back().entry.task);
          queue.pop_back();
          lock.unlock();
          size.fetch_sub(1, std::memory_order_release);
          return true;
        }
        lock.unlock();
        permits.release(); // stale permit → hand back
      }

      // --- 2. No work visible. If stopped, do a final drain check
      //        under the lock (size_/permits_ are hints, not truth). ---
      if (stoken.stop_requested()) {
        std::unique_lock lock(mutex);
        if (!queue.empty()) {
          std::ranges::pop_heap(queue, nc());
          t = std::move(queue.back().entry.task);
          queue.pop_back();
          lock.unlock();
          size.fetch_sub(1, std::memory_order_release);
          permits.try_acquire(); // consume the permit the fast path
                                 // missed
          return true;
        }
        return false; // queue truly empty → done
      }

      // --- 3. Slow path: park until a push or a stop. ---
      if (!cb) {
        cb.emplace(stoken, [this] {
          wake.fetch_add(1, std::memory_order_release);
          wake.notify_all();
        });
      }
      uint64_t w = wake.load(std::memory_order_acquire);
      while (size.load(std::memory_order_acquire) == 0 &&
             !stoken.stop_requested()) {
        wake.wait(w, std::memory_order_acquire);
        w = wake.load(std::memory_order_acquire);
      }
      // Loop back: either work appeared (fast path will hit) or stop
      // was requested (drain check will run, then return false).
    }
  }

  void notify_all() { wake.notify_all(); }

  bool empty() { return size.load(std::memory_order_acquire) == 0; }
};

} // namespace concurrency::queues
