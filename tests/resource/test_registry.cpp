import resource_helper;

#include <cassert>

using registryUnique =
    resource::Registry<tags, resources,
                       resource::UniquePtrPolicy<tags, resources>>;

using registryShared =
    resource::Registry<tags, resources,
                       resource::SharedPtrPolicy<tags, resources>>;

using registryWeak =
    resource::Registry<tags, resources,
                       resource::WeakPtrPolicy<tags, resources>>;

static inline const tags T4{.x = 4, .y = 4.5};

// -------------------------- add tests --------------------------

void test_add_unique() {
    TEST("add unique");

    registryUnique unique;
    std::vector<int> vec{5, 5};

    unique.add(&tagsCreated::T0, std::make_unique<resources>(tagsCreated::T0));
    unique.add(&tagsCreated::T1, std::make_unique<resources>(tagsCreated::T1));
    unique.add(&tagsCreated::T2,
               std::make_unique<resources>(tagsCreated::T2, vec));
    unique.add(&T3, std::make_unique<resources>(T3));
    unique.add(&T4, std::make_unique<resources>(T4));

    assert(unique.size() == 5);
    assert(unique.get(&tagsCreated::T2)->vec == vec);

    assert(unique.get(&tagsCreated::T0) != unique.get(&tagsCreated::T1));
    assert(unique.get(&tagsCreated::T1) != unique.get(&tagsCreated::T2));
    assert(unique.get(&tagsCreated::T2) != unique.get(&T3));
    assert(unique.get(&T3) != unique.get(&T4));

    assert(unique.getEntry(&tagsCreated::T2).resource->vec == vec);

    assert(unique.getEntry(&tagsCreated::T0) !=
           unique.getEntry(&tagsCreated::T1));
    assert(unique.getEntry(&tagsCreated::T1) !=
           unique.getEntry(&tagsCreated::T2));
    assert(unique.getEntry(&tagsCreated::T2) != unique.getEntry(&T3));
    assert(unique.getEntry(&T3) != unique.getEntry(&T4));

    unique.clear();
    assert(unique.size() == 0);
    assert(unique.empty());

    PASS();
}

void test_add_shared() {
    TEST("add shared");

    registryShared shared;
    std::vector<int> vec{5, 5};

    shared.add(&tagsCreated::T0, std::make_shared<resources>(tagsCreated::T0));
    shared.add(&tagsCreated::T1, std::make_shared<resources>(tagsCreated::T1));
    shared.add(&tagsCreated::T2,
               std::make_unique<resources>(tagsCreated::T2, vec));
    shared.add(&T3, std::make_shared<resources>(T3));
    shared.add(&T4, std::make_shared<resources>(T4));

    assert(shared.size() == 5);
    assert(shared.get(&tagsCreated::T2)->vec == vec);

    assert(shared.get(&tagsCreated::T0) != shared.get(&tagsCreated::T1));
    assert(shared.get(&tagsCreated::T1) != shared.get(&tagsCreated::T2));
    assert(shared.get(&tagsCreated::T2) != shared.get(&T3));
    assert(shared.get(&T3) != shared.get(&T4));

    auto s0 = shared.getStored(&tagsCreated::T0);
    auto s1 = shared.getStored(&tagsCreated::T1);
    auto s2 = shared.getStored(&tagsCreated::T2);
    auto s3 = shared.getStored(&T3);
    auto s4 = shared.getStored(&T4);

    assert(shared.getStored(&tagsCreated::T0) == s0);
    assert(shared.getStored(&tagsCreated::T1) == s1);
    assert(shared.getStored(&tagsCreated::T2) == s2);
    assert(shared.getStored(&T3) == s3);
    assert(shared.getStored(&T4) == s4);

    // 1 registry, 1 extra var, 1 var to check count
    assert(shared.getStored(&tagsCreated::T0).use_count() == 3);
    assert(shared.getStored(&tagsCreated::T1).use_count() == 3);
    assert(shared.getStored(&tagsCreated::T2).use_count() == 3);
    assert(shared.getStored(&T3).use_count() == 3);
    assert(shared.getStored(&T4).use_count() == 3);

    s0.reset();
    s1.reset();
    s2.reset();
    s3.reset();
    s4.reset();

    // 1 registry, 1 var to check count
    assert(shared.getStored(&tagsCreated::T0).use_count() == 2);
    assert(shared.getStored(&tagsCreated::T1).use_count() == 2);
    assert(shared.getStored(&tagsCreated::T2).use_count() == 2);
    assert(shared.getStored(&T3).use_count() == 2);
    assert(shared.getStored(&T4).use_count() == 2);

    assert(shared.getEntry(&tagsCreated::T2).resource->vec == vec);

    assert(shared.getEntry(&tagsCreated::T0) !=
           shared.getEntry(&tagsCreated::T1));
    assert(shared.getEntry(&tagsCreated::T1) !=
           shared.getEntry(&tagsCreated::T2));
    assert(shared.getEntry(&tagsCreated::T2) != shared.getEntry(&T3));
    assert(shared.getEntry(&T3) != shared.getEntry(&T4));

    shared.clear();
    assert(shared.size() == 0);
    assert(shared.empty());

    PASS();
}

