module;

export module concurrency_helper;

export import std.compat;
export import concurrency;

export {
  std::atomic<int> tests_run{0};
  std::atomic<int> tests_passed{0};

  std::mutex log_mutex;

  void TEST(std::string_view name) {
    tests_run.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(log_mutex);
    std::println("[TEST] {} ... ", name);
  }

  void PASS() {
    tests_passed.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(log_mutex);
    std::println("PASSED");
  }

  // Simulates an external event (like a Vulkan fence) that resumes a
  // coroutine from another thread.
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
      // Launch a detached thread to resume later
      std::jthread([state = state]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        state->resume();
      }).detach();
    }

    void await_resume() {}
  };
}
