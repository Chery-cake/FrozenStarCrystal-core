#include <cassert>
import concurrency_helper;

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, void>
scheduler_probe_always(concurrency::pool::ThreadPool<TQ> &t,
                       std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, void>
scheduler_probe_always2(TQ *queue, std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool<TQ>::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Never, void>
scheduler_probe_never(concurrency::pool::ThreadPool<TQ> &t,
                      std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Never, void>
scheduler_probe_never2(TQ *queue, std::optional<bool> &result) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool<TQ>::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  result = (!before && after);
}

///////////////////////////////////////

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
scheduler_return_probe_always(concurrency::pool::ThreadPool<TQ> &t) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
scheduler_return_probe_always2(TQ *queue) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool<TQ>::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Never, bool>
scheduler_return_probe_never(concurrency::pool::ThreadPool<TQ> &t) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await t.schedule();
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Never, bool>
scheduler_return_probe_never2(TQ *queue) {
  const bool before = concurrency::pool::coroutine::isPoolWorker;
  co_await concurrency::pool::ThreadPool<TQ>::schedule(queue);
  const bool after = concurrency::pool::coroutine::isPoolWorker;
  co_return (!before && after);
}

//////////////////////////////////////

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, void>
await_probe_void_always(concurrency::pool::ThreadPool<TQ> &t,
                        std::optional<bool> &result) {
  co_await scheduler_probe_always(t, result);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
await_probe_value_always(concurrency::pool::ThreadPool<TQ> &t) {
  co_return co_await scheduler_return_probe_always(t);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Never, void>
await_probe_void_never(concurrency::pool::ThreadPool<TQ> &t,
                       std::optional<bool> &result) {
  co_await scheduler_probe_never(t, result);
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Never, bool>
await_probe_value_never(concurrency::pool::ThreadPool<TQ> &t) {
  co_return co_await scheduler_return_probe_never(t);
}

//////////////////////////////////////

template <concurrency::queues::TaskQueue TQ> void test_void_always() {
  TEST("void always");

  concurrency::pool::ThreadPool<TQ> t(2);

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

template <concurrency::queues::TaskQueue TQ> void test_value_always() {
  TEST("value always");

  concurrency::pool::ThreadPool<TQ> t(2);

  auto r1 = scheduler_return_probe_always(t);
  assert(r1.get());

  auto r2 = scheduler_return_probe_always2(t.queue());
  assert(r2.get());

  PASS();
}

template <concurrency::queues::TaskQueue TQ> void test_void_never() {
  TEST("void never");

  concurrency::pool::ThreadPool<TQ> t(2);

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

template <concurrency::queues::TaskQueue TQ> void test_value_never() {
  TEST("value never");

  concurrency::pool::ThreadPool<TQ> t(2);

  auto r1 = scheduler_return_probe_never(t);
  assert(r1.get());

  auto r2 = scheduler_return_probe_never2(t.queue());
  assert(r2.get());

  PASS();
}

template <concurrency::queues::TaskQueue TQ> void test_co_await_void_always() {
  TEST("co_await void always");

  concurrency::pool::ThreadPool<TQ> t(2);

  std::optional<bool> result;
  auto task = await_probe_void_always(t, result);
  task.get();

  assert(result.has_value());
  assert(*result);

  PASS();
}

template <concurrency::queues::TaskQueue TQ> void test_co_await_value_always() {
  TEST("co_await value always");

  concurrency::pool::ThreadPool<TQ> t(2);

  auto task = await_probe_value_always(t);
  assert(task.get());

  PASS();
}

template <concurrency::queues::TaskQueue TQ> void test_co_await_void_never() {
  TEST("co_await void never");

  concurrency::pool::ThreadPool<TQ> t(2);

  std::optional<bool> result;
  auto task = await_probe_void_never(t, result);
  task.get();

  assert(result.has_value());
  assert(*result);

  PASS();
}

template <concurrency::queues::TaskQueue TQ> void test_co_await_value_never() {
  TEST("co_await value never");

  concurrency::pool::ThreadPool<TQ> t(2);

  auto task = await_probe_value_never(t);
  assert(task.get());

  PASS();
}

/////////////////////////////////////////////

template <concurrency::queues::TaskQueue TQ,
          concurrency::pool::coroutine::policy::Suspend SP>
concurrency::pool::coroutine::CoroutineTask<TQ, SP, void>
probe_initial_suspend(bool &flag) {
  flag = true; // ← body ran
  co_return;
}

template <concurrency::queues::TaskQueue TQ>
void test_initial_suspend_always() {
  TEST("suspend always");
  bool flag = false;
  auto task = probe_initial_suspend<
      TQ, concurrency::pool::coroutine::policy::Suspend::Always>(flag);
  assert(!flag);
  task.start();
  assert(flag);
  assert(task.done());
  PASS();
}
template <concurrency::queues::TaskQueue TQ> void test_initial_suspend_never() {
  TEST("suspend never");
  bool flag = false;
  auto task = probe_initial_suspend<
      TQ, concurrency::pool::coroutine::policy::Suspend::Never>(flag);
  assert(flag);
  assert(task.done());
  PASS();
}

/////////////////////////////////////////////

// --- Nested scheduling with final continuation ---
template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
inner_schedule_and_return(concurrency::pool::ThreadPool<TQ> &t) {
  co_await t.schedule();
  co_return true;
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
outer_awaits_inner(concurrency::pool::ThreadPool<TQ> &t) {
  bool result = co_await inner_schedule_and_return(t);
  co_return result;
}

template <concurrency::queues::TaskQueue TQ>
void test_nested_schedule_continuation() {
  TEST("nested schedule continuation");

  concurrency::pool::ThreadPool<TQ> t(2);

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
template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
external_resume_inner(concurrency::pool::ThreadPool<TQ> &t) {
  // Schedule onto the pool first
  co_await t.schedule();
  // Then await an external event
  co_await ExternalEventAwaiter{};
  co_return true;
}

template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, bool>
external_resume_outer(concurrency::pool::ThreadPool<TQ> &t) {
  bool result = co_await external_resume_inner(t);
  co_return result;
}

template <concurrency::queues::TaskQueue TQ>
void test_external_resume_continuation() {
  TEST("external resume continuation");

  concurrency::pool::ThreadPool<TQ> t(2);

  auto task = external_resume_outer(t);
  bool result = task.get();
  assert(result == true);
  PASS();
}

//////////////////////////////////////////////

// --- Chain of multiple awaits, each scheduling ---
template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, void>
chain_step(concurrency::pool::ThreadPool<TQ> &t, int depth, int &counter) {
  if (depth > 0) {
    co_await t.schedule();
    co_await chain_step(t, depth - 1, counter);
    counter++;
  } else {
    co_return;
  }
}

template <concurrency::queues::TaskQueue TQ> void test_chain_of_awaits() {
  TEST("chain of awaits");

  concurrency::pool::ThreadPool<TQ> t(2);

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
template <concurrency::queues::TaskQueue TQ>
concurrency::pool::coroutine::CoroutineTask<
    TQ, concurrency::pool::coroutine::policy::Suspend::Always, void>
capture_resume_worker_flag(concurrency::pool::ThreadPool<TQ> &t,
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

template <concurrency::queues::TaskQueue TQ>
void test_continuation_runs_on_pool() {
  TEST("continuation runs on pool");

  concurrency::pool::ThreadPool<TQ> t(2);

  bool flag_on_resume = false;
  auto task = capture_resume_worker_flag(t, flag_on_resume);
  task.get();

  assert(flag_on_resume == true);
  PASS();
}

//////////////////////////////////////////////

template <concurrency::queues::TaskQueue TQ> static void tests() {
  test_void_always<TQ>();
  test_value_always<TQ>();

  test_void_never<TQ>();
  test_value_never<TQ>();

  test_co_await_void_always<TQ>();
  test_co_await_value_always<TQ>();

  test_co_await_void_never<TQ>();
  test_co_await_value_never<TQ>();

  test_initial_suspend_always<TQ>();
  test_initial_suspend_never<TQ>();

  test_nested_schedule_continuation<TQ>();
  test_external_resume_continuation<TQ>();
  test_chain_of_awaits<TQ>();
  test_continuation_runs_on_pool<TQ>();
};

template <concurrency::queues::TaskQueue TQ> static void ex(uint32_t repeats) {
  {
    std::lock_guard lock(log_mutex);
    std::println("Started id: {}", std::this_thread::get_id());
  }
  std::ranges::for_each(std::views::iota(0U, repeats),
                        [](uint32_t) { tests<TQ>(); });
};

int main() {
  std::println("=== Concurrency Coroutines and Scheduler Tests ===");

  std::array<std::jthread, 5> threads;

  std::ranges::for_each(threads, [](std::jthread &th) {
    th = std::jthread(ex<concurrency::queues::FifoTaskQueue>, 100);
  });

  std::ranges::for_each(threads, [](std::jthread &th) { th.join(); });

  ex<concurrency::queues::FifoTaskQueue>(100);

  std::println("\n{}/{} tests passed", tests_passed.load(), tests_run.load());
  return (tests_passed == tests_run) ? 0 : 1;
}
