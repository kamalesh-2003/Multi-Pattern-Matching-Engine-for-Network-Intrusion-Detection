// libFuzzer entry point for the attacker-controlled packet parser + reassembler.
// Build with Clang and -fsanitize=fuzzer,address (see CMake option MPM_FUZZER).
//
//   ./fuzz_packet -runs=1000000        # or let it run continuously
//
// The parser must never read out of bounds, hang, or grow memory without bound
// on any input; ASan + the reassembler's internal caps enforce that.
#include <cstddef>
#include <cstdint>

#include "mpm/packet.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string out = mpm::parse_stream(data, size);
    // Touch the result so the call cannot be optimised away.
    return out.empty() ? 0 : (out.back() == '\0' ? 0 : 0);
}
