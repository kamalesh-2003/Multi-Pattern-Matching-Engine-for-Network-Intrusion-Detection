// Single-producer / single-consumer lock-free ring buffer.
//
// Exactly one thread calls push(); exactly one (other) thread calls pop().
// Capacity is rounded up to a power of two so index wrap is a mask.
//
// Memory-ordering rationale (mirrored in docs/memory-model.md):
//
//   head_  : write index owned by the CONSUMER (advanced in pop()).
//   tail_  : write index owned by the PRODUCER (advanced in push()).
//
// Producer publishes a slot with tail_.store(release); consumer observes it
// with tail_.load(acquire). That release/acquire pair is what makes the slot's
// data write "happen-before" the consumer's read of it. Symmetrically, the
// consumer frees a slot with head_.store(release) and the producer observes
// free space with head_.load(acquire), so the producer never overwrites a slot
// whose element the consumer is still reading.
#ifndef MPM_SPSC_RING_HPP
#define MPM_SPSC_RING_HPP

#include <atomic>
#include <cstddef>
#include <new>
#include <vector>

namespace mpm {

// Fixed 64-byte cache line for x86-64. We intentionally do NOT use
// std::hardware_destructive_interference_size: its value can differ across
// compiler/-mtune settings (an ABI hazard GCC warns about), and this header is
// shared between translation units built with different flags.
inline constexpr std::size_t kCacheLine = 64;

template <typename T>
class SpscRing {
public:
    // `min_capacity` usable slots (rounded up to a power of two). One slot is
    // reserved to distinguish "full" from "empty", so the buffer allocates the
    // next power of two strictly greater than min_capacity.
    explicit SpscRing(std::size_t min_capacity) {
        std::size_t cap = 2;
        while (cap <= min_capacity) cap <<= 1;
        capacity_ = cap;
        mask_ = cap - 1;
        buffer_.resize(cap);
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    std::size_t capacity() const { return capacity_ - 1; }

    // Producer side. Returns false if the ring is full.
    bool push(const T& value) {
        // relaxed: the producer is the sole writer of tail_, so its own last
        // store is already visible to it; no synchronisation needed to re-read.
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t next = t + 1;
        // acquire: observe the consumer's latest head_ release so we know how
        // much space has actually been freed. Without acquire we could read a
        // stale head_ and wrongly believe the ring is full (liveness), or --
        // more dangerously, paired with the store below -- reorder the slot
        // write before seeing the free (safety on weak hardware).
        if (next - head_.load(std::memory_order_acquire) > capacity_ - 1) {
            return false; // full
        }
        buffer_[t & mask_] = value;
        // release: publish the slot write. A consumer that acquire-loads this
        // new tail_ is guaranteed to see buffer_[t] fully written. Without
        // release the store to buffer_ could sink past the tail_ update and the
        // consumer would read an uninitialised / half-written element.
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false if the ring is empty.
    bool pop(T& out) {
        // relaxed: the consumer is the sole writer of head_; re-reading its own
        // value needs no synchronisation.
        const std::size_t h = head_.load(std::memory_order_relaxed);
        // acquire: pair with the producer's tail_ release so the element write
        // is visible before we read it below.
        if (h == tail_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer_[h & mask_];
        // release: signal the freed slot to the producer. Its acquire-load of
        // head_ then guarantees our read of buffer_[h] completed before it may
        // reuse the slot. Without release the producer could overwrite the slot
        // while this read is still in flight.
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::vector<T> buffer_;

    // Keep the two hot indices on separate cache lines to avoid false sharing
    // between the producer and consumer threads.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
};

} // namespace mpm

#endif // MPM_SPSC_RING_HPP
