#pragma once

#include "signal.hpp"
#include <execution>
#include <mutex>

namespace core::signal {

template <typename R, typename... Args> Signal<R(Args...)>::Signal() = default;

template <typename R, typename... Args> Signal<R(Args...)>::~Signal() = default;

template <typename R, typename... Args>
typename Signal<R(Args...)>::ConnectResult
Signal<R(Args...)>::connect(Slot slot) {
  if (!slot) {
    return std::unexpected(ConnectError::NullSlot);
  }

  std::unique_lock lock(mutex_);
  ConnectionId id = next_id_++;
  auto slotPtr = std::make_shared<Slot>(std::move(slot));
  connections_.emplace(id, std::move(slotPtr));
  return id;
}

template <typename R, typename... Args>
bool Signal<R(Args...)>::disconnect(ConnectionId id) {
  std::unique_lock lock(mutex_);
  return connections_.erase(id) > 0;
}

template <typename R, typename... Args> void Signal<R(Args...)>::clear() {
  std::unique_lock lock(mutex_);
  connections_.clear();
}

template <typename R, typename... Args> bool Signal<R(Args...)>::empty() const {
  std::shared_lock lock(mutex_);
  return connections_.empty();
}

template <typename R, typename... Args>
size_t Signal<R(Args...)>::size() const {
  std::shared_lock lock(mutex_);
  return connections_.size();
}

template <typename R, typename... Args>
void Signal<R(Args...)>::emit(Args... args) const {
  // Take a snapshot under a shared (read) lock to avoid holding the lock
  // while invoking potentially slow or blocking callbacks.
  auto snapshot = make_snapshot();

  std::ranges::for_each(
      snapshot | std::views::filter([](const auto &pair) {
        return pair.second && *pair.second;
      }),
      [&args...](const auto &pair) { std::invoke(*pair.second, args...); });
}

template <typename R, typename... Args>
void Signal<R(Args...)>::emit_parallel(Args... args) const {
  // Take a snapshot under a shared (read) lock to avoid holding the lock
  // while invoking potentially slow or blocking callbacks.
  auto snapshot = make_snapshot();

  auto filter =
      snapshot | std::views::filter([](const auto &pair) {
        return (pair.second && *pair.second);
      }) |
      std::views::transform([](const auto &pair) { return pair.second; }) |
      std::ranges::to<std::vector>();

  std::for_each(
      std::execution::par_unseq, filter.begin(), filter.end(),
      [&args...](const SlotPtr &slot) { std::invoke(*slot, args...); });
}

template <typename R, typename... Args>
template <typename Predicate>
void Signal<R(Args...)>::emit_until(Args... args, Predicate &&pred) const {
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
std::vector<ConnectionId> Signal<R(Args...)>::connection_ids() const {
  std::shared_lock lock(mutex_);
  std::vector<ConnectionId> ids;
  ids.reserve(connections_.size());
  std::ranges::transform(connections_, std::back_inserter(ids),
                         [](const auto &pair) { return pair.first; });
  return ids;
}

template <typename R, typename... Args>
std::vector<std::pair<ConnectionId, typename Signal<R(Args...)>::SlotPtr>>
Signal<R(Args...)>::make_snapshot() const {
  std::shared_lock lock(mutex_);
  std::vector<std::pair<ConnectionId, SlotPtr>> snapshot;
  snapshot.reserve(connections_.size());
  std::ranges::transform(
      connections_, std::back_inserter(snapshot), [](const auto &pair) {
        return std::pair<ConnectionId, SlotPtr>(pair.first, pair.second);
      });
  return snapshot;
}

SignalHub::~SignalHub() { clear(); }

template <typename Signature, typename Slot>
ScopedConnection<Signature> SignalHub::connect(Signal<Signature> &signal,
                                               Slot &&slot) {
  auto result = signal.connect(std::forward<Slot>(slot));
  if (!result) {
    throw std::runtime_error("[SignalHub] connect failed");
  }
  ConnectionId id = *result;

  // Create a disconnector that captures the signal reference and id
  // Note: We assume the signal outlives the hub. If not, use weak_ptr
  // pattern.
  auto disconnector = [&signal, id] { signal.disconnect(id); };

  {
    std::unique_lock lock(mutex_);
    disconnectors_.emplace_back(std::move(disconnector));
  }

  return ScopedConnection<Signature>(signal, id, false);
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
  // Execute outside lock to avoid deadlocks
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

// ScopedConnection template implementations
template <typename Signature>
ScopedConnection<Signature>::ScopedConnection(SignalType &signal,
                                              ConnectionId id,
                                              bool auto_disconnect)
    : signal_(&signal), id_(id), auto_disconnect_(auto_disconnect) {}

template <typename Signature> ScopedConnection<Signature>::~ScopedConnection() {
  if (auto_disconnect_) {
    reset();
  }
}

template <typename Signature>
ScopedConnection<Signature>::ScopedConnection(ScopedConnection &&other) noexcept
    : signal_(std::exchange(other.signal_, nullptr)),
      id_(std::exchange(other.id_, 0)),
      auto_disconnect_(other.auto_disconnect_) {}

template <typename Signature>
ScopedConnection<Signature> &
ScopedConnection<Signature>::operator=(ScopedConnection &&other) noexcept {
  if (this != &other) {
    reset();
    signal_ = std::exchange(other.signal_, nullptr);
    id_ = std::exchange(other.id_, 0);
    auto_disconnect_ = other.auto_disconnect_;
  }
  return *this;
}

template <typename Signature> void ScopedConnection<Signature>::reset() {
  if (signal_ && id_ != 0) {
    signal_->disconnect(id_);
    signal_ = nullptr;
    id_ = 0;
  }
}

template <typename Signature>
bool ScopedConnection<Signature>::is_connected() const {
  return signal_ != nullptr && id_ != 0;
}

template <typename Signature>
ConnectionId ScopedConnection<Signature>::release() {
  signal_ = nullptr;
  return std::exchange(id_, 0);
}

} // namespace core::signal
