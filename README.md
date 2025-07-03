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
count::Default<2> count;
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
// Output: 
//  * Mean cycles per iteration 
//  * Mean instructions per iteration
//  * Mean L1D cache misses per iteration
//  * Mean branch misses per iteration
//  * Overall IPC
std::cout << "For section 0" << std::endl;
count.output_counters(0, iterations);

std::cout << "\nAnd for section 1 (to std:cerr)" << std::endl; 
count.output_counters(0, iterations, std::cerr);
```

## More details

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