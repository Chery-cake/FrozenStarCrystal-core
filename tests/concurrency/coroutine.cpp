import std.compat;

// -----------------------------------------------------------------------------
// Simple functions
// -----------------------------------------------------------------------------
void print_id_twice() {
  std::cout << "  thread id: " << std::this_thread::get_id() << '\n';
}

std::thread::id get_thread_id() { return std::this_thread::get_id(); }

// -----------------------------------------------------------------------------
// print_task – self-destroying coroutine with safe handle management
// -----------------------------------------------------------------------------
struct print_task {
  struct promise_type {
    print_task get_return_object() {
      return print_task{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };

  using handle_t = std::coroutine_handle<promise_type>;
  handle_t h_;

  print_task(handle_t h) : h_{h} {}
  print_task(const print_task &) = delete;
  print_task(print_task &&other) noexcept
      : h_{std::exchange(other.h_, nullptr)} {}
  ~print_task() {
    if (h_)
      h_.destroy();
  }

  void resume() {
    if (h_ && !h_.done())
      h_.resume();
  }
};

// -----------------------------------------------------------------------------
// task<T> – value-returning coroutine (unchanged)
// -----------------------------------------------------------------------------
template <typename T> class task {
public:
  struct promise_type {
    std::optional<T> result;
    std::latch done{1};

    task get_return_object() {
      return task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    auto final_suspend() noexcept {
      struct final_awaiter {
        promise_type *p;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) noexcept {
          p->done.count_down();
        }
        void await_resume() noexcept {}
      };
      return final_awaiter{this};
    }
    void return_value(T value) { result = std::move(value); }
    void unhandled_exception() { std::terminate(); }
  };

  using handle_t = std::coroutine_handle<promise_type>;
  handle_t h_;

  task(handle_t h) : h_{h} {}
  task(const task &) = delete;
  task(task &&other) noexcept : h_{std::exchange(other.h_, nullptr)} {}
  ~task() {
    if (h_)
      h_.destroy();
  }

  T get_result() {
    h_.promise().done.wait();
    return *h_.promise().result;
  }

  void resume() { h_.resume(); }
};

// -----------------------------------------------------------------------------
// Coroutines
// -----------------------------------------------------------------------------
print_task demo_print() {
  std::cout << "\n>>> First print (will run on the thread that resumes):\n";
  print_id_twice();

  co_await std::suspend_always{};

  std::cout << "\n>>> Second print (resumed on a different thread):\n";
  print_id_twice();

  co_await std::suspend_always{};

  std::cout << "\n>>> Third print (resumed on another thread):\n";
  print_id_twice();

  co_return;
}

task<std::thread::id> capture_id() { co_return get_thread_id(); }

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
int main() {
  std::cout << "=== Part 1: print_id_twice() on three different threads ===";

  auto pt = demo_print();

  pt.resume();

  std::jthread worker1{[&pt]() { pt.resume(); }};
  std::jthread worker2{[&pt]() { pt.resume(); }};

  worker1.join();
  worker2.join();

  std::cout << "\n=== Part 2: get_thread_id() from two worker threads ===";

  auto t1 = capture_id();
  auto t2 = capture_id();

  std::jthread wt1{[&]() { t1.resume(); }};
  std::jthread wt2{[&]() { t2.resume(); }};

  auto id1 = t1.get_result();
  auto id2 = t2.get_result();

  std::cout << "\nThread ID captured by first coroutine:  " << id1
            << "\nThread ID captured by second coroutine: " << id2 << '\n';
}
