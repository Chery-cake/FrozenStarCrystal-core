import concurrency_helper;
#include <cassert>

constexpr concurrency::pool::Pool p1("p1");
constexpr concurrency::pool::Pool p2("p2");
constexpr concurrency::pool::Pool p3("p3");

template <concurrency::queues::Queue TQ> void test_create() {
  TEST("create");

  concurrency::pool::Manager<TQ> m;

  assert(m.createPool(&p1));
  assert(!m.createPool(&p1));

  assert(m.getPool(&p1).lock()->size() == std::thread::hardware_concurrency());

  assert(m.createPool(&p2, 5));
  assert(m.getPool(&p2).lock()->size() == 5);

  assert(m.removePool(&p2));
  assert(!m.removePool(&p2));

  assert(m.split(&p1, &p2, 5));

  assert(m.getPool(&p1).lock()->size() ==
         (std::thread::hardware_concurrency() - 5));
  assert(m.getPool(&p2).lock()->size() == 5);

  assert(!m.split(&p2, &p3, 5));
  assert(m.getPool(&p3).expired());

  assert(!m.resizePool(&p2, 0));
  assert(m.getPool(&p2).lock()->size() == 5);

  assert(m.resizePool(&p2, 3));
  assert(m.getPool(&p2).lock()->size() == 3);

  assert(m.resizePool(&p2, 5));
  assert(m.getPool(&p2).lock()->size() == 5);

  PASS();
}

template <concurrency::queues::Queue TQ> void test_signals() {
  TEST("signals");

  concurrency::pool::Manager<TQ> m;

  std::atomic<int> count = 0;

  {

    auto add = [&count](auto, auto tp) { count += tp->size(); };
    auto rem = [&count](auto, auto tp) { count -= tp->size(); };
    auto res = [&count](auto, auto old, auto new_s) { count += (old - new_s); };

    auto res1 = m.onPoolAdded.connect(add);
    auto res2 = m.onPoolRemoved.connect(rem);
    auto res3 = m.onPoolResized.connect(res);
  }

  assert(count.load() == 0);

  m.createPool(&p1, 2);
  m.createPool(&p2, 4);

  // add +2 +4
  assert(count.load() == 6);

  m.split(&p2, &p3, 2);

  // split + (4-2)
  // add +2
  assert(count.load() == 10);

  m.resizePool(&p1, 4);

  // resize + (2-4)
  assert(count.load() == 8);

  m.removePool(&p1);
  m.removePool(&p2);
  m.removePool(&p3);

  // rem -4 -2 -2
  assert(count.load() == 0);

  PASS();
}

template <concurrency::queues::Queue TQ> static void tests() {
  std::ranges::for_each(std::views::iota(0, 10), [](uint32_t) {
    test_create<TQ>();
    test_signals<TQ>();
  });
};

template <concurrency::queues::Queue TQ> static void ex() {
  {
    std::lock_guard lock(log_mutex);
    std::println("Started id: {}", std::this_thread::get_id());
  }
  tests<TQ>();
};

int main() {
  std::println("=== Concurrency Thread Pool Tests ===");

  std::array<std::jthread, 5> threads;

  std::ranges::for_each(threads, [](std::jthread &th) {
    th = std::jthread(ex<concurrency::queues::Fifo>);
  });

  std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });

  ex<concurrency::queues::Fifo>();

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
