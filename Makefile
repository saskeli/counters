CFLAGS = -std=c++2a -Wall -Wextra -Wshadow -pedantic -Werror -march=native

.DEFAULT: test

test: test.cpp counters.hpp
	g++ $(CFLAGS) -Ofast -o test test.cpp
