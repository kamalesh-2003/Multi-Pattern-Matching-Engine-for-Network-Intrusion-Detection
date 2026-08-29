// Minimal, self-contained packet framing + stream reassembly.
//
// This is attacker-controlled input, so every field is bounds-checked and the
// reassembler is bounded in memory. `fuzz/fuzz_packet.cpp` drives parse_stream()
// with arbitrary bytes; it must never read out of bounds, loop forever, or grow
// without limit.
//
// Wire format (little-endian), header is 14 bytes:
//   magic[2] = {'M','P'} | version u8 | flags u8 | stream_id u32 | seq u32 |
//   payload_len u16 | payload[payload_len]
// `seq` is a byte offset within the stream, so out-of-order frames can be
// reordered into a contiguous byte stream (the thing the matcher scans).
#ifndef MPM_PACKET_HPP
#define MPM_PACKET_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mpm {

inline constexpr std::size_t kFrameHeaderSize = 14;

struct Frame {
    std::uint8_t version = 0;
    std::uint8_t flags = 0;
    std::uint32_t stream_id = 0;
    std::uint32_t seq = 0;
    std::string_view payload; // view into the caller's buffer
};

enum class ParseStatus {
    Ok,         // a complete frame was parsed
    Incomplete, // need more bytes; `consumed` is 0
    BadMagic,   // resync by skipping one byte
    BadVersion,
};

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    Frame frame;
    std::size_t consumed = 0; // bytes to advance on Ok
};

// Parse a single frame at the front of [data, data+len). Never reads past len.
ParseResult parse_frame(const std::uint8_t* data, std::size_t len);

// Reassembles per-stream payloads into contiguous byte streams. Bounded so that
// hostile/garbage input cannot exhaust memory.
class Reassembler {
public:
    static constexpr std::size_t kMaxStreams = 4096;
    static constexpr std::size_t kMaxPendingFramesPerStream = 64;
    static constexpr std::size_t kMaxPendingBytesPerStream = 1u << 20;

    // Feed one frame. Any bytes that become newly contiguous are appended to
    // `out_appended` (cleared first).
    void accept(const Frame& f, std::string& out_appended);

private:
    struct Stream {
        std::uint32_t next_seq = 0;
        std::size_t pending_bytes = 0;
        std::map<std::uint32_t, std::string> pending; // seq -> payload
    };
    std::unordered_map<std::uint32_t, Stream> streams_;
};

// Convenience for fuzzing/testing: parse every frame in `data`, reassemble, and
// return the concatenated reassembled output across all streams. Malformed
// bytes trigger a one-byte resync. Guaranteed to terminate.
std::string parse_stream(const std::uint8_t* data, std::size_t len);

} // namespace mpm

#endif // MPM_PACKET_HPP
