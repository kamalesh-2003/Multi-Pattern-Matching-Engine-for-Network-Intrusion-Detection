// Benchmark harness. Emits a JSON report to stdout and, if given a path
// argument, to that file. Every number here is measured at run time -- nothing
// is hard-coded. Usage:
//
//   bench [output.json] [--mb N] [--patterns K] [--passes P]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mpm/aho_corasick.hpp"
#include "mpm/gen.hpp"
#include "mpm/naive_matcher.hpp"
#include "mpm/prefilter.hpp"

using namespace mpm;
using Clock = std::chrono::steady_clock;

static double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Minimal JSON string escaper for the few strings we emit.
static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else o.push_back(c);
    }
    o.push_back('"');
    return o;
}

int main(int argc, char** argv) {
    const char* out_path = nullptr;
    std::size_t mb = 64;
    unsigned num_patterns = 2000;
    unsigned passes = 3;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mb") == 0 && i + 1 < argc) mb = std::strtoull(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--patterns") == 0 && i + 1 < argc) num_patterns = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        else if (std::strcmp(argv[i], "--passes") == 0 && i + 1 < argc) passes = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        else if (argv[i][0] != '-') out_path = argv[i];
    }

    const std::size_t input_bytes = mb * 1024 * 1024;

    // ---- Build workload -------------------------------------------------
    gen::Rng rng(0xBEEF2024ULL);
    // Signature-ish patterns: 4..24 bytes over the full byte range.
    std::vector<Pattern> patterns =
        gen::random_patterns(rng, num_patterns, 4, 24, 256);
    // Haystack: bytes over a moderate alphabet.
    std::string haystack = gen::random_bytes(rng, input_bytes, 200);
    // Plant every pattern at a random offset so the scan produces a realistic,
    // non-zero number of matches (random patterns would otherwise never occur).
    if (input_bytes > 32) {
        for (const Pattern& p : patterns) {
            if (p.bytes.size() >= input_bytes) continue;
            const std::size_t off = rng.below(
                static_cast<std::uint32_t>(input_bytes - p.bytes.size()));
            std::memcpy(&haystack[off], p.bytes.data(), p.bytes.size());
        }
    }
    const auto* hay = reinterpret_cast<const std::uint8_t*>(haystack.data());

    // ---- Build the automaton -------------------------------------------
    auto tb = Clock::now();
    AhoCorasick ac(patterns);
    const double build_s = seconds_since(tb);

    // ---- Aho-Corasick scan throughput ----------------------------------
    std::uint64_t match_count = 0;
    double best_scan_mbps = 0.0;
    for (unsigned p = 0; p < passes; ++p) {
        std::uint64_t local = 0;
        auto t0 = Clock::now();
        AhoCorasick::State st;
        ac.scan(haystack, st, [&](const Match&) { ++local; });
        const double s = seconds_since(t0);
        match_count = local;
        const double mbps = (static_cast<double>(input_bytes) / (1024.0 * 1024.0)) / s;
        if (mbps > best_scan_mbps) best_scan_mbps = mbps;
    }

    // ---- Prefilter throughput (scalar vs dispatched tier) --------------
    // Measure raw scan speed: an EMPTY set matches nothing, so the prefilter is
    // forced to traverse the entire buffer and return `len`. This is the honest
    // worst case (no early exit) and isolates the byte-scanning loop.
    ByteSet empty_set; // all-zero membership
    std::size_t sink = 0;
    double best_scalar_mbps = 0.0, best_simd_mbps = 0.0;
    for (unsigned p = 0; p < passes; ++p) {
        auto t0 = Clock::now();
        sink += prefilter_find_first_scalar(empty_set, hay, input_bytes);
        double s = seconds_since(t0);
        double mbps = (static_cast<double>(input_bytes) / (1024.0 * 1024.0)) / s;
        if (mbps > best_scalar_mbps) best_scalar_mbps = mbps;

        t0 = Clock::now();
        sink += prefilter_find_first(empty_set, hay, input_bytes);
        s = seconds_since(t0);
        mbps = (static_cast<double>(input_bytes) / (1024.0 * 1024.0)) / s;
        if (mbps > best_simd_mbps) best_simd_mbps = mbps;
    }

    // ---- Naive reference throughput on a small slice -------------------
    // Naive is O(n * sum(len)); measure on a small window to keep it bounded.
    const std::size_t naive_bytes = 256 * 1024;
    NaiveMatcher naive(patterns);
    auto tn = Clock::now();
    std::uint64_t naive_matches = 0;
    naive.scan(std::string_view(haystack).substr(0, naive_bytes),
               [&](const Match&) { ++naive_matches; });
    const double naive_s = seconds_since(tn);
    const double naive_mbps =
        (static_cast<double>(naive_bytes) / (1024.0 * 1024.0)) / naive_s;

#if defined(__clang__)
    std::string compiler = std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    std::string compiler = "gcc " + std::to_string(__GNUC__) + "." +
                           std::to_string(__GNUC_MINOR__) + "." +
                           std::to_string(__GNUC_PATCHLEVEL__);
#else
    std::string compiler = "unknown";
#endif

    // ---- Emit JSON ------------------------------------------------------
    std::string j;
    j += "{\n";
    j += "  \"compiler\": " + jstr(compiler) + ",\n";
    j += "  \"simd_tier\": " + jstr(to_string(prefilter_active_tier())) + ",\n";
    j += "  \"cpu\": { \"sse42\": " + std::string(cpu_has_sse42() ? "true" : "false") +
         ", \"avx2\": " + std::string(cpu_has_avx2() ? "true" : "false") + " },\n";
    j += "  \"workload\": { \"input_mib\": " + std::to_string(mb) +
         ", \"patterns\": " + std::to_string(patterns.size()) +
         ", \"automaton_states\": " + std::to_string(ac.num_states()) +
         ", \"passes\": " + std::to_string(passes) + " },\n";
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%.4f", build_s);
    j += "  \"build_seconds\": " + std::string(buf) + ",\n";
    std::snprintf(buf, sizeof(buf), "%.1f", best_scan_mbps);
    j += "  \"aho_corasick_scan_mib_s\": " + std::string(buf) + ",\n";
    j += "  \"aho_corasick_matches\": " + std::to_string(match_count) + ",\n";
    std::snprintf(buf, sizeof(buf), "%.1f", best_scalar_mbps);
    j += "  \"prefilter_scalar_mib_s\": " + std::string(buf) + ",\n";
    std::snprintf(buf, sizeof(buf), "%.1f", best_simd_mbps);
    j += "  \"prefilter_simd_mib_s\": " + std::string(buf) + ",\n";
    std::snprintf(buf, sizeof(buf), "%.1f", naive_mbps);
    j += "  \"naive_scan_mib_s\": " + std::string(buf) + ",\n";
    j += "  \"naive_matches\": " + std::to_string(naive_matches) + ",\n";
    j += "  \"_sink\": " + std::to_string(sink) + "\n";
    j += "}\n";

    std::fputs(j.c_str(), stdout);
    if (out_path) {
        if (FILE* f = std::fopen(out_path, "wb")) {
            std::fputs(j.c_str(), f);
            std::fclose(f);
        }
    }
    return 0;
}
