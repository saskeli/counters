#include "counters.hpp"
#include <iostream>

int main() {
    Counters<1> count;
    std::cout << "Hello World!" << std::endl;
    for (auto v : count.accumulate(0)) {
        std::cout << v << std::endl;
    }
    return 0;
}
