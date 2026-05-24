module;

export module resource;

import :policy;
import :registry;
import :registry_impl;

import std.compat;

/*

Exemple on how to use

struct Thing {
  std::vector<int> vec;
  std::unordered_map<float, std::string> map;
};

struct ThingDefinitions {
  inline static const Thing t1{.vec = {0, 1}, .map = {{1.5, "test"}}};
  inline static const Thing t2{.vec = {1, 2}, .map = {{2.5, "test"}}};
};

class Things {
  std::vector<int> vec;
  std::unordered_map<float, std::string> map;

public:
  explicit Things(const Thing &t) : vec(t.vec), map(t.map) {}
};

using ThingRegistry =
    resource::Registry<Thing, Things, resource::WeakPtrPolicy<Thing, Things>>;

ThingRegistry regis;

void init() {
  regis.emplace(&ThingDefinitions::t1);
  regis.emplace(&ThingDefinitions::t2);
}

*/
