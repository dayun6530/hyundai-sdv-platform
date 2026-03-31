#pragma once

#include <iostream>
#include <string>

namespace sqa::test {

struct TestCounter {
    int total = 0;
    int passed = 0;
};

inline void check(TestCounter& counter, bool condition, const std::string& name) {
    counter.total++;
    if (condition) {
        counter.passed++;
        std::cout << "[PASS] " << name << std::endl;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
    }
}

inline int finalize(const TestCounter& counter, const std::string& suiteName) {
    std::cout << "=== " << suiteName << " : "
              << counter.passed << "/" << counter.total << " passed ===" << std::endl;
    return (counter.passed == counter.total) ? 0 : 1;
}

}  // namespace sqa::test
