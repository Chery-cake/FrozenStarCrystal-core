module;

export module concurrency_test.queue_types;

export import std.compat;
export import concurrency;

export namespace ct {

using concurrency::queues::Task;

// ─── Fifo / Loop entries ─────────────────────────────────────────────

struct SimplePayload {
  int value = 0;
  auto operator<=>(const SimplePayload &) const = default;
};

// Payload-carrying entry usable by Fifo and Loop. No static make —
// those queues brace-init from (Task) and (Task, Payload).
struct SimplePayloadEntry {
  Task task;
  SimplePayload p;
  using Payload = SimplePayload;
  SimplePayload payload() const { return p; }

  SimplePayloadEntry(Task t) : task(std::move(t)) {}
  SimplePayloadEntry(Task t, SimplePayload pl)
      : task(std::move(t)), p(std::move(pl)) {}
};

// Loop entry: invokes the task once and then retires. Satisfies
// RetireableLoopEntry, so it's SchedulableQueue-capable.
struct OneShotLoopEntry {
  Task task;
  bool retire_flag = false;

  struct Transient {};
  OneShotLoopEntry(Task t) : task(std::move(t)) {}
  OneShotLoopEntry(Transient, Task t) : task(std::move(t)), retire_flag(true) {}

  void invoke() { task(); }
  bool should_retire() const { return retire_flag; }
};

// Loop entry with payload: retires after one call.
struct PayloadLoopEntry {
  Task task;
  SimplePayload p;
  bool retire_flag = false;

  using Payload = SimplePayload;
  struct Transient {};

  SimplePayload payload() const { return p; }

  PayloadLoopEntry(Task t) : task(std::move(t)) {}
  PayloadLoopEntry(Task t, SimplePayload pl)
      : task(std::move(t)), p(std::move(pl)) {}
  PayloadLoopEntry(Transient, Task t) : task(std::move(t)), retire_flag(true) {}
  PayloadLoopEntry(Transient, Task t, SimplePayload pl)
      : task(std::move(t)), p(std::move(pl)), retire_flag(true) {}

  void invoke() { task(); }
  bool should_retire() const { return retire_flag; }
};

// ─── Priority entries ─────────────────────────────────────────────────

// No payload. Static make so DefaultPriorityPolicy picks it up.
struct PriorityEntry {
  Task task;
  int priority = 0;

  static PriorityEntry make(Task t) { return {std::move(t), 0}; }
};

using BasicPriorityQueue = concurrency::queues::Priority<PriorityEntry>;

// With payload.
struct PriorityPayload {
  int priority = 0;
  int data = 0;
  auto operator<=>(const PriorityPayload &) const = default;
};

struct PayloadPriorityEntry {
  Task task;
  PriorityPayload p;
  using Payload = PriorityPayload;

  PriorityPayload payload() const { return p; }
  int priority() const { return p.priority; }

  static PayloadPriorityEntry make(Task t) { return {std::move(t), {}}; }
  static PayloadPriorityEntry make(Task t, PriorityPayload pl) {
    return {std::move(t), pl};
  }
};

using PayloadPriorityQueue =
    concurrency::queues::Priority<PayloadPriorityEntry>;

// ─── Aging variants ──────────────────────────────────────────────────

// Deadline-based aging. `now` is a live reference the test controls.
struct DeadlineAging {
  using Score = std::tuple<int64_t, int>;
  std::reference_wrapper<int64_t> now;

  auto score(const PriorityEntry &e) const {
    const int64_t deadline = now.get() + (11 - e.priority);
    return std::tuple{-deadline, e.priority};
  }

  PriorityEntry make(Task t) { return PriorityEntry::make(std::move(t)); }
};

using DeadlinePriorityQueue =
    concurrency::queues::Priority<PriorityEntry, std::vector, DeadlineAging>;

// Time-since-arrival aging. Uses a global clock so tests can advance it
// and call reprioritize_all(). AddressableReprioritize so tests can
// reprioritize a single handle.
inline std::atomic<int64_t> g_aging_now{0};

struct TimedEntry {
  Task task;
  int priority = 0;
  int64_t arrival = 0;

  static TimedEntry make(Task t) { return {std::move(t), 0, 0}; }
};

struct TimeAgingScore {
  using Score = int;

  int score(const TimedEntry &e) const {
    const int64_t waited =
        g_aging_now.load(std::memory_order_relaxed) - e.arrival;
    return e.priority + static_cast<int>(waited / 10);
  }

  TimedEntry make(Task t) { return TimedEntry::make(std::move(t)); }
};

using TimedPriorityQueue =
    concurrency::queues::Priority<TimedEntry, std::vector, TimeAgingScore,
                                  concurrency::queues::AddressableReprioritize>;

// ─── Score-on-a-custom-class variant ─────────────────────────────────

struct CustomPriority {
  int value = 0;
  auto operator<=>(const CustomPriority &) const = default;
};

struct CustomEntry {
  Task task;
  CustomPriority priority;

  static CustomEntry make(Task t) { return {std::move(t), {0}}; }
};

using CustomPriorityQueue = concurrency::queues::Priority<CustomEntry>;

// ─── Convenience aliases ─────────────────────────────────────────────

using FifoTask = concurrency::queues::Fifo<Task>;
using FifoPayload = concurrency::queues::Fifo<SimplePayloadEntry>;
using LoopTask = concurrency::queues::Loop<Task>;
using LoopOneShot = concurrency::queues::Loop<OneShotLoopEntry>;
using LoopPayload = concurrency::queues::Loop<PayloadLoopEntry>;

} // namespace ct
