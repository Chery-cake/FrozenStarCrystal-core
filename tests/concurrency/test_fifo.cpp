#include <cassert>
import concurrency_helper;

// ─── 1. Pure queue behavior ──────────────────────────────────────────

void test_fifo_alone() {
  ct::TEST("fifo: standalone");
  concurrency::queues::Fifo<concurrency::queues::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};
  q.push([&] { count.fetch_add(1); });
  q.push([&] { count.fetch_add(2); });
  q.push([&] { count.fetch_add(3); });

  concurrency::queues::Task t;
  for (int expected : {1, 3, 6}) {
    assert(q.try_pop(t, st));
    t();
    assert(count == expected);
  }
  ss.request_stop();
  assert(!q.try_pop(t, st));
  ct::PASS();
}

void test_fifo_blocking_pop() {
  // The producer/consumer scenario from the old test — one worker blocks
  // in try_pop, main thread pushes, worker wakes.
  ct::TEST("fifo: blocking pop");
  concurrency::queues::Fifo<concurrency::queues::Task> q;
  std::stop_source ss;
  auto st = ss.get_token();

  std::atomic<int> count{0};
  std::jthread jt([&] {
    concurrency::queues::Task t;
    assert(q.try_pop(t, st));
    t();
  });

  q.push([&] { count.fetch_add(1); });
  q.push([&] { count.fetch_add(2); });
  jt.join();
  assert(count == 1);

  ss.request_stop();
  ct::PASS();
}

// ─── 2. ThreadPool behavior ──────────────────────────────────────────

void test_fifo_pool_basics() {
  ct::TEST("fifo: pool basics");
  concurrency::pool::ThreadPool<
      concurrency::queues::Fifo<concurrency::queues::Task>>
      t(4);

  constexpr int kN = 64;
  std::atomic<int> completed{0};
  for (int i = 0; i < kN; ++i) {
    t.submit_detach([&] { completed.fetch_add(1); });
  }
  t.wait();
  assert(completed == kN);

  // submit() with futures — valid only when Pushed == Task
  auto f = [](int x) { return x * 2; };
  std::vector<std::future<int>> futures;
  for (int i = 0; i < 8; ++i)
    futures.push_back(t.submit(f, i));
  int sum = 0;
  for (auto &fu : futures)
    sum += fu.get();
  assert(sum == 56);

  ct::PASS();
}

// ─── 3. Coroutine matrix — all 4 policy combinations ─────────────────

void test_fifo_coroutines_all_policies() {
  ct::coro::run_all_policies<
      concurrency::queues::Fifo<concurrency::queues::Task>,
      concurrency::queues::Task>("fifo: coroutine");
}

// ─── 4. Payload variants ─────────────────────────────────────────────

void test_fifo_payload_alone() {
  ct::TEST("fifo: payload standalone");
  concurrency::queues::Fifo<ct::SimplePayloadEntry> q;

  // Fifo::make(t, payload) brace-inits Entry{task, payload}
  auto e = q.make([] {}, ct::SimplePayload{42});
  assert(e.payload().value == 42);

  std::stop_source ss;
  auto st = ss.get_token();
  concurrency::queues::Task t;
  q.push(std::move(e));
  assert(q.try_pop(t, st));
  t();
  ct::PASS();
}

void test_fifo_payload_pool() {
  ct::TEST("fifo: payload pool");
  concurrency::pool::ThreadPool<ct::FifoPayload, ct::SimplePayloadEntry> t(2);
  std::atomic<int> sum{0};
  for (int i = 0; i < 8; ++i) {
    t.submit_detach([&, i] { sum.fetch_add(i); }, ct::SimplePayload{i});
  }
  t.wait();
  assert(sum == 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7);
  ct::PASS();
}

void test_fifo_payload_coroutines() {
  ct::coro::run_all_policies<ct::FifoPayload, ct::SimplePayloadEntry>(
      "fifo: payload coroutine");
}

// ─── Driver ──────────────────────────────────────────────────────────

void suite() {
  test_fifo_alone();
  test_fifo_blocking_pop();
  test_fifo_pool_basics();
  test_fifo_coroutines_all_policies();
  test_fifo_payload_alone();
  test_fifo_payload_pool();
  test_fifo_payload_coroutines();
}

int main() {
  std::println("=== Fifo tests ===");
  std::array<std::jthread, 4> threads;
  for (auto &th : threads)
    th = std::jthread([] { suite(); });
  for (auto &th : threads)
    th.join();
  suite();

  std::println("\n{}/{} passed", ct::tests_passed.load(), ct::tests_run.load());
  return ct::tests_passed == ct::tests_run ? 0 : 1;
}
