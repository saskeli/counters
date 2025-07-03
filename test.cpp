#include "counters.hpp"
#include <iostream>
#include <array>
#include <cstdint>

int main() {
    std::array<uint64_t, 100000> acc;
    std::array<uint64_t, 100000> nums;
    for (size_t i = 0; i < 100000; ++i) {
        nums[i] = rand();
        acc[i] = 1;
    }
    count::Default<3> count;
    std::cout << "For printing this 'Hello World!':" << std::endl;
    count.accumulate();
    count.output_counters(0);
    size_t n = 1000;
    for (size_t i = 0; i < n; ++i) {
        count.reset();
        for (size_t j = 0; j < 100000; ++j) {
            acc[j] *= nums[j];
        }
        count.accumulate<1>();
        count.accumulate<2>();
    }
    std::cout << "\nArray multiplication on average" << std::endl;
    count.output_counters(1, n);
    std::cout << "\n'Nothing' on average" << std::endl;
    count.output_counters(2, n);

    uint64_t v = 0;
    for (uint64_t i = 0; i < 100000; ++i) {
        v += acc[i];
    }
    std::cout << "\nChecksum: " << v << std::endl;
    return 0;
}
