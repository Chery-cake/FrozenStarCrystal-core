module;

export module signals:impl;

import signals;
import std.compat;

export namespace signals {

template <typename R, typename... Args>
std::vector<std::pair<ConnectionId, typename Signals<R(Args...)>::SlotPtr>>
Signals<R(Args...)>::make_snapshot() const {
  std::shared_lock lock(mutex_);
  std::vector<std::pair<ConnectionId, SlotPtr>> snapshot;
  snapshot.reserve(connections_.size());
  std::ranges::transform(
      connections_, std::back_inserter(snapshot), [](const auto &pair) {
        return std::pair<ConnectionId, SlotPtr>(pair.first, pair.second);
      });
  return snapshot;
}

template <typename R, typename... Args>
ConnectionResult Signals<R(Args...)>::connect(Slot slot) {
  if (!slot) {
    return std::unexpected(ConnectionError::NullSlot);
  }

  std::unique_lock lock(mutex_);
  ConnectionId id = next_id_++;
  auto slotPtr = std::make_shared<Slot>(std::move(slot));
  connections_.emplace(id, std::move(slotPtr));
  return id;
}

template <typename R, typename... Args>
bool Signals<R(Args...)>::disconnect(ConnectionId id) {
  std::unique_lock lock(mutex_);
  return connections_.erase(id);
}

template <typename R, typename... Args> void Signals<R(Args...)>::clear() {
  std::unique_lock lock(mutex_);
  connections_.clear();
}

template <typename R, typename... Args>
bool Signals<R(Args...)>::empty() const {
  std::shared_lock lock(mutex_);
  return connections_.empty();
}

template <typename R, typename... Args>
size_t Signals<R(Args...)>::size() const {
  std::shared_lock lock(mutex_);
  return connections_.size();
}

template <typename R, typename... Args>
void Signals<R(Args...)>::emit(Args... args) const {
  auto snapshot = make_snapshot();

  std::ranges::for_each(
      snapshot | std::views::filter([](const auto &pair) {
        return pair.second && *pair.second;
      }) | std::views::transform([](const auto &pair) { return pair.second; }),
      [&args...](const SlotPtr &slot) { std::invoke(*slot, args...); });
}

template <typename R, typename... Args>
void Signals<R(Args...)>::emit_parallel(Args... args) const {
  auto snapshot = make_snapshot();

  auto filter =
      snapshot | std::views::filter([](const auto &pair) {
        return pair.second && *pair.second;
      }) |
      std::views::transform([](const auto &pair) { return pair.second; }) |
      std::ranges::to<std::vector>();

  std::for_each(
      std::execution::par_unseq, filter.begin(), filter.end(),
      [&args...](const SlotPtr &slot) { std::invoke(*slot, args...); });
}

template <typename R, typename... Args>
template <typename Predicate>
void Signals<R(Args...)>::emit_until(Args... args, Predicate &&pred) const {
  auto snapshot = make_snapshot();

  auto filter =
      snapshot | std::views::filter([](const auto &pair) {
        return pair.second && *pair.second;
      }) |
      std::views::transform([](const auto &pair) { return pair.second; });

  std::ranges::find_if(filter, [&pred, &args...](const SlotPtr &slot) {
    if constexpr (std::is_void_v<R>) {
      std::invoke(*slot, args...);
      return pred();
    } else {
      return pred(std::invoke(*slot, args...));
    }
  });
}

template <typename R, typename... Args>
std::vector<ConnectionId> Signals<R(Args...)>::connection_ids() const {
  std::shared_lock lock(mutex_);
  std::vector<ConnectionId> ids;
  ids.reserve(connections_.size());
  std::ranges::transform(connections_, std::back_inserter(ids),
                         [](const auto &pair) { return pair.first; });
  return ids;
}

template <typename Signature>
ScopedConnection<Signature>::ScopedConnection(SignalType &signal,
                                              ConnectionId id,
                                              bool auto_disconnect)
    : signal_(std::move(signal)), id_(id), auto_disconnect_(auto_disconnect) {}

template <typename Signature> ScopedConnection<Signature>::~ScopedConnection() {
  if (auto_disconnect_) {
    reset();
  }
}

template <typename Signature> void ScopedConnection<Signature>::reset() {
  if (id_ == 0) {
    return;
  }

  if (auto sig = signal_.lock()) {
    sig->disconnect(id_);
  }
  signal_.reset();
  id_ = 0;
}

template <typename Signature>
bool ScopedConnection<Signature>::is_connected() const {
  return id_ != 0 && !signal_.expired();
}

template <typename Signature>
ConnectionId ScopedConnection<Signature>::release() {
  signal_.reset();
  return std::exchange(id_, 0);
}

SignalHub::~SignalHub() { clear(); }

template <typename Signature, typename Slot>
ScopedConnection<Signature> SignalHub::connect(
    std::shared_ptr<Signals<Signature>> signal,
    Slot &&slot) { // TODO remove error handling with throw/exceptions
  if (!signal) {
    throw std::runtime_error("[SignalHub] connection failed: null signal");
  }

  auto idResult = signal->connect(std::forward(slot));
  if (!idResult) {
    throw std::runtime_error("[SignalHub] connection failed");
  }
  ConnectionId id = *idResult;

  std::weak_ptr<Signals<Signature>> weakSig = signal;

  auto disconector = [weakSig, id] {
    if (auto sig = weakSig.lock()) {
      sig->disconnect(id);
    }
  };

  {
    std::unique_lock lock(mutex_);
    disconnectors_.emplace_back(std::move(disconector));
  }

  return ScopedConnection<Signature>(weakSig, id, false);
}

void SignalHub::add_disconnector(DisconnectFunc &&func) {
  std::unique_lock lock(mutex_);
  disconnectors_.emplace_back(std::move(func));
}

void SignalHub::clear() {
  std::vector<DisconnectFunc> local_disconnectors;
  {
    std::unique_lock lock(mutex_);
    local_disconnectors.swap(disconnectors_);
  }
  std::ranges::for_each(local_disconnectors | std::views::reverse |
                            std::views::filter([](const auto &disconcector) {
                              return !!disconcector;
                            }),
                        [](auto &disconnector) { disconnector(); });
}

size_t SignalHub::size() const {
  std::shared_lock lock(mutex_);
  return disconnectors_.size();
}

bool SignalHub::empty() const {
  std::shared_lock lock(mutex_);
  return disconnectors_.empty();
}

} // namespace signals
