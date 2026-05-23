#pragma once

#include "entities.h"

namespace ecs::entity {

template <typename... Components>
template <typename... Args>
    requires(sizeof...(Args) == sizeof...(Components))
Tuple<Components...>::Tuple(Args &&...args)
    : components_(std::forward<Args>(args)...) {}

template <typename... Components>
template <typename T>
    requires(std::same_as<T, Components> || ...)
T &Tuple<Components...>::get() {
    return std::get<T>(components_);
}

template <typename... Components>
template <typename T>
    requires(std::same_as<T, Components> || ...)
const T &Tuple<Components...>::get() const {
    return std::get<T>(components_);
}

template <typename Derived, typename First, typename... Rest>
template <typename F, typename... R>
Linear<Derived, First, Rest...>::Linear(F &&first, R &&...rest)
    : First(std::forward<F>(first)),
      Linear<Derived, Rest...>(std::forward<R>(rest)...) {}

template <typename Derived, typename First, typename... Rest>
template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
T &Linear<Derived, First, Rest...>::get() {
    if constexpr (std::same_as<T, First>) {
        return *this;
    } else {
        return Linear<Derived, Rest...>::template get<T>();
    }
}

template <typename Derived, typename First, typename... Rest>
template <typename F, typename... R>
Virtual<Derived, First, Rest...>::Virtual(F &&first, R &&...rest)
    : First(std::forward<F>(first)),
      Virtual<Derived, Rest...>(std::forward<R>(rest)...) {}

template <typename Derived, typename First, typename... Rest>
template <typename T>
    requires(std::same_as<T, First> || (std::same_as<T, Rest> || ...))
T &Virtual<Derived, First, Rest...>::get() {
    if constexpr (std::same_as<T, First>) {
        return *this;
    } else {
        return Virtual<Derived, Rest...>::template get<T>();
    }
}

} // namespace ecs::entity
