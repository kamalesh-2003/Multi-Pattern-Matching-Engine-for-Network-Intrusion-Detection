// Ties the framing/reassembly layer to the automaton. Feed it raw wire bytes,
// fragmented however the network delivered them; it reassembles each stream and
// reports pattern matches in the coordinates of the reassembled stream.
#ifndef MPM_SCANNER_HPP
#define MPM_SCANNER_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "mpm/aho_corasick.hpp"
#include "mpm/packet.hpp"

namespace mpm {

class StreamScanner {
public:
    explicit StreamScanner(const AhoCorasick& ac) : ac_(&ac) {}

    // Feed a slice of wire bytes. Complete frames are reassembled and scanned;
    // a trailing partial frame is buffered until the next call, so callers may
    // split the input at any boundary. `on_match(stream_id, Match)` fires per
    // occurrence.
    template <typename Fn>
    void feed(std::string_view bytes, Fn&& on_match) {
        carry_.append(bytes);
        const auto* base = reinterpret_cast<const std::uint8_t*>(carry_.data());
        std::size_t pos = 0;
        while (pos < carry_.size()) {
            ParseResult r = parse_frame(base + pos, carry_.size() - pos);
            if (r.status == ParseStatus::Incomplete) break;
            if (r.status != ParseStatus::Ok) {
                ++pos; // resync past a bad byte
                continue;
            }
            reasm_.accept(r.frame, appended_);
            if (!appended_.empty()) {
                AhoCorasick::State& st = states_[r.frame.stream_id];
                const std::uint32_t sid = r.frame.stream_id;
                ac_->scan(appended_, st,
                          [&](const Match& m) { on_match(sid, m); });
            }
            pos += r.consumed;
        }
        carry_.erase(0, pos);
    }

    std::size_t tracked_streams() const { return states_.size(); }

private:
    const AhoCorasick* ac_;
    Reassembler reasm_;
    std::unordered_map<std::uint32_t, AhoCorasick::State> states_;
    std::string carry_;    // bytes of an incomplete trailing frame
    std::string appended_; // scratch: newly contiguous bytes from reasm_
};

} // namespace mpm

#endif // MPM_SCANNER_HPP