void test_add_weak() {
    TEST("add weak");

    registryWeak weak;
    std::vector<int> vec{5, 5};

    auto sp0 = std::make_shared<resources>(tagsCreated::T0);
    auto sp1 = std::make_shared<resources>(tagsCreated::T1);
    auto sp2 = std::make_shared<resources>(tagsCreated::T2, vec);
    auto sp3 = std::make_shared<resources>(T3);
    auto sp4 = std::make_shared<resources>(T4);

    weak.add(&tagsCreated::T0, sp0);
    weak.add(&tagsCreated::T1, sp1);
    weak.add(&tagsCreated::T2, sp2);
    weak.add(&T3, sp3);
    weak.add(&T4, sp4);

    assert(weak.size() == 5);
    assert(weak.get(&tagsCreated::T2)->vec == vec);

    assert(weak.get(&tagsCreated::T0) != weak.get(&tagsCreated::T1));
    assert(weak.get(&tagsCreated::T1) != weak.get(&tagsCreated::T2));
    assert(weak.get(&tagsCreated::T2) != weak.get(&T3));
    assert(weak.get(&T3) != weak.get(&T4));

    auto w0 = weak.getStored(&tagsCreated::T0);
    auto w1 = weak.getStored(&tagsCreated::T1);
    auto w2 = weak.getStored(&tagsCreated::T2);
    auto w3 = weak.getStored(&T3);
    auto w4 = weak.getStored(&T4);

    assert(weak.getStored(&tagsCreated::T0).lock() == sp0);
    assert(weak.getStored(&tagsCreated::T1).lock() == sp1);
    assert(weak.getStored(&tagsCreated::T2).lock() == sp2);
    assert(weak.getStored(&T3).lock() == sp3);
    assert(weak.getStored(&T4).lock() == sp4);

    assert(weak.getStored(&tagsCreated::T0).lock() == w0.lock());
    assert(weak.getStored(&tagsCreated::T1).lock() == w1.lock());
    assert(weak.getStored(&tagsCreated::T2).lock() == w2.lock());
    assert(weak.getStored(&T3).lock() == w3.lock());
    assert(weak.getStored(&T4).lock() == w4.lock());

    // 1 registry
    assert(weak.getStored(&tagsCreated::T0).use_count() == 1);
    assert(weak.getStored(&tagsCreated::T1).use_count() == 1);
    assert(weak.getStored(&tagsCreated::T2).use_count() == 1);
    assert(weak.getStored(&T3).use_count() == 1);
    assert(weak.getStored(&T4).use_count() == 1);

    w0.reset();
    w1.reset();
    w2.reset();
    w3.reset();
    w4.reset();

    // 1 registry
    assert(weak.getStored(&tagsCreated::T0).use_count() == 1);
    assert(weak.getStored(&tagsCreated::T1).use_count() == 1);
    assert(weak.getStored(&tagsCreated::T2).use_count() == 1);
    assert(weak.getStored(&T3).use_count() == 1);
    assert(weak.getStored(&T4).use_count() == 1);

    assert(weak.getEntry(&tagsCreated::T2).resource->vec == vec);

    assert(weak.getEntry(&tagsCreated::T0) != weak.getEntry(&tagsCreated::T1));
    assert(weak.getEntry(&tagsCreated::T1) != weak.getEntry(&tagsCreated::T2));
    assert(weak.getEntry(&tagsCreated::T2) != weak.getEntry(&T3));
    assert(weak.getEntry(&T3) != weak.getEntry(&T4));

    weak.clear();
    assert(weak.size() == 0);
    assert(weak.empty());

    PASS();
}

// ------------------------ emplace tests -------------------------

