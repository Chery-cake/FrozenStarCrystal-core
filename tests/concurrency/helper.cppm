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

    // ─── Priority queue test types ───────────────────────────────────────

    struct PriorityEntry {
        concurrency::queues::Task task;
        int priority = 0;
    };

    // Default-style score: read the entry's own priority field.
    struct BasicScore {
        using Score = int;
        int score(const PriorityEntry &e) const noexcept { return e.priority; }
    };

    using PriorityQueue =
        concurrency::queues::Priority<PriorityEntry, std::vector, BasicScore>;

    // Deadline aging. Highest tuple value pops first (max-heap), so:
    //   - primary:  -deadline  (earlier deadline → larger key)
    //   - tiebreak: priority   (higher priority → larger key)
    struct TestAging {
        using Score = std::tuple<int64_t, int>;
        std::reference_wrapper<int64_t> now;

        auto score(const PriorityEntry &e) const {
            const int64_t deadline = now.get() + (11 - e.priority);
            return std::tuple{-deadline, e.priority};
        }
    };

    using AgedPriorityQueue =
        concurrency::queues::Priority<PriorityEntry, std::vector, TestAging>;

    // ─── Payload entry for priority queue ───────────────────────────────
    struct PriorityPayload {
        int priority = 0;
        int data = 0;
        auto operator<=>(const PriorityPayload &) const = default;
    };

    struct PayloadPriorityEntry {
        concurrency::queues::Task task;
        PriorityPayload p;
        using Payload = PriorityPayload;
        Payload payload() const { return p; }
        int priority() const { return p.priority; }
    };

    struct PayloadScore {
        using Score = int;
        int score(const PayloadPriorityEntry &e) const noexcept {
            return e.priority();
        }
        // Required by concurrency.pool.coroutine::enqueue_payload →
        // queue.make(t, payload)
        PayloadPriorityEntry make(concurrency::queues::Task t) {
            return PayloadPriorityEntry{std::move(t), {}};
        }
        PayloadPriorityEntry make(concurrency::queues::Task t,
                                  PriorityPayload p) {
            return PayloadPriorityEntry{std::move(t), std::move(p)};
        }
    };

    using PayloadPriorityQueue =
        concurrency::queues::Priority<PayloadPriorityEntry, std::vector,
                                      PayloadScore>;

    // ─── Timed entry for aging tests ────────────────────────────────────
    inline std::atomic<int64_t> g_aging_now{0};

    struct TimedEntry {
        concurrency::queues::Task task;
        int priority = 0;
        int64_t arrival = 0;
    };

    struct TimeAgingScore {
        using Score = int;
        int score(const TimedEntry &e) const {
            const int64_t waited =
                g_aging_now.load(std::memory_order_relaxed) - e.arrival;
            return e.priority + static_cast<int>(waited / 10);
        }
    };

    // Renamed: was AgedPriorityQueue (which already exists above with
    // TestAging)
    using TimedPriorityQueue = concurrency::queues::Priority<
        TimedEntry, std::vector, TimeAgingScore,
        concurrency::queues::AddressableReprioritize>;
}

// TODO
// rebalance the test files, making them smaller so eglot can process them
