#ifndef SHARED_PTR_HEADER
#define SHARED_PTR_HEADER

#include <cassert>
#include <utility>

template <typename T>
class SharedPtr;

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args);

class ControlBlockBase {
public:
    ControlBlockBase() = default;

    // dtor is virtual, so that we can call derived class's dtor from a ptr to this base class.
    virtual ~ControlBlockBase() = default;

    // pure virtual function; must be overriden by derived classes
    virtual void* managedAddress() = 0;

    // Delete copies, which also implicitly deletes moves.
    ControlBlockBase(const ControlBlockBase&) = delete;
    ControlBlockBase& operator=(const ControlBlockBase&) = delete;

    long increment() {
        // A live control block is always owned by at least one SharedPtr.
        assert(mRefCount > 0);
        return ++mRefCount;
    }

    long decrement() {
        // Cannot release an owner that does not exist.
        assert(mRefCount > 0);
        return --mRefCount;
    }

    long refCount() const {
        return mRefCount;
    }

private:
    // Created by the single SharedPtr which first takes ownership of the resource.
    long mRefCount = 1;
};

// Control block for a resource allocated elsewhere: it just keeps the raw pointer to it.
template <typename T>
class ControlBlock : public ControlBlockBase {
public:
    ControlBlock(T* ptr) : mPtr(ptr) {}

    ~ControlBlock() override {
        delete mPtr;
    }

    void* managedAddress() override {
        return mPtr;
    }

private:
    T* mPtr;
};

template <typename T>
class SharedPtr {
public:
    SharedPtr() = default;

    explicit SharedPtr(T* ptr) : mStored(ptr), mBlock(new ControlBlock<T>(ptr)) {}

    ~SharedPtr() {
        if (mBlock != nullptr && mBlock->decrement() == 0) {
            delete mBlock;
        }
    }

    SharedPtr(const SharedPtr& other) : mStored(other.mStored), mBlock(other.mBlock) {
        if (mBlock != nullptr) {
            mBlock->increment();
        }
    }

    // Steals both pointers; the refcount is unchanged because ownership did not change hands.
    SharedPtr(SharedPtr&& other) noexcept : mStored(other.mStored), mBlock(other.mBlock) {
        other.mStored = nullptr;
        other.mBlock = nullptr;
    }

    // Copy-and-swap: the temporary releases whatever we used to own when it is destroyed.
    SharedPtr& operator=(const SharedPtr& other) {
        SharedPtr copy(other);
        swap(copy);
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        SharedPtr moved(std::move(other));
        swap(moved);
        return *this;
    }

    // Aliasing constructor: share other's ownership, but represent storedPtr instead.
    template <typename U>
    SharedPtr(const SharedPtr<U>& other, T* storedPtr) noexcept
        : mStored(storedPtr), mBlock(other.mBlock) {
        // There is no control block to share if other owns nothing.
        assert(mBlock != nullptr);
        mBlock->increment();
    }

    T& operator*() const {
        assert(mStored != nullptr);
        return *mStored;
    }

    T* operator->() const {
        assert(mStored != nullptr);
        return mStored;
    }

    T* get() const noexcept {
        return mStored;
    }

    bool operator==(const SharedPtr& other) const noexcept {
        return mStored == other.mStored;
    }

    explicit operator bool() const noexcept {
        return mStored != nullptr;
    }

    void swap(SharedPtr& other) noexcept {
        std::swap(mStored, other.mStored);
        std::swap(mBlock, other.mBlock);
    }

    void reset() noexcept {
        SharedPtr empty;
        swap(empty);
    }

    void reset(T* other) {
        // Resetting to the pointer we already store must be a no-op. Otherwise the old control block
        // becomes the last owner and deletes the very object we are being asked to keep owning.
        if (other == mStored) {
            return;
        }

        SharedPtr replacement(other);
        swap(replacement);
    }

    long useCount() const noexcept {
        return mBlock != nullptr ? mBlock->refCount() : 0;
    }

private:
    // For makeShared(): an embedded control block already owns the object it was built around, so the
    // refcount must not be incremented here.
    explicit SharedPtr(T* storedPtr, ControlBlockBase* block) noexcept
        : mStored(storedPtr), mBlock(block) {}

    // The aliasing constructor needs to read another instantiation's control block.
    template <typename U>
    friend class SharedPtr;

    template <typename U, typename... Args>
    friend SharedPtr<U> makeShared(Args&&... args);

    T* mStored = nullptr;
    ControlBlockBase* mBlock = nullptr;
};

// Bonus: control block which stores the resource inside itself, so only one allocation is needed.
template <typename T>
class ControlBlockEmbedded : public ControlBlockBase {
public:
    template <typename... Args>
    ControlBlockEmbedded(Args&&... args) : mValue(std::forward<Args>(args)...) {}

    void* managedAddress() override {
        return &mValue;
    }

private:
    T mValue;
};

template <typename T, typename... Args>
SharedPtr<T> makeSharedBasic(Args&&... args) {
    return SharedPtr<T>(new T(std::forward<Args>(args)...));
}

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    auto* block = new ControlBlockEmbedded<T>(std::forward<Args>(args)...);
    return SharedPtr<T>(static_cast<T*>(block->managedAddress()), block);
}

#endif
