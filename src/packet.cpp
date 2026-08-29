#include "mpm/packet.hpp"

#include <algorithm>

namespace mpm {

namespace {
std::uint16_t load_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t load_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
} // namespace

ParseResult parse_frame(const std::uint8_t* data, std::size_t len) {
    ParseResult r;
    if (len < kFrameHeaderSize) {
        r.status = ParseStatus::Incomplete;
        return r;
    }
    if (data[0] != 'M' || data[1] != 'P') {
        r.status = ParseStatus::BadMagic;
        return r;
    }
    const std::uint8_t version = data[2];
    if (version != 1) {
        r.status = ParseStatus::BadVersion;
        return r;
    }
    const std::uint16_t payload_len = load_u16(data + 12);
    // Guard against overflow before comparing against len.
    const std::size_t total = kFrameHeaderSize + static_cast<std::size_t>(payload_len);
    if (len < total) {
        r.status = ParseStatus::Incomplete;
        return r;
    }
    r.frame.version = version;
    r.frame.flags = data[3];
    r.frame.stream_id = load_u32(data + 4);
    r.frame.seq = load_u32(data + 8);
    r.frame.payload = std::string_view(reinterpret_cast<const char*>(data + kFrameHeaderSize),
                                       payload_len);
    r.status = ParseStatus::Ok;
    r.consumed = total;
    return r;
}

void Reassembler::accept(const Frame& f, std::string& out_appended) {
    out_appended.clear();

    // Cap the number of distinct streams so a flood of stream_ids can't grow the
    // map without bound. Unknown streams beyond the cap are dropped.
    auto it = streams_.find(f.stream_id);
    if (it == streams_.end()) {
        if (streams_.size() >= kMaxStreams) return;
        it = streams_.emplace(f.stream_id, Stream{}).first;
    }
    Stream& s = it->second;

    auto append_from = [&](std::uint32_t seq, std::string_view payload) {
        // Contribute only the part at or after next_seq (handles retransmit /
        // overlap). `seq` is the absolute offset of payload[0].
        if (seq > s.next_seq) return false; // gap: not contiguous yet
        const std::uint32_t skip = s.next_seq - seq;
        if (skip >= payload.size()) return false; // entirely old data
        const std::string_view fresh = payload.substr(skip);
        out_appended.append(fresh);
        s.next_seq += static_cast<std::uint32_t>(fresh.size());
        return true;
    };

    if (!append_from(f.seq, f.payload)) {
        // Future data: buffer it, subject to bounds.
        if (f.seq > s.next_seq) {
            if (s.pending.size() < kMaxPendingFramesPerStream &&
                s.pending_bytes + f.payload.size() <= kMaxPendingBytesPerStream) {
                auto [pit, inserted] = s.pending.try_emplace(f.seq, std::string(f.payload));
                if (inserted) {
                    s.pending_bytes += pit->second.size();
                }
            }
        }
        return;
    }

    // We advanced next_seq; drain any buffered frames that are now contiguous.
    for (auto pit = s.pending.begin(); pit != s.pending.end();) {
        const std::uint32_t seq = pit->first;
        if (seq > s.next_seq) break; // still a gap
        const std::string& payload = pit->second;
        if (seq <= s.next_seq && (s.next_seq - seq) < payload.size()) {
            const std::uint32_t skip = s.next_seq - seq;
            const std::string_view fresh = std::string_view(payload).substr(skip);
            out_appended.append(fresh);
            s.next_seq += static_cast<std::uint32_t>(fresh.size());
        }
        s.pending_bytes -= payload.size();
        pit = s.pending.erase(pit);
    }
}

std::string parse_stream(const std::uint8_t* data, std::size_t len) {
    Reassembler reasm;
    std::string result;
    std::string appended;
    std::size_t pos = 0;
    while (pos < len) {
        ParseResult r = parse_frame(data + pos, len - pos);
        switch (r.status) {
            case ParseStatus::Ok:
                reasm.accept(r.frame, appended);
                result.append(appended);
                pos += r.consumed;
                break;
            case ParseStatus::BadMagic:
            case ParseStatus::BadVersion:
                ++pos; // resync one byte and retry
                break;
            case ParseStatus::Incomplete:
                return result; // need more bytes than we have
        }
    }
    return result;
}

} // namespace mpm
