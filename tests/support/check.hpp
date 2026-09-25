// Minimal zero-dependency test harness. Each test is a plain executable that
// returns the number of failures (0 == pass); CTest treats non-zero as failure.
#ifndef MPM_TEST_CHECK_HPP
#define MPM_TEST_CHECK_HPP

#include <cstdio>
#include <string>

namespace mpm::test {

struct Ctx {
    int failures = 0;
    const char* name = "";
};

inline Ctx& ctx() {
    static Ctx c;
    return c;
}

inline void begin(const char* name) { ctx().name = name; }

inline int finish() {
    if (ctx().failures == 0) {
        std::printf("[ PASS ] %s\n", ctx().name);
    } else {
        std::printf("[ FAIL ] %s (%d failure%s)\n", ctx().name, ctx().failures,
                    ctx().failures == 1 ? "" : "s");
    }
    return ctx().failures;
}

inline void fail(const char* file, int line, const std::string& msg) {
    ++ctx().failures;
    if (ctx().failures <= 20) { // avoid flooding on catastrophic failure
        std::printf("  [x] %s:%d: %s\n", file, line, msg.c_str());
    }
}

} // namespace mpm::test

#define MPM_CHECK(cond)                                                       \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ::mpm::test::fail(__FILE__, __LINE__, "CHECK failed: " #cond);    \
        }                                                                     \
    } while (0)

#define MPM_CHECK_EQ(a, b)                                                    \
    do {                                                                      \
        auto _va = (a);                                                       \
        auto _vb = (b);                                                       \
        if (!(_va == _vb)) {                                                  \
            ::mpm::test::fail(__FILE__, __LINE__,                             \
                              "CHECK_EQ failed: " #a " == " #b);              \
        }                                                                     \
    } while (0)

#endif // MPM_TEST_CHECK_HPP
