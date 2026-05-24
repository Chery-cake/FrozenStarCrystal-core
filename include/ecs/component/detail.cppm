module;

export module ecs.component.detail;
import std.compat;

export namespace ecs::component::detail {

template <typename... Args> struct ComponentDependencies {
  using required = std::tuple<>;
};

/* Example specialisation for Renderable<N>
template <std::size_t N>
struct ComponentDependencies<Renderable<N>> {
using required = std::tuple<Transform<N>>;
};*/

// Check if a type T is present in Pack...
template <typename T, typename... Pack>
constexpr bool is_in_pack_v = (std::same_as<T, Pack> || ...);

// Check that all dependencies of every component in the pack are satisfied
template <typename... Components> constexpr bool all_dependencies_satisfied() {
  // For each component type...
  return ([]<typename Comp> {
    using Reqs = typename ComponentDependencies<Comp>::required;
    // For each required type in the tuple, verify it's in Components...
    auto check_one = []<typename Req>() {
      return is_in_pack_v<Req, Components...>;
    };
    // Use a simple recursive template to iterate tuple types
    return std::apply(
        [check_one](auto... req_types) {
          return (check_one.template operator()<decltype(req_types)>() && ...);
        },
        Reqs{});
  }.template operator()<Components>() &&
          ...);
}

} // namespace ecs::component::detail
