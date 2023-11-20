#include "counters.hpp"
#include <iostream>
#include <array>

template <class A>
void p(A& a) {
    std::cerr << (double(a[0]) / 1000) << " cycles\n"
              << (double(a[1]) / 1000) << " retired instructions\n"
              << (double(a[1]) / a[0]) << " ipc\n"
              << (double(a[2]) / 1000) << " branch misspredictions\n"
              << (double(a[3]) / 1000) << " L1D misses" << std::endl; 
}

int main() {
    std::array<uint64_t, 100000> acc;
    std::array<uint64_t, 100000> nums;
    for (size_t i = 0; i < 100000; ++i) {
        nums[i] = i;
        acc[i] = 1;
    }
    Counters<3> count;
    std::cerr << "Hello World!" << std::endl;
    auto counts = count.accumulate(0);
    std::cerr << counts[0] << " cycles\n"
              << counts[1] << " retired instructions\n"
              << (double(counts[1]) / counts[0]) << " IPC\n" 
              << counts[2] << " branch misspredictions\n"
              << counts[3] << " L1D misses" << std::endl;
    for (size_t i = 0; i < 1000; ++i) {
        count.reset();
        for (size_t j = 0; j < 100000; ++j) {
            acc[j] *= nums[j];
        }
        count.accumulate(1);
        count.accumulate(2);
    }
    std::cerr << "Section 1 on average" << std::endl;
    p(count.get(1));
    std::cerr << "Section 2 on average" << std::endl;
    p(count.get(2));
    for (uint64_t i = 0; i < 100000; ++i) {
        std::cout << acc[i] << std::endl;
    }
    return 0;
}
