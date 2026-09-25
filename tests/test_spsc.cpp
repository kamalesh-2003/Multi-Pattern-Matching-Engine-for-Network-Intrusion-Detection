// Lock-free SPSC ring: a producer thread pushes a long known sequence and a
// consumer thread pops it. We verify (a) every value arrives, (b) in order, and
// (c) nothing is duplicated or dropped -- across millions of items through a
// small ring, which forces the full/empty backpressure paths constantly.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "mpm/spsc_ring.hpp"
#include "support/check.hpp"

using namespace mpm;

int main(int argc, char** argv) {
    test::begin("spsc");

    std::uint64_t count = 5'000'000;
    if (argc > 1) count = std::strtoull(argv[1], nullptr, 10);

    SpscRing<std::uint64_t> ring(1024); // small on purpose: hammer full/empty

    std::atomic<bool> order_ok{true};
    std::atomic<std::uint64_t> received{0};
    std::atomic<std::uint64_t> checksum{0};

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        std::uint64_t sum = 0;
        std::uint64_t got = 0;
        while (got < count) {
            std::uint64_t v;
            if (ring.pop(v)) {
                if (v != expected) order_ok.store(false, std::memory_order_relaxed);
                ++expected;
                sum += v;
                ++got;
            }
        }
        received.store(got, std::memory_order_relaxed);
        checksum.store(sum, std::memory_order_relaxed);
    });

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count;) {
            if (ring.push(i)) {
                ++i;
            }
        }
    });

    producer.join();
    consumer.join();

    // Expected checksum of 0..count-1.
    const std::uint64_t expected_sum = (count == 0) ? 0 : (count - 1) * count / 2;

    MPM_CHECK(order_ok.load());
    MPM_CHECK_EQ(received.load(), count);
    MPM_CHECK_EQ(checksum.load(), expected_sum);

    std::printf("  spsc: moved %llu items in order\n",
                static_cast<unsigned long long>(count));
    return test::finish();
}
