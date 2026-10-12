#include <cassert>
import concurrency_helper;

constexpr concurrency::pool::Pool p1("p1");
constexpr concurrency::pool::Pool p2("p2");
constexpr concurrency::pool::Pool p3("p3");
constexpr concurrency::pool::Pool p4("p4");
constexpr concurrency::pool::Pool p5("p5");

// ─── Generic per-kind suite ─────────────────────────────────────────

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_create() {
  ct::TEST("create");

  concurrency::pool::Manager m;
  assert((m.createPool<TQ, Pushed>(&p1)));
  assert((!m.createPool<TQ, Pushed>(&p1)));

  auto pool1 = m.getPool<TQ, Pushed>(&p1);
  assert(pool1);
  assert(pool1->size() == std::thread::hardware_concurrency());

  assert((m.createPool<TQ, Pushed>(&p2, 5)));
  assert((m.getPool<TQ, Pushed>(&p2)->size() == 5));

  assert(m.removePool(&p2));
  assert(!m.removePool(&p2));

  assert((m.split<TQ, Pushed>(&p1, &p2, 5)));
  assert((m.getPool<TQ, Pushed>(&p1)->size() ==
          (std::thread::hardware_concurrency() - 5)));
  assert((m.getPool<TQ, Pushed>(&p2)->size() == 5));

  assert((!m.split<TQ, Pushed>(&p2, &p3, 5)));
  assert((!m.getPool<TQ, Pushed>(&p3)));

  assert((!m.resizePool(&p2, 0)));
  assert((m.getPool<TQ, Pushed>(&p2)->size() == 5));
  assert((m.resizePool(&p2, 3)));
  assert((m.getPool<TQ, Pushed>(&p2)->size() == 3));
  assert((m.resizePool(&p2, 5)));
  assert((m.getPool<TQ, Pushed>(&p2)->size() == 5));

  ct::PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
void test_signals() {
  ct::TEST("signals");

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
  m.createPool<TQ, Pushed>(&p1, 2);
  m.createPool<TQ, Pushed>(&p2, 4);
  assert(count.load() == 6);

  m.split<TQ, Pushed>(&p2, &p3, 2);
  assert(count.load() == 10);

  m.resizePool(&p1, 4);
  assert(count.load() == 8);

  m.removePool(&p1);
  m.removePool(&p2);
  m.removePool(&p3);
  assert(count.load() == 0);

  ct::PASS();
}

template <typename TQ, typename Pushed>
  requires(concurrency::queues::Queue<TQ, Pushed>)
static void per_kind_suite() {
  std::ranges::for_each(std::views::iota(0, 5), [](uint32_t) {
    test_create<TQ, Pushed>();
    test_signals<TQ, Pushed>();
  });
}

// ─── Heterogeneous manager tests ────────────────────────────────────

void test_heterogeneous_coexistence() {
  ct::TEST("heterogeneous coexistence");

  concurrency::pool::Manager m;

  assert(m.createPool<ct::FifoTask>(&p1, 2));
  assert((m.createPool<ct::FifoPayload, ct::SimplePayloadEntry>(&p2, 3)));
  // p2 already taken; different type under the same tag must be refused.
  assert((!m.createPool<ct::BasicPriorityQueue, ct::PriorityEntry>(&p2, 3)));
  assert((m.createPool<ct::BasicPriorityQueue, ct::PriorityEntry>(&p4, 3)));
  assert((m.createPool<ct::LoopOneShot, ct::OneShotLoopEntry>(&p3, 4)));

  assert(m.getPool<ct::FifoTask>(&p1)->size() == 2);
  assert(
      (m.getPool<ct::FifoPayload, ct::SimplePayloadEntry>(&p2)->size() == 3));
  assert(
      (m.getPool<ct::BasicPriorityQueue, ct::PriorityEntry>(&p4)->size() == 3));
  assert((m.getPool<ct::LoopOneShot, ct::OneShotLoopEntry>(&p3)->size() == 4));

  ct::PASS();
}

void test_wrong_type_returns_null() {
  ct::TEST("wrong type returns null");

  concurrency::pool::Manager m;
  assert(m.createPool<ct::FifoTask>(&p1, 2));

  assert((!m.getPool<ct::BasicPriorityQueue, ct::PriorityEntry>(&p1)));
  assert((!m.getPool<ct::LoopOneShot, ct::OneShotLoopEntry>(&p1)));
  assert((!m.getPool<ct::FifoPayload, ct::SimplePayloadEntry>(&p1)));
  assert(m.getPool<ct::FifoTask>(&p1));

  ct::PASS();
}

void test_type_erased_operations() {
  ct::TEST("type-erased operations");

  concurrency::pool::Manager m;
  m.createPool<ct::Fifo>(&p1, 2);

  auto base = m.getPoolBase(&p1);
  assert(base);
  assert(base->size() == 2);
  assert(base->behaviour() == ct::B::Consuming);

  base->resize(4);
  assert(base->size() == 4);

  base->wait();

  ct::PASS();
}

void test_heterogeneous_signals() {
  ct::TEST("heterogeneous signals");

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

  assert(m.createPool<ct::FifoTask>(&p1, 1));
  assert((m.createPool<ct::FifoPayload, ct::SimplePayloadEntry>(&p2, 1)));
  assert((m.createPool<ct::BasicPriorityQueue, ct::PriorityEntry>(&p4, 1)));
  assert((m.createPool<ct::PayloadPriorityQueue, ct::PayloadPriorityEntry>(&p5,
                                                                           1)));
  assert((m.createPool<ct::LoopOneShot, ct::OneShotLoopEntry>(&p3, 1)));
  assert(added.load() == 5);

  m.removePool(&p1);
  m.removePool(&p2);
  m.removePool(&p3);
  m.removePool(&p4);
  m.removePool(&p5);
  assert(removed.load() == 5);

  ct::PASS();
}

// ─── Drivers ────────────────────────────────────────────────────────

static void fifo_task_suite() { per_kind_suite<ct::FifoTask, ct::Task>(); }
static void fifo_payload_suite() {
  per_kind_suite<ct::FifoPayload, ct::SimplePayloadEntry>();
}
static void priority_suite() {
  per_kind_suite<ct::BasicPriorityQueue, ct::PriorityEntry>();
}
static void priority_payload_suite() {
  per_kind_suite<ct::PayloadPriorityQueue, ct::PayloadPriorityEntry>();
}
static void loop_task_suite() { per_kind_suite<ct::LoopTask, ct::Task>(); }
static void loop_oneshot_suite() {
  per_kind_suite<ct::LoopOneShot, ct::OneShotLoopEntry>();
}
static void loop_payload_suite() {
  per_kind_suite<ct::LoopPayload, ct::PayloadLoopEntry>();
}

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

  std::array<std::jthread, 8> threads;
  threads[0] = std::jthread(ex<fifo_task_suite>, 5);
  threads[1] = std::jthread(ex<fifo_payload_suite>, 5);
  threads[2] = std::jthread(ex<priority_suite>, 5);
  threads[3] = std::jthread(ex<priority_payload_suite>, 5);
  threads[4] = std::jthread(ex<loop_task_suite>, 5);
  threads[5] = std::jthread(ex<loop_oneshot_suite>, 5);
  threads[6] = std::jthread(ex<loop_payload_suite>, 5);
  threads[7] = std::jthread(ex<mixed_suite>, 5);
  for (auto &t : threads)
    t.join();

  // Also run once on the main thread for clean sequencing.
  fifo_task_suite();
  fifo_payload_suite();
  priority_suite();
  priority_payload_suite();
  loop_task_suite();
  loop_oneshot_suite();
  loop_payload_suite();
  mixed_suite();

  std::println("\n{}/{} tests passed", ct::tests_passed.load(),
               ct::tests_run.load());
  return (ct::tests_passed == ct::tests_run) ? 0 : 1;
}
