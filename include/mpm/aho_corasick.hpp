// Aho-Corasick multi-pattern automaton.
//
// build() produces a fully deterministic goto table (every state/byte pair has a
// next state), so scan() is one table lookup per input byte. Failure outputs are
// propagated into each state, so all patterns ending at a position are reported
// without walking suffix links. scan() is streaming: it threads a caller-held
// State across calls, so any chunking of the input yields the same matches.
#ifndef MPM_AHO_CORASICK_HPP
#define MPM_AHO_CORASICK_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "mpm/match.hpp"

namespace mpm {

class AhoCorasick {
public:
    // Opaque cursor threaded across streaming scan() calls. `consumed` tracks
    // the absolute number of input bytes seen so far, so match offsets are
    // reported in the coordinates of the whole stream.
    struct State {
        std::int32_t node = 0;      // current automaton state (0 == root)
        std::uint64_t consumed = 0; // absolute bytes fed so far
    };

    AhoCorasick() = default;
    explicit AhoCorasick(const std::vector<Pattern>& patterns) { build(patterns); }

    // Construct the automaton. Empty patterns are ignored (they would match
    // everywhere and have no meaningful length).
    void build(const std::vector<Pattern>& patterns);

    std::size_t num_states() const { return static_cast<std::size_t>(num_states_); }
    std::size_t num_patterns() const { return pat_id_.size(); }

    // Feed one chunk. `emit(const Match&)` is called for every pattern
    // occurrence whose final byte lies in this chunk. State is advanced so the
    // next chunk continues seamlessly.
    template <typename Fn>
    void scan(std::string_view chunk, State& st, Fn&& emit) const {
        const std::int32_t* go = go_.data();
        const std::uint8_t* in = reinterpret_cast<const std::uint8_t*>(chunk.data());
        const std::size_t len = chunk.size();
        std::int32_t node = st.node;
        std::uint64_t pos = st.consumed; // absolute index of the byte about to be read

        for (std::size_t i = 0; i < len; ++i) {
            node = go[static_cast<std::size_t>(node) * 256 + in[i]];
            const std::uint64_t end = pos + i + 1; // one past the byte just consumed
            const std::uint32_t lo = out_off_[static_cast<std::size_t>(node)];
            const std::uint32_t hi = out_off_[static_cast<std::size_t>(node) + 1];
            for (std::uint32_t k = lo; k < hi; ++k) {
                const std::uint32_t pi = out_data_[k];
                emit(Match{pat_id_[pi], end - pat_len_[pi], end});
            }
        }
        st.node = node;
        st.consumed = pos + len;
    }

    // Convenience: scan a whole buffer in one shot.
    Matches scan_all(std::string_view input) const {
        Matches out;
        State st;
        scan(input, st, [&](const Match& m) { out.push_back(m); });
        return out;
    }

private:
    // Flattened deterministic goto table: next = go_[state * 256 + byte].
    std::vector<std::int32_t> go_;
    // Per-state output slice into out_data_: [out_off_[s], out_off_[s+1]).
    std::vector<std::uint32_t> out_off_;
    std::vector<std::uint32_t> out_data_; // pattern indices
    // Per-pattern metadata, indexed by the pattern index stored in out_data_.
    std::vector<std::uint32_t> pat_id_;
    std::vector<std::uint32_t> pat_len_;
    std::int32_t num_states_ = 0;
};

} // namespace mpm

#endif // MPM_AHO_CORASICK_HPP
