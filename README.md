# Counters

Very simple user space perf counting, assuming a modern, non-paranoid linux kernel.

Currently non-modulear, hard coded setup for

* Cycles
* Retired instructions
* Branch misspredictions
* L1D cache read misses

Note that this may not be any faster or more efficient than existing applications, but should hopefully 

* do less poisoning of cache
* do less branch predictor poisoning
* work without kernel hacking

than competing approaches I've found. While being easy to use.

Will currently not deal with overflows at all.

## Usage

```c++
#import counters.hpp
```

At any point before starting actual timing, initalize a counters object, templated with how many different sections you want to profile.

```c++
Counters<2> count;
```

Now you should be able to profile code sections like so:

```c++
for (size_t i = 0; i < iterations; ++i) {
  count.reset();
  /* some intresting code #1 */
  count.accumulate(0);
  /* some intresting code #2 */
  count.accumulate(1);
  /* non-intresting code */
}
// Get arrays of cycles, instruction, branch misses, cache misses
auto c1 = count.get(0); // for secition 1
auto c2 = count.get(1); // for secition 2
```
