#include <cassert>
import concurrency_helper;

constexpr concurrency::pool::Pool p1("p1");
constexpr concurrency::pool::Pool p2("p2");
constexpr concurrency::pool::Pool p3("p3");

using Fifo = concurrency::queues::Fifo;
using Loop = concurrency::queues::Loop;
using Task = concurrency::queues::Task;
using B = concurrency::queues::Behaviour;

// ─── Generic per-kind suite ────────────────────────────────────────────

template <typename TQ, typename Pushed, B Behaviour>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_create() {
  TEST("create");

  concurrency::pool::Manager m;

  assert((m.createPool<TQ, Pushed, Behaviour>(&p1)));
  assert((!m.createPool<TQ, Pushed, Behaviour>(&p1)));

  auto pool1 = m.getPool<TQ, Pushed, Behaviour>(&p1);
  assert(pool1);
  assert(pool1->size() == std::thread::hardware_concurrency());

  assert((m.createPool<TQ, Pushed, Behaviour>(&p2, 5)));
  assert((m.getPool<TQ, Pushed, Behaviour>(&p2)->size() == 5));

  assert(m.removePool(&p2));
  assert(!m.removePool(&p2));

  assert((m.split<TQ, Pushed, Behaviour>(&p1, &p2, 5)));
  assert((m.getPool<TQ, Pushed, Behaviour>(&p1)->size() ==
          (std::thread::hardware_concurrency() - 5)));
  assert((m.getPool<TQ, Pushed, Behaviour>(&p2)->size() == 5));

  // Cannot split a pool whose size <= extract request.
  assert((!m.split<TQ, Pushed, Behaviour>(&p2, &p3, 5)));
  assert((!m.getPool<TQ, Pushed, Behaviour>(&p3)));

  assert((!m.resizePool(&p2, 0)));
  assert((m.getPool<TQ, Pushed, Behaviour>(&p2)->size() == 5));

  assert((m.resizePool(&p2, 3)));
  assert((m.getPool<TQ, Pushed, Behaviour>(&p2)->size() == 3));

  assert((m.resizePool(&p2, 5)));
  assert((m.getPool<TQ, Pushed, Behaviour>(&p2)->size() == 5));

  PASS();
}

template <typename TQ, typename Pushed, B Behaviour>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_signals() {
  TEST("signals");

  concurrency::pool::Manager m;

  std::atomic<int> count = 0;

  {
    auto add = [&count](auto, auto tp) { count += tp->size(); };
    auto rem = [&count](auto, auto tp) { count -= tp->size(); };
    auto res = [&count](auto, auto old, auto ns) { count += (old - ns); };

    auto r1 = m.onPoolAdded.connect(add);
    auto r2 = m.onPoolRemoved.connect(rem);
    auto r3 = m.onPoolResized.connect(res);
  }

  assert(count.load() == 0);

  m.createPool<TQ, Pushed, Behaviour>(&p1, 2);
  m.createPool<TQ, Pushed, Behaviour>(&p2, 4);

  assert(count.load() == 6); // +2 +4

  m.split<TQ, Pushed, Behaviour>(&p2, &p3, 2);

  assert(count.load() == 10); // resize +2, add +2

  m.resizePool(&p1, 4);

  assert(count.load() == 8); // resize +(2-4) = -2

  m.removePool(&p1);
  m.removePool(&p2);
  m.removePool(&p3);

  assert(count.load() == 0); // -4 -2 -2

  PASS();
}

// ─── Heterogeneous Manager ─────────────────────────────────────────────

void test_heterogeneous_coexistence() {
  TEST("heterogeneous coexistence");

  concurrency::pool::Manager m;

  assert(m.createPool<Fifo>(&p1, 2));
  assert((m.createPool<PriorityQueue, PriorityEntry>(&p2, 3)));
  assert((m.createPool<Loop, Task, B::Looping>(&p3, 4)));

  auto f = m.getPool<Fifo>(&p1);
  auto q = m.getPool<PriorityQueue, PriorityEntry>(&p2);
  auto l = m.getPool<Loop, Task, B::Looping>(&p3);

  assert(f && f->size() == 2);
  assert(q && q->size() == 3);
  assert(l && l->size() == 4);

  PASS();
}

