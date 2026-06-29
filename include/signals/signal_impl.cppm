module;

export module signals:signal_impl;

import :signal;
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
            return std::pair<ConnectionId, SlotPtr>{pair.first, pair.second};
        });
    return snapshot;
}

template <typename R, typename... Args>
ConnectionResult Signals<R(Args...)>::connect(Slot slot) {
    if (!slot) {
        return std::unexpected{ConnectionError::NullSlot};
    }

    std::unique_lock lock(mutex_);
    ConnectionId id = next_id_++;
    if (id == 0) {
        return std::unexpected{ConnectionError::MaxConnectionsReached};
    }

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
        }) |
            std::views::transform([](const auto &pair) { return pair.second; }),
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
    requires((std::is_void_v<R> && std::is_invocable_r_v<bool, Predicate>) ||
             (!std::is_void_v<R> && std::is_invocable_r_v<bool, Predicate, R>))
void Signals<R(Args...)>::emit_until(Args... args, Predicate &&pred) const {
    auto snapshot = make_snapshot();

    auto filter =
        snapshot | std::views::filter([](const auto &pair) {
            return pair.second && *pair.second;
        }) |
        std::views::transform([](const auto &pair) { return pair.second; });

    auto result =
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
ScopedConnection<Signature>::ScopedConnection(ScopedConnection &&other) noexcept
    : signal_(std::move(other.signal_)), id_(std::exchange(other.id_, 0)),
      auto_disconnect_(std::exchange(other.auto_disconnect_, false)) {}

template <typename Signature>
ScopedConnection<Signature> &
ScopedConnection<Signature>::operator=(ScopedConnection &&other) noexcept {
    if (this != &other) {
        if (auto_disconnect_) {
            reset();
        }
        signal_ = std::move(other.signal_);
        id_ = std::exchange(other.id_, 0);
        auto_disconnect_ = std::exchange(other.auto_disconnect_, false);
    }
    return *this;
}

template <typename Signature>
ScopedConnection<Signature>::ScopedConnection(
    std::shared_ptr<SignalType> signal, ConnectionId id, bool auto_disconnect)
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
    requires std::is_constructible_v<typename Signals<Signature>::Slot, Slot>
std::expected<ScopedConnection<Signature>, ConnectionError>
SignalHub::connect(std::shared_ptr<Signals<Signature>> signal, Slot &&slot) {
    if (!signal) {
        return std::unexpected(ConnectionError::NullSlot);
    }

    ConnectionResult result = signal->connect(std::forward<Slot>(slot));
    if (!result) {
        return std::unexpected(result.error());
    }
    ConnectionId id = result.value();

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
