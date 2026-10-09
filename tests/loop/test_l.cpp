#include <cstdlib>
import std.compat;
import concurrency;

struct CoroEntry {
  concurrency::queues::Task task;
  // null  → one-shot entry (default): retire after invoke
  // non-null → persistent until the flag is set to true
  std::shared_ptr<std::atomic<bool>> retire_after = nullptr;

  CoroEntry() = default;
  CoroEntry(concurrency::queues::Task t) : task(std::move(t)) {}
  CoroEntry(concurrency::queues::Task t, std::shared_ptr<std::atomic<bool>> r)
      : task(std::move(t)), retire_after(std::move(r)) {}

  void invoke() {
    if (task)
      task();
  }

  bool should_retire() const {
    return !retire_after || retire_after->load(std::memory_order_acquire);
  }
};

using q_loop = concurrency::queues::Loop<CoroEntry>;
using Queue = concurrency::pool::coroutine::policy::Queue;
using Suspend = concurrency::pool::coroutine::policy::Suspend;

// Body runs inline the moment main_coro(ctx) is called; no .start() needed.
using CoroTask =
    concurrency::pool::coroutine::CoroutineTask<q_loop, CoroEntry,
                                                Suspend::Never, void>;

struct Ctx {
  std::atomic<int> counter{0};
  std::atomic<bool> done{false};
  int limit = 9; // multiple of 3: one run adds 3
};

std::mutex g_io;
template <typename... A> void say(std::format_string<A...> f, A &&...a) {
  std::lock_guard lk(g_io);
  std::println(f, std::forward<A>(a)...);
}

// ---------------------------------------------------------------------------
// sub coroutine
// ---------------------------------------------------------------------------
CoroTask sub_coro(std::shared_ptr<Ctx> ctx) {
  int n = ctx->counter.fetch_add(1) + 1;
  say("    [sub_coro]    counter={} thread={}", n, std::this_thread::get_id());
  co_return;
}

// ---------------------------------------------------------------------------
// main coroutine — no loops, just calls sub_coro once
// ---------------------------------------------------------------------------
CoroTask main_coro(std::shared_ptr<Ctx> ctx,
                   concurrency::pool::ThreadPool<q_loop, CoroEntry> &pool) {
  int n = ctx->counter.fetch_add(1) + 1;
  say("[main_coro] counter={} thread={}", n, std::this_thread::get_id());

  co_await sub_coro(ctx); // runs inline on the same worker

  co_await pool.schedule<Queue::Enqueue>();

  n = ctx->counter.fetch_add(1) + 1;
  say("[main_coro] counter={} thread={}", n, std::this_thread::get_id());

  co_return;
}

int main() {
  auto ctx = std::make_shared<Ctx>();

  {
    concurrency::pool::ThreadPool<q_loop, CoroEntry> pool(2);

    // The driver: a single persistent entry that gets invoked by a worker
    // on every loop pass until the counter reaches the limit.
    auto retire = std::make_shared<std::atomic<bool>>(false);

    auto driver = [ctx, &pool, retire]() {
      if (ctx->counter.load(std::memory_order_acquire) >= ctx->limit) {
        retire->store(true, std::memory_order_release);
        ctx->done.store(true, std::memory_order_release);
        ctx->done.notify_all();
        return;
      }
      main_coro(ctx, pool); // fire off another coroutine pass
    };

    pool.submit_detach(driver, retire);

    ctx->done.wait(false, std::memory_order_acquire);
    say("main: done, counter={}", ctx->counter.load());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    say("main: live CoroutineStates = {}",
        concurrency::pool::coroutine::g_live_coroutine_states.load());

    pool.queue()->clear();
    say("main: after clear, live CoroutineStates = {}",
        concurrency::pool::coroutine::g_live_coroutine_states.load());
  }

  say("main: after pool destruction, live CoroutineStates = {}",
      concurrency::pool::coroutine::g_live_coroutine_states.load());
  return EXIT_SUCCESS;
}
