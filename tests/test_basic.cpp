// Hand-written sanity cases for the Aho-Corasick engine and the naive oracle.
#include "mpm/aho_corasick.hpp"
#include "mpm/naive_matcher.hpp"
#include "support/check.hpp"

using namespace mpm;

static Matches sorted(Matches m) {
    canonicalize(m);
    return m;
}

int main() {
    test::begin("basic");

    // Classic Aho-Corasick example: patterns {he, she, his, hers} over "ushers".
    {
        std::vector<Pattern> pats = {
            {0, "he"}, {1, "she"}, {2, "his"}, {3, "hers"}};
        AhoCorasick ac(pats);
        NaiveMatcher nv(pats);
        const std::string text = "ushers";
        Matches a = sorted(ac.scan_all(text));
        Matches b = sorted(nv.scan_all(text));
        MPM_CHECK_EQ(a.size(), b.size());
        MPM_CHECK(a == b);
        // "she" at [1,4), "he" at [2,4), "hers" at [2,6).
        MPM_CHECK_EQ(a.size(), static_cast<std::size_t>(3));
    }

    // Overlapping repeats: pattern "aa" in "aaaa" -> matches at 0,1,2.
    {
        std::vector<Pattern> pats = {{7, "aa"}};
        AhoCorasick ac(pats);
        Matches a = sorted(ac.scan_all("aaaa"));
        MPM_CHECK_EQ(a.size(), static_cast<std::size_t>(3));
        MPM_CHECK_EQ(a[0].begin, static_cast<std::uint64_t>(0));
        MPM_CHECK_EQ(a[2].begin, static_cast<std::uint64_t>(2));
    }

    // No match, empty input, and empty-pattern rejection.
    {
        std::vector<Pattern> pats = {{1, "xyz"}, {2, ""}};
        AhoCorasick ac(pats);
        MPM_CHECK(ac.scan_all("abcabc").empty());
        MPM_CHECK(ac.scan_all("").empty());
        // Empty pattern (id 2) must never produce a match.
        Matches a = ac.scan_all("anything");
        for (const Match& m : a) MPM_CHECK(m.id != 2);
    }

    // Binary-safe: patterns and text containing NUL bytes.
    {
        std::string pat = std::string("a\0b", 3);
        std::string txt = std::string("zza\0b\0a\0b", 9);
        std::vector<Pattern> pats = {{5, pat}};
        AhoCorasick ac(pats);
        NaiveMatcher nv(pats);
        MPM_CHECK(sorted(ac.scan_all(txt)) == sorted(nv.scan_all(txt)));
    }

    return test::finish();
}
