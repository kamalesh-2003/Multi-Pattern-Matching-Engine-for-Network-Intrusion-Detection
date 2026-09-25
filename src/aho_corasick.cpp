#include "mpm/aho_corasick.hpp"

#include <array>
#include <cstddef>
#include <queue>

namespace mpm {

namespace {
constexpr std::int32_t kNoEdge = -1;
constexpr std::size_t kAlpha = 256;
} // namespace

void AhoCorasick::build(const std::vector<Pattern>& patterns) {
    go_.clear();
    out_off_.clear();
    out_data_.clear();
    pat_id_.clear();
    pat_len_.clear();

    // --- 1. Build the trie (sparse edges marked kNoEdge) ------------------
    // Root is state 0. Each state owns a 256-wide row in `go_`.
    auto add_state = [&]() -> std::int32_t {
        go_.insert(go_.end(), kAlpha, kNoEdge);
        return static_cast<std::int32_t>(go_.size() / kAlpha) - 1;
    };
    add_state(); // root

    // Per-state list of pattern indices that *terminate* exactly at that state.
    std::vector<std::vector<std::uint32_t>> terminal(1);

    for (const Pattern& p : patterns) {
        if (p.bytes.empty()) continue; // empty patterns are meaningless here
        std::int32_t node = 0;
        for (unsigned char c : p.bytes) {
            // Re-index (never hold a reference) because add_state() may reallocate go_.
            const std::size_t idx = static_cast<std::size_t>(node) * kAlpha + c;
            if (go_[idx] == kNoEdge) {
                const std::int32_t created = add_state();
                go_[idx] = created;
                terminal.emplace_back();
            }
            node = go_[idx];
        }
        const auto pi = static_cast<std::uint32_t>(pat_id_.size());
        pat_id_.push_back(p.id);
        pat_len_.push_back(static_cast<std::uint32_t>(p.bytes.size()));
        terminal[static_cast<std::size_t>(node)].push_back(pi);
    }

    num_states_ = static_cast<std::int32_t>(go_.size() / kAlpha);

    // --- 2. BFS to compute failure links and make goto deterministic ------
    // Output propagation: a state inherits the outputs of its failure state, so
    // every pattern that is a suffix of the current path is reported without
    // walking suffix links at scan time.
    std::vector<std::int32_t> fail(static_cast<std::size_t>(num_states_), 0);
    std::queue<std::int32_t> bfs;

    // Depth-1 states: fail link is the root; root's missing edges self-loop.
    for (std::size_t c = 0; c < kAlpha; ++c) {
        std::int32_t& e = go_[c]; // root row
        if (e == kNoEdge) {
            e = 0; // root self-loops on unmatched bytes
        } else {
            fail[static_cast<std::size_t>(e)] = 0;
            bfs.push(e);
        }
    }

    while (!bfs.empty()) {
        const std::int32_t u = bfs.front();
        bfs.pop();
        // Propagate failure outputs into this state (fail already finalised
        // because BFS visits shallower states first).
        const std::int32_t fu = fail[static_cast<std::size_t>(u)];
        auto& tu = terminal[static_cast<std::size_t>(u)];
        const auto& tfu = terminal[static_cast<std::size_t>(fu)];
        tu.insert(tu.end(), tfu.begin(), tfu.end());

        for (std::size_t c = 0; c < kAlpha; ++c) {
            std::int32_t& e = go_[static_cast<std::size_t>(u) * kAlpha + c];
            if (e == kNoEdge) {
                // Path-compress: follow the failure link's (already
                // deterministic) transition.
                e = go_[static_cast<std::size_t>(fu) * kAlpha + c];
            } else {
                fail[static_cast<std::size_t>(e)] =
                    go_[static_cast<std::size_t>(fu) * kAlpha + c];
                bfs.push(e);
            }
        }
    }

    // --- 3. Flatten per-state outputs into CSR arrays ---------------------
    out_off_.resize(static_cast<std::size_t>(num_states_) + 1, 0);
    for (std::int32_t s = 0; s < num_states_; ++s) {
        out_off_[static_cast<std::size_t>(s) + 1] =
            out_off_[static_cast<std::size_t>(s)] +
            static_cast<std::uint32_t>(terminal[static_cast<std::size_t>(s)].size());
    }
    out_data_.resize(out_off_.back());
    for (std::int32_t s = 0; s < num_states_; ++s) {
        std::uint32_t at = out_off_[static_cast<std::size_t>(s)];
        for (std::uint32_t pi : terminal[static_cast<std::size_t>(s)]) {
            out_data_[at++] = pi;
        }
    }
}

} // namespace mpm
