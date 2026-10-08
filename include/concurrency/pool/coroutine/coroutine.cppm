module;

export module concurrency.pool.coroutine;

export import concurrency.pool.coroutine.policy;

export import :scheduler;

export import :state;
export import :structs;
export import :task;

// TODO
// make so the priority on the coroutine can be affected by the aging of the
// queue and that it could be applyed when the coroutine is created
//
// make so the coroutine can receive parameters, that aren't only the priority,
// and the parameters are decided by the entry used
