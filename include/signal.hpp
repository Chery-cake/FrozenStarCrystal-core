#pragma once

#include <cstdint>
#include <expected>
#include <flat_map>
#include <functional>
#include <memory>
#include <shared_mutex>

namespace core::signal {

using ConnectionId = uint64_t;

template <typename Signature> class Signal;
template <typename Signature> class ScopedConnection;

template <typename R, typename... Args> class Signal<R(Args...)> {
public:
  // --- Public Type Aliases ---
  using Slot = std::move_only_function<R(Args...)>;
  using SlotPtr = std::shared_ptr<Slot>;

  enum class ConnectError { None = 0, NullSlot, MaxConnectionsReached };

  using ConnectResult = std::expected<ConnectionId, ConnectError>;

  // --- Construction ---
  Signal();
  ~Signal();

  // No move & copy
  Signal(Signal &&) = delete;
  Signal &operator=(Signal &&) = delete;
  Signal(const Signal &) = delete;
  Signal &operator=(const Signal &) = delete;

  // --- Connection Management (Thread-Safe) ---
  [[nodiscard]] ConnectResult connect(Slot slot);
  bool disconnect(ConnectionId id);
  void clear();

  [[nodiscard]] bool empty() const;
  [[nodiscard]] size_t size() const;

  // --- Emission (Thread-Safe) ---
  void emit(Args... args) const;

  void emit_parallel(Args... args) const;

  template <typename Predicate>
  void emit_until(Args... args, Predicate &&pred) const;

  // --- Batch Processing Hint (Thread-Safe Snapshot) ---
  std::vector<ConnectionId> connection_ids() const;

private:
  // Internal helper to take a snapshot of current connections under lock
  std::vector<std::pair<ConnectionId, SlotPtr>> make_snapshot() const;

  // Data Members
  mutable std::shared_mutex mutex_;
  std::flat_map<ConnectionId, SlotPtr> connections_;
  ConnectionId next_id_ = 1; // Protected by mutex_
};

/**
 * @brief
 * Thread-safe connection manager for an entity or system.
 * Automatically disconnects all tracked connections when destroyed.
 */
class SignalHub {
public:
  using DisconnectFunc = std::move_only_function<void()>;

  SignalHub() = default;
  inline ~SignalHub();

  // No move & copy
  SignalHub(SignalHub &&) = delete;
  SignalHub &operator=(SignalHub &&) = delete;
  SignalHub(const SignalHub &) = delete;
  SignalHub &operator=(const SignalHub &) = delete;

  /**
   * Connect a slot to a signal and track it in this hub.
   * @return A ScopedConnection that can be used for manual control.
   */
  template <typename Signature, typename Slot>
  ScopedConnection<Signature> connect(Signal<Signature> &signal, Slot &&slot);

  /**
   * Manually add a disconnector function.
   */
  inline void add_disconnector(DisconnectFunc &&func);

  /**
   * Disconnect all tracked connections immediately.
   */
  inline void clear();

  [[nodiscard]] inline size_t size() const;
  [[nodiscard]] inline bool empty() const;

private:
  mutable std::shared_mutex mutex_;
  std::vector<DisconnectFunc> disconnectors_;
};

// ScopedConnection definition
template <typename Signature> class ScopedConnection {
public:
  using SignalType = Signal<Signature>;

  ScopedConnection() = default;
  ScopedConnection(SignalType &signal, ConnectionId id,
                   bool auto_disconnect = true);
  ~ScopedConnection();

  ScopedConnection(const ScopedConnection &) = delete;
  ScopedConnection &operator=(const ScopedConnection &) = delete;

  ScopedConnection(ScopedConnection &&other) noexcept;
  ScopedConnection &operator=(ScopedConnection &&other) noexcept;

  void reset();
  [[nodiscard]] bool is_connected() const;
  ConnectionId release();

private:
  SignalType *signal_ = nullptr;
  ConnectionId id_ = 0;
  bool auto_disconnect_ = true;
};

} // namespace core::signal

namespace ecs::component {
struct SignalHub {
  core::signal::SignalHub hub;

  // Convenience method to connect a slot to an external signal
  template <typename Signature, typename Slot>
  core::signal::ScopedConnection<Signature>
  connect(core::signal::Signal<Signature> &signal, Slot &&slot) {
    return hub.connect(signal, std::forward<Slot>(slot));
  }
};
} // namespace ecs::component

// Include implementation details
#include "../src/signal_impl.hpp"
