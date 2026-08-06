// SPDX-License-Identifier: Apache-2.0
//
// A 100-line test harness, for the same reason the project has its own JSON
// writer: RuntimeSkeptic must build and test with nothing but a compiler and
// CMake, on three platforms, with no network access.
#ifndef RUNTIMESKEPTIC_TESTS_TEST_SUPPORT_HPP
#define RUNTIMESKEPTIC_TESTS_TEST_SUPPORT_HPP

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace rs::test {

struct Case {
    std::string name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline std::string& current_case() {
    static std::string name;
    return name;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> body) {
        registry().push_back(Case{std::move(name), std::move(body)});
    }
};

inline void report_failure(const char* file, int line, const std::string& message) {
    ++failure_count();
    std::cerr << "  FAIL " << current_case() << "\n"
              << "    " << file << ":" << line << "\n"
              << "    " << message << "\n";
}

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* expr_a, const char* expr_b,
              const char* file, int line) {
    if (a == b) return;
    std::ostringstream out;
    out << expr_a << " != " << expr_b << "\n"
        << "      left : " << a << "\n"
        << "      right: " << b;
    report_failure(file, line, out.str());
}

inline int run_all(const char* suite_name) {
    std::cout << "== " << suite_name << " (" << registry().size() << " cases)\n";
    for (const auto& c : registry()) {
        current_case() = c.name;
        const int before = failure_count();
        c.body();
        if (failure_count() == before) std::cout << "  ok   " << c.name << "\n";
    }
    if (failure_count() > 0) {
        std::cerr << "\n" << failure_count() << " failure(s) in " << suite_name
                  << "\n";
        return 1;
    }
    std::cout << "all " << registry().size() << " cases passed\n";
    return 0;
}

}  // namespace rs::test

#define RS_TEST(name)                                                        \
    static void rs_test_##name();                                            \
    static const ::rs::test::Registrar rs_registrar_##name(#name,            \
                                                           rs_test_##name);  \
    static void rs_test_##name()

// MSVC's C4127 (conditional expression is constant) fires on the many test
// assertions that compare compile-time constants - a deliberate, correct thing
// for a test to do. It is a documented false positive on older MSVC: VS2022 /
// MSVC 19.44 flagged test_arena_walk.cpp:639 under /WX while VS2026 / 19.51 no
// longer does, which meant the warnings-as-errors CI build was pinned to the
// newest compiler (independent review B4c). Suppressing it at the one place
// every assertion's condition is evaluated makes the /WX build portable across
// MSVC versions and covers the whole class, not just the one line that surfaced.
// A no-op on GCC and Clang.
#if defined(_MSC_VER)
#define RS_MSVC_ALLOW_CONST_COND __pragma(warning(suppress : 4127))
#else
#define RS_MSVC_ALLOW_CONST_COND
#endif

#define RS_CHECK(condition)                                                  \
    do {                                                                     \
        RS_MSVC_ALLOW_CONST_COND                                             \
        if (!(condition)) {                                                  \
            ::rs::test::report_failure(__FILE__, __LINE__,                   \
                                       "expected: " #condition);             \
        }                                                                    \
    } while (false)

#define RS_CHECK_EQ(a, b) \
    ::rs::test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)

#define RS_CHECK_MESSAGE(condition, message)                                 \
    do {                                                                     \
        RS_MSVC_ALLOW_CONST_COND                                             \
        if (!(condition)) {                                                  \
            ::rs::test::report_failure(__FILE__, __LINE__, (message));       \
        }                                                                    \
    } while (false)

#define RS_TEST_MAIN(suite_name) \
    int main() { return ::rs::test::run_all(suite_name); }

#endif  // RUNTIMESKEPTIC_TESTS_TEST_SUPPORT_HPP
