#include <cstdio>
#include <cstdlib>
#include <utility>

#include "SharedPtr.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, int line) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL line %d: %s\n", line, expression);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

// Counts live instances, so we can tell whether objects are destroyed at the right moment.
struct Tracked {
    static inline int alive = 0;

    int value = 0;

    Tracked() {
        ++alive;
    }

    explicit Tracked(int v) : value(v) {
        ++alive;
    }

    Tracked(const Tracked& other) : value(other.value) {
        ++alive;
    }

    ~Tracked() {
        --alive;
    }
};

// Two constructor arguments, to check that makeSharedBasic forwards more than one argument.
struct Pair {
    int first;
    double second;

    Pair(int first, double second) : first(first), second(second) {}
};

// The copy constructor is deleted, so this type only compiles if arguments really are forwarded.
struct MoveOnly {
    int value;

    explicit MoveOnly(int value) : value(value) {}
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
};

// The aliasing example from the lab page.
struct BigObject {
    int payload;

    explicit BigObject(int payload = 0) : payload(payload) {}
};

struct BigObjectManager {
    static inline int destroyed = 0;

    BigObject bigObject;

    BigObjectManager() : bigObject(42) {}

    ~BigObjectManager() {
        ++destroyed;
    }
};

void testEmptySharedPtr() {
    SharedPtr<Tracked> first;
    SharedPtr<Tracked> second;

    CHECK(first.get() == nullptr);
    CHECK(!static_cast<bool>(first));
    CHECK(first.useCount() == 0);
    CHECK(first == second);  // both store nullptr
    CHECK(!(first != second));

    first.swap(second);
    CHECK(first.get() == nullptr);
    CHECK(second.get() == nullptr);
    CHECK(first.useCount() == 0);

    first.reset();
    CHECK(first.get() == nullptr);
    CHECK(first.useCount() == 0);
}

void testConstructionFromRawPointer() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> ptr(new Tracked(7));

        CHECK(static_cast<bool>(ptr));
        CHECK(ptr.get() != nullptr);
        CHECK(ptr.useCount() == 1);
        CHECK(Tracked::alive == before + 1);
        CHECK((*ptr).value == 7);
        CHECK(ptr->value == 7);
    }
    CHECK(Tracked::alive == before);  // the destructor deleted the managed object
}

void testCopySharesOwnership() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> original(new Tracked(3));
        SharedPtr<Tracked> copy(original);

        CHECK(copy.get() == original.get());
        CHECK(copy == original);
        CHECK(!(copy != original));
        CHECK(original.useCount() == 2);
        CHECK(copy.useCount() == 2);
        CHECK(Tracked::alive == before + 1);

        copy.reset();
        CHECK(!copy);
        CHECK(original.useCount() == 1);
        CHECK(original->value == 3);
        CHECK(Tracked::alive == before + 1);  // still owned by original
    }
    CHECK(Tracked::alive == before);
}

void testCopyAssignmentReleasesPreviousObject() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> target(new Tracked(1));
        SharedPtr<Tracked> source(new Tracked(2));
        CHECK(Tracked::alive == before + 2);

        target = source;
        CHECK(Tracked::alive == before + 1);  // the object target used to own is gone
        CHECK(target.get() == source.get());
        CHECK(target.useCount() == 2);
        CHECK(source.useCount() == 2);

        // Self-assignment has to leave the pointer untouched.
        SharedPtr<Tracked>& alias = target;
        target = alias;
        CHECK(target.get() == source.get());
        CHECK(target.useCount() == 2);
        CHECK(Tracked::alive == before + 1);
    }
    CHECK(Tracked::alive == before);
}

void testMoveTransfersOwnership() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> source(new Tracked(4));
        SharedPtr<Tracked> moved(std::move(source));

        CHECK(!source);
        CHECK(source.get() == nullptr);
        CHECK(source.useCount() == 0);
        CHECK(moved->value == 4);
        CHECK(moved.useCount() == 1);  // moving does not change the refcount
        CHECK(Tracked::alive == before + 1);

        SharedPtr<Tracked> target(new Tracked(5));
        target = std::move(moved);
        CHECK(!moved);
        CHECK(target->value == 4);
        CHECK(target.useCount() == 1);
        CHECK(Tracked::alive == before + 1);  // the object target used to own is gone

        // Self move-assignment must not empty the pointer.
        SharedPtr<Tracked>& alias = target;
        target = std::move(alias);
        CHECK(target->value == 4);
        CHECK(target.useCount() == 1);
    }
    CHECK(Tracked::alive == before);
}

void testSwap() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> a(new Tracked(1));
        SharedPtr<Tracked> b(new Tracked(2));
        Tracked* aAddress = a.get();
        Tracked* bAddress = b.get();

        a.swap(b);
        CHECK(a.get() == bAddress);
        CHECK(b.get() == aAddress);
        CHECK(a->value == 2);
        CHECK(b->value == 1);
        CHECK(a.useCount() == 1);  // swapping pointers does not touch either refcount
        CHECK(b.useCount() == 1);

        // Swapping two pointers that share the same resource also leaves the counts alone.
        SharedPtr<Tracked> shared = a;
        CHECK(a.useCount() == 2);
        a.swap(shared);
        CHECK(a.get() == bAddress);
        CHECK(a.useCount() == 2);
        CHECK(shared.useCount() == 2);
    }
    CHECK(Tracked::alive == before);
}

