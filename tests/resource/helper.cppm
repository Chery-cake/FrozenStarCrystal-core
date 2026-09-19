module;

export module resource_helper;

export import std.compat;
export import resource;

export {
  int tests_run = 0;
  int tests_passed = 0;

  void TEST(std::string_view name) {
    ++tests_run;
    std::println("[TEST] {} ... ", name);
  }

  void PASS() {
    ++tests_passed;
    std::println("PASSED");
  }

  struct tags {
    int x = 0;
    float y = 0.5;
  };

  struct tagsCreated {
    inline static const tags T0;
    inline static const tags T1{.x = 1, .y = 1.5};
    inline static const tags T2{.x = 2, .y = 2.5};
  };
  inline const tags T3{.x = 3, .y = 3.5};

  struct resources {
    int x;
    float y;
    std::vector<int> vec;

    resources(const tags &tag) : x(tag.x), y(tag.y) {};
    resources(const tags &tag, std::vector<int> &v)
        : x(tag.x), y(tag.y), vec(v) {}

    bool operator<=>(const resources &) const = default;
  };

  struct resourcesNoTag {
    int x = 0;
    float y = 0.5F;
    std::vector<int> vec;

    resourcesNoTag() = default;
    explicit resourcesNoTag(std::vector<int> v) : vec(std::move(v)) {}
    resourcesNoTag(int xIn, float yIn) : x(xIn), y(yIn) {}
    resourcesNoTag(int xIn, float yIn, std::vector<int> v)
        : x(xIn), y(yIn), vec(std::move(v)) {}

    auto operator<=>(const resourcesNoTag &) const = default;
  };

  struct resourcesAmbiguous {
    int x = 0;
    float y = 0.5F;
    bool fromTag = false;

    resourcesAmbiguous() = default;
    resourcesAmbiguous(const tags &tag) : x(tag.x), y(tag.y), fromTag(true) {}
    resourcesAmbiguous(int xIn, float yIn) : x(xIn), y(yIn), fromTag(false) {}

    auto operator<=>(const resourcesAmbiguous &) const = default;
  };
}
