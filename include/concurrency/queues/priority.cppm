module;

#include "FrozenStarCrystal-core_export.h"

export module concurrency.queues:priority;

import std.compat;
import :queue;

export namespace concurrency::queues {

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

// Score: a callable that produces a totally ordered value
template <typename T>
concept LessThanComparable = requires(const T &a, const T &b) {
  { a < b } -> std::convertible_to<bool>;
};

template <typename P, typename Entry>
concept PriorityPolicy =
    PriorityEntry<Entry> && requires(const P cp, const Entry &e) {
      typename P::Score;
      { cp.score(e) } -> LessThanComparable;
    };

// Default: score is the entry's own priority
template <PriorityEntry Entry>
  requires(
      requires(Task t) { Entry::make(std::move(t)); } ||
      requires(typename Entry::Payload p, Task t) {
        Entry::make(std::move(t), std::move(p));
      })
struct DefaultPriorityPolicy {
  using Score = PriorityOf<Entry>;

  Score score(const Entry &e) const { return priority_of(e); }

  Entry make(Task t)
    requires requires(Task t2) { Entry::make(std::move(t2)); }
  {
    return Entry::make(std::move(t));
  }

  template <typename E = Entry>
    requires requires { typename E::Payload; }
  Entry make(Task t, typename E::Payload p)
    requires requires(Task t2, typename E::Payload p2) {
      Entry::make(std::move(t2), std::move(p2));
    }
  {
    return Entry::make(std::move(t), std::move(p));
  }
};

// Node
struct NoReprioritize {};
struct AddressableReprioritize {};

template <typename R>
concept ReprioritizePolicy =
    std::same_as<R, NoReprioritize> || std::same_as<R, AddressableReprioritize>;

template <bool A> struct NodeIndex;
template <> struct NodeIndex<false> {}; // empty
template <> struct NodeIndex<true> {
  size_t value = 0;
};

template <typename Entry, typename ScoreT, ReprioritizePolicy R>
struct ScoredNode {
  Entry entry;
  [[no_unique_address]] ScoreT score;
  [[no_unique_address]] NodeIndex<std::same_as<R, AddressableReprioritize>>
      index;
};

template <typename Node, ReprioritizePolicy R>
using StorageFor = std::conditional_t<std::same_as<R, AddressableReprioritize>,
                                      std::shared_ptr<Node>, Node>;

template <typename Node, ReprioritizePolicy R>
using HandleFor = std::conditional_t<std::same_as<R, AddressableReprioritize>,
                                     std::shared_ptr<Node>, std::monostate>;

// Aliases the class body can reuse
template <typename Entry, typename P, ReprioritizePolicy R>
using NodeFor = ScoredNode<Entry, typename P::Score, R>;

template <typename Entry, typename P, ReprioritizePolicy R>
using StoredFor = StorageFor<NodeFor<Entry, P, R>, R>;

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

template <PriorityEntry Entry,
          template <typename> typename Container = std::vector,
          PriorityPolicy<Entry> Policy = DefaultPriorityPolicy<Entry>,
          ReprioritizePolicy R = NoReprioritize>
  requires(PriorityContainer<Container<StoredFor<Entry, Policy, R>>,
                             StoredFor<Entry, Policy, R>>)
struct FROZENSTARCRYSTAL_CORE_API Priority {
public:
  using Handle = HandleFor<NodeFor<Entry, Policy, R>, R>;

private:
  static constexpr bool kAddressable = std::same_as<R, AddressableReprioritize>;

  using ScoreT = typename Policy::Score;
  using Node = NodeFor<Entry, Policy, R>;
  using Stored = StoredFor<Entry, Policy, R>;

  Container<Stored> queue;
  [[no_unique_address]] Policy policy;

  mutable std::mutex mutex;
  std::counting_semaphore<> permits{0};

  std::atomic<size_t> size{0};
  std::atomic<uint64_t> wake{0};

  // Uniform access to a Node out of a Stored
  static Node &unwrap(Stored &s) {
    if constexpr (kAddressable) {
      return *s;
    } else {
      return s;
    }
  }
  static const Node &unwrap(const Stored &s) {
    if constexpr (kAddressable) {
      return *s;
    } else {
      return s;
    }
  }
  static void set_heap_index(Stored &s, size_t i) {
    if constexpr (kAddressable) {
      s->index.value = i;
    }
  }

  static constexpr size_t kPopped = std::numeric_limits<size_t>::max();

