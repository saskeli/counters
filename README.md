# Counters

Very simple user space perf counting, assuming a modern, non-paranoid linux kernel.

Note that this may not be any faster or more efficient than existing applications, but should hopefully 

* do less poisoning of cache
* do less branch predictor poisoning
* work without kernel hacking

than competing approaches I've found. While being easy to use.

Will currently not deal with overflows at all.

## Simple usage

```c++
#import "counters.hpp"
```

At any point before starting actual timing, initalize a counters object, templated with how many different sections you want to profile.

```c++
count::Default<2> counter;
```

Now you should be able to profile code sections like so:

```c++
for (size_t i = 0; i < iterations; ++i) {
  counter.reset();
  /* some intresting code #1 */
  counter.accumulate();
  /* some intresting code #2 */
  counter.accumulate<1>();
  /* non-intresting code */
}
// Output: 
//  * Mean cycles per iteration 
//  * Mean instructions per iteration
//  * Mean L1D cache misses per iteration
//  * Mean branch misses per iteration
//  * Overall IPC
std::cout << "For section 0" << std::endl;
counter.output_counters(0, iterations);

std::cout << "\nAnd for section 1 (to std:cerr)" << std::endl; 
counter.output_counters(1, iterations, std::cerr);
```

## More details

### Configuring

The `Counters<uint16_t sections, Counter... counters>` class can be used to manually specify the counters used and the number of sections.

Available counters:

```c++
  instructions,      /**! Number of retired instructions */
  branch_miss,       /**! Number of branch misspredictions */
  branches,          /**! Number of retired branch instructions */
  L1D_access,        /**! Number of Level 1 data cache accesses */
  L1D_miss,          /**! Number of Level 1 data cache misses */
  L1I_access,        /**! Number of Level 1 instruction cache accesses */
  L1I_miss,          /**! Number of Level 1 instructino cache misses */
  DTLB_miss,         /**! Number of Data TLB misses */
  ITLB_miss,         /**! Number of Instruction TLB misses */
  page_faults,       /**! Number of page faults */
  page_faults_minor, /**! Number of page faults not requiring secondary storage
                        IO */
  page_faults_major, /**! Number of page faults requiring secondary storage IO
                      */
  context_switches, /**! Number of context switches (related to this process) */
  alignment_faults, /**! Counts unaligned memory accesses */
  emulation_faults, /**! Counts number of emulated instructions */
  IPC, /**! Ratio of retired instructions / elapsed time. Requires the
          `instructions` counter. Needs to be efter any actual counters. */
  branch_miss_rate, /**! `branch_miss / branches`. Requires both of these
                       counters, and needs to be after any actual counters. */
```

For example, if you are only intrested in TLB misses in a single section, you would do:

```c++
count::Counters<1, Counter::DTLB_miss, Counter::ITLB_miss> counters;
```

This will still output elapsed cycles, since the counter is allways running anyway.

### Expected performance

On my machine, running `./test` yields:

```bash
[saskeli@computer counters]$ ./test
For printing this 'Hello World!':
Cycles:	161270
Instructions:	5232
Branch misspredictions:	103
L1D misses:	228
IPC:	0.0324425

Array multiplication on average
Cycles:	71.494
Instructions:	375.03
Branch misspredictions:	0.001
L1D misses:	25.003
IPC:	5.24562

'Nothing' on average
Cycles:	0.069
Instructions:	0.031
Branch misspredictions:	0
L1D misses:	0.001
IPC:	0.449275

Checksum: 14990041521197361340
```

Statistics for the single print are very unstable and don't make too much sense. Not the intended use-case.

Array multiplication statistics are very stable, but the actual values don't necessarily make sence. But if you change the implementation, you should be able to reason about the performance based on the changes in the statistics.

Nothing is generally nothing. `accumulate` has low overhead.