void test_emplace_unique() {
    TEST("emplace unique");

    registryUnique unique;
    std::vector<int> vec{5, 5};

    unique.emplace(&tagsCreated::T0);
    unique.emplace(&tagsCreated::T1);
    unique.emplace(&tagsCreated::T2, vec);
    unique.emplace(&T3);
    unique.emplace(&T4);

    assert(unique.size() == 5);
    assert(unique.get(&tagsCreated::T2)->vec == vec);

    assert(unique.get(&tagsCreated::T0) != unique.get(&tagsCreated::T1));
    assert(unique.get(&tagsCreated::T1) != unique.get(&tagsCreated::T2));
    assert(unique.get(&tagsCreated::T2) != unique.get(&T3));
    assert(unique.get(&T3) != unique.get(&T4));

    assert(unique.getEntry(&tagsCreated::T2).resource->vec == vec);

    assert(unique.getEntry(&tagsCreated::T0) !=
           unique.getEntry(&tagsCreated::T1));
    assert(unique.getEntry(&tagsCreated::T1) !=
           unique.getEntry(&tagsCreated::T2));
    assert(unique.getEntry(&tagsCreated::T2) != unique.getEntry(&T3));
    assert(unique.getEntry(&T3) != unique.getEntry(&T4));

    unique.clear();
    assert(unique.size() == 0);
    assert(unique.empty());

    PASS();
}

void test_emplace_shared() {
    TEST("emplace shared");

    registryShared shared;
    std::vector<int> vec{5, 5};

    shared.emplace(&tagsCreated::T0);
    shared.emplace(&tagsCreated::T1);
    shared.emplace(&tagsCreated::T2, vec);
    shared.emplace(&T3);
    shared.emplace(&T4);

    assert(shared.size() == 5);
    assert(shared.get(&tagsCreated::T2)->vec == vec);

    assert(shared.get(&tagsCreated::T0) != shared.get(&tagsCreated::T1));
    assert(shared.get(&tagsCreated::T1) != shared.get(&tagsCreated::T2));
    assert(shared.get(&tagsCreated::T2) != shared.get(&T3));
    assert(shared.get(&T3) != shared.get(&T4));

    assert(shared.getEntry(&tagsCreated::T2).resource->vec == vec);

    assert(shared.getEntry(&tagsCreated::T0) !=
           shared.getEntry(&tagsCreated::T1));
    assert(shared.getEntry(&tagsCreated::T1) !=
           shared.getEntry(&tagsCreated::T2));
    assert(shared.getEntry(&tagsCreated::T2) != shared.getEntry(&T3));
    assert(shared.getEntry(&T3) != shared.getEntry(&T4));

    shared.clear();
    assert(shared.size() == 0);
    assert(shared.empty());

    PASS();
}

void test_signals() {
    TEST("signals");

    registryUnique unique;

    int count = 0;

    auto func = [&count](const tags *, resources *) { ++count; };

    auto resultAdd = unique.onAdd(func);
    auto resultRe = unique.onRemove(func);

    assert(resultAdd.has_value());
    assert(resultRe.has_value());

    unique.emplace(&tagsCreated::T0);
    unique.emplace(&tagsCreated::T1);
    unique.emplace(&tagsCreated::T2);

    assert(count == 3);

    unique.clear();

    assert(count == 6);

    unique.clearSignals();

    unique.emplace(&tagsCreated::T0);
    unique.emplace(&tagsCreated::T1);
    unique.emplace(&tagsCreated::T2);

    assert(count == 6);

    unique.clear();

    assert(count == 6);

    PASS();
}

void test_list_unique() {
    TEST("list");

    registryUnique unique;
    std::vector<int> vec{5, 5};

    unique.add(&tagsCreated::T0, std::make_unique<resources>(tagsCreated::T0));
    unique.add(&tagsCreated::T1, std::make_unique<resources>(tagsCreated::T1));
    unique.add(&tagsCreated::T2,
               std::make_unique<resources>(tagsCreated::T2, vec));
    unique.add(&T3, std::make_unique<resources>(T3));
    unique.add(&T4, std::make_unique<resources>(T4));

    auto vecList = unique.getAll();

    assert(vecList.size() == 5);
    assert(unique.getAll()[0] == vecList[0]);

    vecList.pop_back();
    assert(unique.getAll().size() == 5);

    auto *st = unique.get(&tagsCreated::T0);
    auto ex = unique.extract(&tagsCreated::T0);
    assert(st == ex.get());
    assert(unique.getAll().size() == 4);

    assert(!unique.remove(&tagsCreated::T0));
    assert(!unique.contains(&tagsCreated::T0));
    assert(unique.contains(&tagsCreated::T1));
    assert(unique.remove(&tagsCreated::T1));
    assert(!unique.contains(&tagsCreated::T1));

    unique.clear();

    unique.add(&tagsCreated::T0, std::make_unique<resources>(tagsCreated::T0));
    unique.add(&tagsCreated::T1, std::make_unique<resources>(tagsCreated::T1));
    unique.add(&tagsCreated::T2, std::make_unique<resources>(tagsCreated::T2));
    unique.add(&T3, std::make_unique<resources>(T3));
    unique.add(&T4, std::make_unique<resources>(T4));

    int count = 0;
    auto func = [&count](const tags *, resources *) { ++count; };

    unique.forEach(func);

    assert(count == 5);

    assert(unique.get(&tagsCreated::T0)->vec.empty());
    unique.set(&tagsCreated::T0,
               std::make_unique<resources>(tagsCreated::T0, vec));
    assert(!unique.get(&tagsCreated::T0)->vec.empty());

    PASS();
}