  struct Less {
    bool operator()(const Stored &a, const Stored &b) const {
      return unwrap(a).score < unwrap(b).score;
    }
  };

  void sift_up(size_t i) {
    while (i > 0) {
      size_t parent = (i - 1) / 2;
      if (!Less{}(queue[parent], queue[i])) {
        break;
      }
      std::swap(queue[parent], queue[i]);
      set_heap_index(queue[parent], parent);
      set_heap_index(queue[i], i);
      i = parent;
    }
  }

  void sift_down(size_t i) {
    const size_t n = queue.size();
    while (true) {

      auto canditates = std::array{i, (2 * i) + 1, (2 * i) + 2} |
                        std::views::filter([n](size_t k) { return k < n; });

      size_t largest = *std::ranges::max_element(
          canditates, Less{},
          [&q = queue](size_t k) -> const Stored & { return q[k]; });

      if (largest == i) {
        break;
      }

      std::swap(queue[i], queue[largest]);
      set_heap_index(queue[i], i);
      set_heap_index(queue[largest], largest);
      i = largest;
    }
  }

  void pop_top(Task &out) {
    Stored top = std::move(queue.front());
    if constexpr (kAddressable) {
      top->index.value = kPopped;
    }

    queue.front() = std::move(queue.back());
    queue.pop_back();

    if (!queue.empty()) {
      set_heap_index(queue.front(), 0);
      sift_down(0);
    }

    out = to_task(std::move(unwrap(top).entry));
  }

  void signal_push() {
    size.fetch_add(1, std::memory_order_release);
    wake.fetch_add(1, std::memory_order_release);
    wake.notify_one();
    permits.release();
  }

public:
  Priority()
    requires std::default_initializable<Policy>
  = default;
  Priority(Policy p) : policy(std::move(p)) {}

  std::conditional_t<kAddressable, Handle, void> push(Entry e) {
    auto s = policy.score(e);
    Node node{.entry = std::move(e), .score = std::move(s), .index = {}};

    if constexpr (kAddressable) {
      auto stored = std::make_shared<Node>(std::move(node));
      {
        std::scoped_lock lock(mutex);
        stored->index.value = queue.size();
        queue.push_back(stored);
        sift_up(queue.size() - 1);
      }
      signal_push();
      return Handle{std::move(stored)};
    } else {
      {
        std::scoped_lock lock(mutex);
        queue.push_back(std::move(node));
        sift_up(queue.size() - 1);
      }
      signal_push();
    }
  }

  template <bool Enabled = kAddressable>
    requires(Enabled)
  bool reprioritize(const Handle &h) {
    if (!h) {
      return false;
    }
    Node &node = *h;

    std::scoped_lock lock(mutex);

    auto new_score = policy.score(node.entry);
    if (!(node.score < new_score) && !(new_score < node.score)) {
      return true;
    }

    size_t i = node.index.value;
    if (i >= queue.size() || queue[i].get() != &node) {
      return false;
    }

    node.score = std::move(new_score);
    sift_up(i);
    sift_down(node.index.value);
    return true;
  }

  template <bool Enabled = kAddressable>
    requires(Enabled)
  void reprioritize_all() {
    std::scoped_lock lock(mutex);

    std::ranges::for_each(queue, [this](Stored &s) {
      unwrap(s).score = policy.score(unwrap(s).entry);
    });

    // Rebuild bottom-up: for each internal node, sift down. O(n).
    std::ranges::for_each(std::views::iota(size_t{0}, queue.size() / 2) |
                              std::views::reverse,
                          [this](size_t i) { sift_down(i); });
  }

  bool try_pop(Task &t, const std::stop_token &stoken) {
    std::optional<std::stop_callback<std::function<void()>>> cb;
    while (true) {

      // --- 1. Try to grab work, regardless of stop state. ---
      if (size.load(std::memory_order_relaxed) != 0 && permits.try_acquire()) {
        std::unique_lock lock(mutex);
        if (!queue.empty()) {
          pop_top(t);
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
          pop_top(t);
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

  Entry make(Task t)
    requires requires(Policy &p, Task t2) { p.make(std::move(t2)); }
  {
    return policy.make(std::move(t));
  }

  template <typename E = Entry>
    requires requires { typename E::Payload; }
  Entry make(Task t, typename E::Payload p)
    requires requires(Policy &p1, Task t2, typename E::Payload p2) {
      p1.make(std::move(t2), std::move(p2));
    }
  {
    return policy.make(std::move(t), std::move(p));
  }
};

} // namespace concurrency::queues