void testReset() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> ptr(new Tracked(11));
        CHECK(Tracked::alive == before + 1);

        ptr.reset();
        CHECK(!ptr);
        CHECK(ptr.get() == nullptr);
        CHECK(ptr.useCount() == 0);
        CHECK(Tracked::alive == before);  // reset() released the object

        ptr.reset(new Tracked(12));
        CHECK(static_cast<bool>(ptr));
        CHECK(ptr->value == 12);
        CHECK(ptr.useCount() == 1);
        CHECK(Tracked::alive == before + 1);

        // reset(get()) must not delete the object it is asked to keep owning.
        Tracked* address = ptr.get();
        ptr.reset(address);
        CHECK(ptr.get() == address);
        CHECK(ptr->value == 12);
        CHECK(ptr.useCount() == 1);
        CHECK(Tracked::alive == before + 1);

        // The same holds while somebody else shares the object.
        SharedPtr<Tracked> shared = ptr;
        CHECK(ptr.useCount() == 2);
        ptr.reset(ptr.get());
        CHECK(ptr.get() == address);
        CHECK(ptr->value == 12);
        CHECK(ptr.useCount() == 2);
        CHECK(shared.useCount() == 2);
        CHECK(Tracked::alive == before + 1);

        // The notes mention this state: a control block may exist while it manages nothing.
        ptr.reset(nullptr);
        CHECK(!ptr);
        CHECK(ptr.get() == nullptr);
    }
    CHECK(Tracked::alive == before);
}

void testMakeSharedBasic() {
    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> defaulted = makeSharedBasic<Tracked>();
        CHECK(defaulted.useCount() == 1);
        CHECK(defaulted->value == 0);

        SharedPtr<Pair> pair = makeSharedBasic<Pair>(4, 1.5);
        CHECK(pair->first == 4);
        CHECK(pair->second == 1.5);
        CHECK(pair.useCount() == 1);

        // Forwarding keeps working for an rvalue argument of a move-only type.
        MoveOnly source(9);
        SharedPtr<MoveOnly> moved = makeSharedBasic<MoveOnly>(std::move(source));
        CHECK(moved->value == 9);
        CHECK(moved.useCount() == 1);
    }
    CHECK(Tracked::alive == before);
}

void testAliasingConstructor() {
    const int before = BigObjectManager::destroyed;
    {
        SharedPtr<BigObjectManager> manager = makeSharedBasic<BigObjectManager>();
        CHECK(manager.useCount() == 1);

        SharedPtr<BigObject> object(manager, &manager->bigObject);
        CHECK(object.get() == &manager->bigObject);
        CHECK(object->payload == 42);
        CHECK(manager.useCount() == 2);  // aliasing shares the same control block
        CHECK(object.useCount() == 2);

        // Dropping the manager must not invalidate the aliasing pointer.
        manager.reset();
        CHECK(!manager);
        CHECK(object.useCount() == 1);
        CHECK(object->payload == 42);
        CHECK(BigObjectManager::destroyed == before);
    }
    CHECK(BigObjectManager::destroyed == before + 1);  // the last owner destroyed it
}

void testEmbeddedControlBlock() {
    ControlBlockEmbedded<Tracked> block(21);
    CHECK(block.refCount() == 1);
    CHECK(block.increment() == 2);
    CHECK(block.decrement() == 1);

    // The managed object lives inside the control block, so no separate allocation happened for it.
    const auto* address = static_cast<const char*>(block.managedAddress());
    const auto* base = reinterpret_cast<const char*>(&block);
    CHECK(block.managedAddress() != nullptr);
    CHECK(address >= base);
    CHECK(address < base + sizeof(block));
    CHECK(static_cast<Tracked*>(block.managedAddress())->value == 21);

    const int before = Tracked::alive;
    {
        SharedPtr<Tracked> ptr = makeShared<Tracked>(33);
        CHECK(ptr.useCount() == 1);
        CHECK(ptr->value == 33);
        CHECK(Tracked::alive == before + 1);

        SharedPtr<Tracked> copy = ptr;
        CHECK(ptr.useCount() == 2);
        CHECK(copy->value == 33);

        ptr.reset();
        CHECK(!ptr);
        CHECK(Tracked::alive == before + 1);  // still owned by copy
        CHECK(copy.useCount() == 1);

        SharedPtr<Pair> pair = makeShared<Pair>(6, 2.5);
        CHECK(pair->first == 6);
        CHECK(pair->second == 2.5);

        MoveOnly source(8);
        SharedPtr<MoveOnly> moved = makeShared<MoveOnly>(std::move(source));
        CHECK(moved->value == 8);
    }
    CHECK(Tracked::alive == before);  // embedded objects are destroyed with their control block
}

}  // namespace

int main() {
    testEmptySharedPtr();
    testConstructionFromRawPointer();
    testCopySharesOwnership();
    testCopyAssignmentReleasesPreviousObject();
    testMoveTransfersOwnership();
    testSwap();
    testReset();
    testMakeSharedBasic();
    testAliasingConstructor();
    testEmbeddedControlBlock();

    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
