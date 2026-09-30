#include "gc.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

thread_local GarbageCollector *GarbageCollector::active = nullptr;

GarbageCollector::GarbageCollector() : previous(active) {
    stress      = std::getenv("SCSA_GC_STRESS") != nullptr;
    reportStats = std::getenv("SCSA_GC_STATS") != nullptr;
    if (const char *setting = std::getenv("SCSA_GC_THRESHOLD")) {
        minimumThreshold = threshold = std::max<size_t>(1, std::stoull(setting));
    }
    active = this;
}

GarbageCollector::~GarbageCollector() {
    if (reportStats) {
        std::cerr << "GC: allocations=" << allocations << " collections=" << collections
                  << " reclaimed=" << reclaimed << " live_objects=" << liveObjects
                  << " peak_objects=" << peakObjects << " last_live_bytes=" << liveBytes << '\n';
    }
    while (objects) {
        HeapObject *next = objects->next;
        delete objects;
        objects = next;
    }
    active = previous;
}

GarbageCollector &GarbageCollector::current() {
    if (!active)
        throw std::logic_error("Heap allocation requires an active interpreter");
    return *active;
}

void GarbageCollector::mark(HeapObject *object) {
    if (!object || object->marked)
        return;
    object->marked = true;
    gray.push_back(object);
}

void GarbageCollector::collect() {
    ++collections;
    if (traceRoots)
        traceRoots(*this);
    for (GCRoot *root = roots; root; root = root->previous)
        root->trace(*this);
    while (!gray.empty()) {
        HeapObject *object = gray.back();
        gray.pop_back();
        object->trace(*this);
    }
    liveBytes         = 0;
    HeapObject **link = &objects;
    while (*link) {
        HeapObject *object = *link;
        if (object->marked) {
            object->marked = false;
            liveBytes += object->retainedBytes();
            link = &object->next;
        } else {
            *link = object->next;
            delete object;
            --liveObjects;
            ++reclaimed;
        }
    }
    bytesSinceCollection = 0;
    // Grow the allocation allowance with the live graph; avoid rescanning it too often.
    threshold = std::max(minimumThreshold, liveBytes);
}
