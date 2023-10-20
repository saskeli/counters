CFLAGS = -std=c++2a -Wall -Wextra -Wshadow -pedantic -Werror -DCACHE_LINE=$(CL) -march=native

.DEFAULT: test

test: test.cpp counters.hpp
	g++ $(CFLAGS) -Ofast -o test test.cpp
