module;

export module concurrency_test.framework;

export import std.compat;

export namespace ct {

inline std::atomic<int> tests_run{0};
inline std::atomic<int> tests_passed{0};
inline std::mutex log_mutex;

inline void TEST(std::string_view name) {
  tests_run.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard lock(log_mutex);
  std::println("[TEST] {} ... ", name);
}

inline void PASS() {
  tests_passed.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard lock(log_mutex);
  std::println("PASSED");
}

struct ExternalEventState {
  std::mutex mtx;
  std::condition_variable cv;
  std::coroutine_handle<> h;
  bool resumed = false;

  void resume() {
    std::lock_guard lk(mtx);
    if (!resumed) {
      resumed = true;
      h.resume();
    }
  }
};

struct ExternalEventAwaiter {
  std::shared_ptr<ExternalEventState> state =
      std::make_shared<ExternalEventState>();

  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> h) {
    state->h = h;
    std::jthread([state = state]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      state->resume();
    }).detach();
  }

  void await_resume() {}
};

} // namespace ct
