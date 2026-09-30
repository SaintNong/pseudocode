#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

class GarbageCollector;

// Non-moving objects: values and object edges contain ordinary, non-owning pointers.
struct HeapObject {
    HeapObject *next      = nullptr;
    bool marked           = false;
    size_t allocationSize = 0;
    virtual ~HeapObject() = default;
    virtual void trace(GarbageCollector &) {
    }
    virtual size_t retainedBytes() const {
        return allocationSize;
    }
    void accountGrowth(size_t previousBytes);
};

class GCRoot;

class GarbageCollector {
    static thread_local GarbageCollector *active;
    GarbageCollector *previous;
    HeapObject *objects = nullptr;
    std::vector<HeapObject *> gray;
    GCRoot *roots               = nullptr;
    size_t bytesSinceCollection = 0;
    size_t threshold            = 1024 * 1024;
    size_t minimumThreshold     = 1024 * 1024;
    bool stress                 = false;
    bool reportStats            = false;
    friend class GCRoot;

public:
    std::function<void(GarbageCollector &)> traceRoots;
    size_t allocations = 0;
    size_t collections = 0;
    size_t reclaimed   = 0;
    size_t liveObjects = 0;
    size_t peakObjects = 0;
    size_t liveBytes   = 0;

    GarbageCollector();
    ~GarbageCollector();
    GarbageCollector(const GarbageCollector &)            = delete;
    GarbageCollector &operator=(const GarbageCollector &) = delete;
    static GarbageCollector &current();

    // Allocation never collects: the caller may still have unrooted C++ temporaries.
    template <typename T, typename... Args> T *make(Args &&...args) {
        T *object              = new T(std::forward<Args>(args)...);
        object->allocationSize = sizeof(T);
        object->next           = objects;
        objects                = object;
        bytesSinceCollection += object->retainedBytes();
        ++allocations;
        ++liveObjects;
        if (liveObjects > peakObjects)
            peakObjects = liveObjects;
        return object;
    }

    void mark(HeapObject *object);
    // Buffer growth requests collection at the next safepoint, never immediately.
    void accountAllocation(size_t bytes) {
        bytesSinceCollection += bytes;
    }
    void collect();
    void safepoint() {
        if (stress || bytesSinceCollection >= threshold)
            collect();
    }
};

inline void HeapObject::accountGrowth(size_t previousBytes) {
    const size_t currentBytes = retainedBytes();
    if (currentBytes > previousBytes)
        GarbageCollector::current().accountAllocation(currentBytes - previousBytes);
}

template <typename T, typename... Args> T *gcNew(Args &&...args) {
    return GarbageCollector::current().make<T>(std::forward<Args>(args)...);
}

// An explicit C++ root, including temporary vectors held across nested VM execution.
class GCRoot {
    GarbageCollector &gc;
    GCRoot *previous;
    std::function<void(GarbageCollector &)> trace;
    friend class GarbageCollector;

public:
    explicit GCRoot(std::function<void(GarbageCollector &)> callback)
        : gc(GarbageCollector::current()), previous(gc.roots), trace(std::move(callback)) {
        gc.roots = this;
    }
    ~GCRoot() {
        gc.roots = previous;
    }
    GCRoot(const GCRoot &)            = delete;
    GCRoot &operator=(const GCRoot &) = delete;
};
