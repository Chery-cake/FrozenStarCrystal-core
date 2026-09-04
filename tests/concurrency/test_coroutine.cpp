#include <cassert>
import concurrency_helper;

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, void>
scheduler_probe_always(concurrency::pool::ThreadPool &t,
                       std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, void>
scheduler_probe_always2(concurrency::queues::TaskQueue *queue,
                        std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
scheduler_probe_never(concurrency::pool::ThreadPool &t,
                      std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
scheduler_probe_never2(concurrency::queues::TaskQueue *queue,
                       std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

///////////////////////////////////////

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
scheduler_return_probe_always(concurrency::pool::ThreadPool &t) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
scheduler_return_probe_always2(concurrency::queues::TaskQueue *queue) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, bool>
scheduler_return_probe_never(concurrency::pool::ThreadPool &t) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, bool>
scheduler_return_probe_never2(concurrency::queues::TaskQueue *queue) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

//////////////////////////////////////

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, void>
await_probe_void_always(concurrency::pool::ThreadPool &t,
                        std::optional<bool> &result) {
  co_await scheduler_probe_always(t, result);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
await_probe_value_always(concurrency::pool::ThreadPool &t) {
  co_return co_await scheduler_return_probe_always(t);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, void>
await_probe_void_never(concurrency::pool::ThreadPool &t,
                       std::optional<bool> &result) {
  co_await scheduler_probe_never(t, result);
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Never, bool>
await_probe_value_never(concurrency::pool::ThreadPool &t) {
  co_return co_await scheduler_return_probe_never(t);
}

//////////////////////////////////////

void test_void_always() {
  TEST("void always");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  std::optional<bool> result;
  auto task1 = scheduler_probe_always(t, result);
  task1.get();

  assert(result.has_value());
  assert(*result);

  result.reset();
  auto task2 = scheduler_probe_always2(t.queue(), result);
  task2.get();

  assert(result.has_value());
  assert(*result);

  PASS();
}

void test_value_always() {
  TEST("value always");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  auto r1 = scheduler_return_probe_always(t);
  assert(r1.get());

  auto r2 = scheduler_return_probe_always2(t.queue());
  assert(r2.get());

  PASS();
}

void test_void_never() {
  TEST("void never");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  std::optional<bool> result;
  auto task1 = scheduler_probe_never(t, result);
  task1.get();

  assert(result.has_value());
  assert(*result);

  result.reset();
  auto task2 = scheduler_probe_never2(t.queue(), result);
  task2.get();

  assert(result.has_value());
  assert(*result);

  PASS();
}

void test_value_never() {
  TEST("value never");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  auto r1 = scheduler_return_probe_never(t);
  assert(r1.get());

  auto r2 = scheduler_return_probe_never2(t.queue());
  assert(r2.get());

  PASS();
}

void test_co_await_void_always() {
  TEST("co_await void always");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  std::optional<bool> result;
  auto task = await_probe_void_always(t, result);
  task.get();

  assert(result.has_value());
  assert(*result);

  PASS();
}

void test_co_await_value_always() {
  TEST("co_await value always");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  auto task = await_probe_value_always(t);
  assert(task.get());

  PASS();
}

void test_co_await_void_never() {
  TEST("co_await void never");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  std::optional<bool> result;
  auto task = await_probe_void_never(t, result);
  task.get();

  assert(result.has_value());
  assert(*result);

  PASS();
}

void test_co_await_value_never() {
  TEST("co_await value never");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  auto task = await_probe_value_never(t);
  assert(task.get());

  PASS();
}

/////////////////////////////////////////////

template <concurrency::pool::coroutine::policy::Suspend SP>
concurrency::pool::coroutine::CoroutineTask<SP, void>
probe_initial_suspend(bool &flag) {
  flag = true; // ← body ran
  co_return;
}

void test_initial_suspend_always() {
  TEST("suspend always");
  bool flag = false;
  auto task = probe_initial_suspend<
      concurrency::pool::coroutine::policy::Suspend::Always>(flag);
  assert(!flag);
  task.start();
  assert(flag);
  assert(task.done());
  PASS();
}
void test_initial_suspend_never() {
  TEST("suspend never");
  bool flag = false;
  auto task = probe_initial_suspend<
      concurrency::pool::coroutine::policy::Suspend::Never>(flag);
  assert(flag);
  assert(task.done());
  PASS();
}

int main() {
  std::println("=== Concurrency Coroutines and Scheduler Tests ===");

  static auto tests = [](std::thread::id id) {
    std::println("Started id: {}", id);

    test_void_always();
    test_value_always();

    test_void_never();
    test_value_never();

    test_co_await_void_always();
    test_co_await_value_always();

    test_co_await_void_never();
    test_co_await_value_never();

    test_initial_suspend_always();
    test_initial_suspend_never();
  };

  std::array<std::jthread, 5> threads;

  static auto ex = []() { tests(std::this_thread::get_id()); };

  for (int i = 0; i < 100; i++) {
    std::ranges::for_each(threads,
                          [](std::jthread &th) { th = std::jthread(ex); });

    std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });
  }

  ex();

  std::println("\n{}/{} tests passed", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
