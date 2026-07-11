module;

#include "FrozenStarCrystal-core_export.h"
#include <cstdint>

export module signals:signal;

import std.compat;

export namespace signals {

enum class FROZENSTARCRYSTAL_CORE_API ConnectionError : uint8_t {
  None = 0,
  NullSlot,
  MaxConnectionsReached

};

using ConnectionId = uint64_t;
using ConnectionResult = std::expected<ConnectionId, ConnectionError>;

template <typename Signature> class FROZENSTARCRYSTAL_CORE_API Signals;
template <typename R, typename... Args> class Signals<R(Args...)> {
public:
  using Slot = std::move_only_function<R(Args...)>;
  using SlotPtr = std::shared_ptr<Slot>;

private:
  std::flat_map<ConnectionId, SlotPtr> connections_;
  ConnectionId next_id_ = 1;
  mutable std::shared_mutex mutex_;

  std::vector<std::pair<ConnectionId, SlotPtr>> make_snapshot() const;

public:
  Signals() = default;
  ~Signals() = default;

  Signals(Signals &&) = delete;
  Signals &operator=(Signals &&) = delete;
  Signals(const Signals &) = delete;
  Signals &operator=(const Signals &) = delete;

  [[nodiscard]] ConnectionResult connect(Slot slot);
  bool disconnect(ConnectionId id);
  void clear();

  [[nodiscard]] bool empty() const;
  [[nodiscard]] size_t size() const;

  void emit(Args... args) const;

  void emit_parallel(Args... args) const;

  template <typename Predicate>
    requires((std::is_void_v<R> && std::is_invocable_r_v<bool, Predicate>) ||
             (!std::is_void_v<R> && std::is_invocable_r_v<bool, Predicate, R>))
  void emit_until(Args... args, Predicate &&pred) const;

  std::vector<ConnectionId> connection_ids() const;
};

template <typename Signature> class FROZENSTARCRYSTAL_CORE_API ScopedConnection {
public:
  using SignalType = Signals<Signature>;

private:
  std::weak_ptr<SignalType> signal_;
  ConnectionId id_ = 0;
  bool auto_disconnect_ = true;

public:
  ScopedConnection() = default;
  ScopedConnection(std::shared_ptr<SignalType> signal, ConnectionId id,
                   bool auto_disconnect = true);
  ~ScopedConnection();

  ScopedConnection(const ScopedConnection &) = delete;
  ScopedConnection &operator=(const ScopedConnection &) = delete;
  ScopedConnection(ScopedConnection &&other) noexcept;
  ScopedConnection &operator=(ScopedConnection &&other) noexcept;

  void reset();
  [[nodiscard]] bool is_connected() const;
  ConnectionId release();
};

class FROZENSTARCRYSTAL_CORE_API SignalHub {
public:
  using DisconnectFunc = std::move_only_function<void()>;

private:
  std::vector<DisconnectFunc> disconnectors_;
  mutable std::shared_mutex mutex_;

public:
  SignalHub() = default;
  ~SignalHub();

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
    requires std::is_constructible_v<typename Signals<Signature>::Slot, Slot>
  std::expected<ScopedConnection<Signature>, ConnectionError>
  connect(std::shared_ptr<Signals<Signature>> signal, Slot &&slot);

  /**
   * Manually add a disconnector function.
   */
  void add_disconnector(DisconnectFunc &&func);

  /**
   * Disconnect all tracked connections immediately.
   */
  void clear();

  [[nodiscard]] size_t size() const;
  [[nodiscard]] bool empty() const;
};

} // namespace signals