void test_list_shared() {
    TEST("list");

    registryShared shared;
    std::vector<int> vec{5, 5};

    shared.add(&tagsCreated::T0, std::make_shared<resources>(tagsCreated::T0));
    shared.add(&tagsCreated::T1, std::make_shared<resources>(tagsCreated::T1));
    shared.add(&tagsCreated::T2,
               std::make_shared<resources>(tagsCreated::T2, vec));
    shared.add(&T3, std::make_shared<resources>(T3));
    shared.add(&T4, std::make_shared<resources>(T4));

    auto vecList = shared.getAll();

    assert(vecList.size() == 5);
    assert(shared.getAll()[0] == vecList[0]);

    vecList.pop_back();
    assert(shared.getAll().size() == 5);

    auto *st = shared.get(&tagsCreated::T0);
    auto ex = shared.extract(&tagsCreated::T0);
    assert(st == ex.get());
    assert(shared.getAll().size() == 4);

    assert(!shared.remove(&tagsCreated::T0));
    assert(!shared.contains(&tagsCreated::T0));
    assert(shared.contains(&tagsCreated::T1));
    assert(shared.remove(&tagsCreated::T1));
    assert(!shared.contains(&tagsCreated::T1));

    shared.clear();

    shared.add(&tagsCreated::T0, std::make_shared<resources>(tagsCreated::T0));
    shared.add(&tagsCreated::T1, std::make_shared<resources>(tagsCreated::T1));
    shared.add(&tagsCreated::T2, std::make_shared<resources>(tagsCreated::T2));
    shared.add(&T3, std::make_shared<resources>(T3));
    shared.add(&T4, std::make_shared<resources>(T4));

    int count = 0;
    auto func = [&count](const tags *, resources *) { ++count; };

    shared.forEach(func);

    assert(count == 5);

    assert(shared.get(&tagsCreated::T0)->vec.empty());
    shared.set(&tagsCreated::T0,
               std::make_shared<resources>(tagsCreated::T0, vec));
    assert(!shared.get(&tagsCreated::T0)->vec.empty());

    PASS();
}

void test_list_weak() {
    TEST("list");

    registryWeak weak;
    std::vector<int> vec{5, 5};

    auto res0 = std::make_shared<resources>(tagsCreated::T0);
    auto res1 = std::make_shared<resources>(tagsCreated::T1);
    auto res2 = std::make_shared<resources>(tagsCreated::T2, vec);
    auto res3 = std::make_shared<resources>(T3);
    auto res4 = std::make_shared<resources>(T4);

    weak.add(&tagsCreated::T0, res0);
    weak.add(&tagsCreated::T1, res1);
    weak.add(&tagsCreated::T2, res2);
    weak.add(&T3, res3);
    weak.add(&T4, res4);

    auto vecList = weak.getAll();

    assert(vecList.size() == 5);
    assert(weak.getAll()[0] == vecList[0]);

    vecList.pop_back();
    assert(weak.getAll().size() == 5);

    auto st = weak.get(&tagsCreated::T0);
    auto ex = weak.extract(&tagsCreated::T0);
    assert(st == ex);
    assert(weak.getAll().size() == 4);

    assert(!weak.remove(&tagsCreated::T0));
    assert(!weak.contains(&tagsCreated::T0));
    assert(weak.contains(&tagsCreated::T1));
    assert(weak.remove(&tagsCreated::T1));
    assert(!weak.contains(&tagsCreated::T1));

    weak.clear();

    weak.add(&tagsCreated::T0, res0);
    weak.add(&tagsCreated::T1, res1);
    weak.add(&tagsCreated::T2, res2);
    weak.add(&T3, res3);
    weak.add(&T4, res4);

    int count = 0;
    auto func = [&count](const tags *, std::shared_ptr<resources>) { ++count; };

    weak.forEach(func);

    assert(count == 5);

    auto res01 = std::make_shared<resources>(tagsCreated::T0, vec);

    assert(weak.get(&tagsCreated::T0)->vec.empty());
    weak.set(&tagsCreated::T0, res01);
    assert(!weak.get(&tagsCreated::T0)->vec.empty());

    PASS();
}

int main() {
    std::println("=== Resoure Registry Tests ===");

    test_add_unique();
    test_add_shared();
    test_add_weak();

    test_emplace_unique();
    test_emplace_shared();

    test_signals();

    test_list_unique();
    test_list_shared();
    test_list_weak();

    std::println("\n{}/{} tests passed", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
