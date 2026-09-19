module;

export module concurrency.queues;

export import :queue;
export import :fifo;

// TODO
// change to use lock-free queue with semaphores, preferably atomics
// will need to implement the queue, std doen't have one lock-free