void test_wrong_type_returns_null() {
  TEST("wrong type returns null");

  concurrency::pool::Manager m;

  assert(m.createPool<Fifo>(&p1, 2));

  // Asking for the wrong concrete type yields a null shared_ptr, not UB.
  assert((!m.getPool<PriorityQueue, PriorityEntry>(&p1)));
  assert((!m.getPool<Loop, Task, B::Looping>(&p1)));
  assert(m.getPool<Fifo>(&p1));

  PASS();
}

void test_type_erased_operations() {
  TEST("type-erased operations");

  concurrency::pool::Manager m;
  m.createPool<Fifo>(&p1, 2);

  auto base = m.getPoolBase(&p1);
  assert(base);
  assert(base->size() == 2);
  assert(base->behaviour() == B::Consuming);

  base->resize(4);
  assert(base->size() == 4);

  std::atomic<int> ran{0};
  base->submit_detach_erased(
      [&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
  base->wait();
  assert(ran.load() == 1);

  PASS();
}

void test_heterogeneous_signals() {
  TEST("heterogeneous signals");

  concurrency::pool::Manager m;

  std::atomic<int> added{0};
  std::atomic<int> removed{0};

  auto r1 = m.onPoolAdded.connect(
      [&added](const concurrency::pool::Pool *,
               concurrency::pool::ThreadPoolBase *) { added.fetch_add(1); });
  auto r2 =
      m.onPoolRemoved.connect([&removed](const concurrency::pool::Pool *,
                                         concurrency::pool::ThreadPoolBase *) {
        removed.fetch_add(1);
      });

  m.createPool<Fifo>(&p1, 1);
  m.createPool<PriorityQueue, PriorityEntry>(&p2, 1);
  m.createPool<Loop, Task, B::Looping>(&p3, 1);
  assert(added.load() == 3);

  m.removePool(&p1);
  m.removePool(&p2);
  m.removePool(&p3);
  assert(removed.load() == 3);

  PASS();
}

// ─── Drivers ───────────────────────────────────────────────────────────

template <typename TQ, typename Pushed, B Behaviour>
  requires(concurrency::queues::Queue<TQ, Pushed>)
static void per_kind_suite() {
  std::ranges::for_each(std::views::iota(0, 5), [](uint32_t) {
    test_create<TQ, Pushed, Behaviour>();
    test_signals<TQ, Pushed, Behaviour>();
  });
}

static void fifo_suite() { per_kind_suite<Fifo, Task, B::Consuming>(); }
static void priority_suite() {
  per_kind_suite<PriorityQueue, PriorityEntry, B::Consuming>();
}
static void loop_suite() { per_kind_suite<Loop, Task, B::Looping>(); }

static void mixed_suite() {
  std::ranges::for_each(std::views::iota(0, 5), [](uint32_t) {
    test_heterogeneous_coexistence();
    test_wrong_type_returns_null();
    test_type_erased_operations();
    test_heterogeneous_signals();
  });
}

template <auto Suite> static void ex(uint32_t repeats) {
  std::ranges::for_each(std::views::iota(0U, repeats),
                        [](uint32_t) { Suite(); });
}

int main() {
  std::println("=== Concurrency Manager Tests ===");

  std::array<std::jthread, 4> threads;
  threads[0] = std::jthread(ex<fifo_suite>, 5);
  threads[1] = std::jthread(ex<priority_suite>, 5);
  threads[2] = std::jthread(ex<loop_suite>, 5);
  threads[3] = std::jthread(ex<mixed_suite>, 5);
  for (auto &t : threads)
    t.join();

  fifo_suite();
  priority_suite();
  loop_suite();
  mixed_suite();

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
