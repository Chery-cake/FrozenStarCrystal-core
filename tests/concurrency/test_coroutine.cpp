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

/////////////////////////////////////////////

// --- Nested scheduling with final continuation ---
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
inner_schedule_and_return(concurrency::pool::ThreadPool &t) {
  co_await t.schedule();
  co_return true;
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
outer_awaits_inner(concurrency::pool::ThreadPool &t) {
  bool result = co_await inner_schedule_and_return(t);
  co_return result;
}

void test_nested_schedule_continuation() {
  TEST("nested schedule continuation");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  auto task = outer_awaits_inner(t);
  bool result = task.get();
  assert(result == true);

  // The outer coroutine must have resumed on the pool thread.
  // We can check indirectly by ensuring the pool worker flag is set
  // during the outer coroutine's resume. For that we need a modified test
  // that captures isPoolWorker at the moment of resume. We'll create a
  // variant that stores that flag in a variable accessible to the test.

  PASS();
}

//////////////////////////////////////////////

// --- External resume (simulates fence waiter) ---
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
external_resume_inner(concurrency::pool::ThreadPool &t) {
  // Schedule onto the pool first
  co_await t.schedule();
  // Then await an external event
  co_await ExternalEventAwaiter{};
  co_return true;
}

concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, bool>
external_resume_outer(concurrency::pool::ThreadPool &t) {
  bool result = co_await external_resume_inner(t);
  co_return result;
}

void test_external_resume_continuation() {
  TEST("external resume continuation");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  auto task = external_resume_outer(t);
  bool result = task.get();
  assert(result == true);
  PASS();
}

//////////////////////////////////////////////

// --- Chain of multiple awaits, each scheduling ---
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, void>
chain_step(concurrency::pool::ThreadPool &t, int depth, int &counter) {
  if (depth > 0) {
    co_await t.schedule();
    co_await chain_step(t, depth - 1, counter);
    counter++;
  } else {
    co_return;
  }
}

void test_chain_of_awaits() {
  TEST("chain of awaits");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  int counter = 0;
  auto task = chain_step(t, 5, counter);
  task.get();

  assert(counter == 5);
  PASS();
}

//////////////////////////////////////////////

// --- Verify correct queue for continuation ---
// We'll create a coroutine that schedules onto the pool, then awaits an
// inner coroutine that also schedules. The outer coroutine must resume on
// the pool. We can capture `isPoolWorker` during the outer resume using a
// small helper.
concurrency::pool::coroutine::CoroutineTask<
    concurrency::pool::coroutine::policy::Suspend::Always, void>
capture_resume_worker_flag(concurrency::pool::ThreadPool &t,
                           bool &flag_on_resume) {
  co_await t.schedule();
  // When this coroutine resumes after the inner await, we are on the pool.
  // We can't directly capture the flag here because this code runs before
  // the inner await. Instead, we'll have the inner coroutine set the flag
  // before it returns, and the outer coroutine can observe it after co_await.
  bool inner_result = co_await inner_schedule_and_return(t);
  // After the inner returns, we are resumed on the pool if the
  // continuation was scheduled there.
  flag_on_resume = concurrency::pool::coroutine::isPoolWorker;
  co_return;
}

void test_continuation_runs_on_pool() {
  TEST("continuation runs on pool");

  concurrency::pool::Pool p("test");
  concurrency::pool::ThreadPool t(p, 2);

  bool flag_on_resume = false;
  auto task = capture_resume_worker_flag(t, flag_on_resume);
  task.get();

  assert(flag_on_resume == true);
  PASS();
}

//////////////////////////////////////////////

int main() {
  std::println("=== Concurrency Coroutines and Scheduler Tests ===");

  static auto tests = []() {
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

    test_nested_schedule_continuation();
    test_external_resume_continuation();
    test_chain_of_awaits();
    test_continuation_runs_on_pool();
  };

  static auto ex = [](uint32_t repeats) {
    std::println("Started id: {}", std::this_thread::get_id());

    std::ranges::for_each(std::views::iota(0U, repeats),
                          [](uint32_t) { tests(); });
  };

  std::array<std::jthread, 5> threads;

  std::ranges::for_each(threads,
                        [](std::jthread &th) { th = std::jthread(ex, 100); });

  std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });

  ex(100);

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
