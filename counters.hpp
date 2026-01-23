#include <fcntl.h>
#include <linux/perf_event.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#if !defined(__aarch64__) && !defined(__arm__)
#include <immintrin.h>
#include <x86intrin.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <ostream>

namespace count {

/**
 * Different types of available perf counters
 *
 * Counter that are ratios of other counters need to be last.
 *
 * No separate Cycle couter is available, as RTC will allways be used.
 */
enum Counter {
  instructions, /**! Number of retired instructions */
  branch_miss,  /**! Number of branch misspredictions */
  branches,     /**! Number of retired branch instructions */
  L1D_access,   /**! Number of Level 1 data cache accesses */
  L1D_miss,     /**! Number of Level 1 data cache misses */
  L1I_access,   /**! Number of Level 1 instruction cache accesses */
  L1I_miss,     /**! Number of Level 1 instructino cache misses */
  DTLB_miss,    /**! Number of Data TLB misses */
  ITLB_miss,    /**! Number of Instruction TLB misses */
  LL_access,    /**! Number of Last Level cache accesses */
  LL_miss,      /**! Number of Last Level cache misses */
  IPC,          /**! Ratio of retired instructions / elapsed time. Requires the
                   `instructions` counter. Needs to be efter any actual counters. */
  branch_miss_rate, /**! `branch_miss / branches`. Requires both of these
                       counters, and needs to be after any actual counters. */
};

/**
 * Low overhead access to performance counters.
 *
 * Reading perofrmance counters should not require context switching and should
 * have a minimal memory / cycle footprint.
 *
 * Templata parameter `sections` enables simultaniously collection information
 * on multiple sections of code.
 *
 * The fewer counters are defined, the lower the impact of doing the counting on
 * the thing that is counted.
 *
 * The ratio counters `IPC` and `branch_miss_rate` require other conuters to be
 * defined, and need to be last on the list of conters.
 *
 * @tparam sections  Number of instances of each counter.
 * @tparam counters  Parameter pack definition of all counters used.
 */
template <bool pipeline_flush, uint16_t sections, Counter... counters>
class Counters {
  template <bool has_instructions, bool has_b_count, bool has_b_miss,
            bool seen_ratio, Counter C, Counter... rest>
  static constexpr bool validate_counters() {
    if constexpr (C == Counter::IPC) {
      static_assert(has_instructions);
      if constexpr (sizeof...(rest) > 0) {
        return validate_counters<has_instructions, has_b_count, has_b_count,
                                 true, rest...>();
      }
    } else if constexpr (C == Counter::branch_miss_rate) {
      static_assert(has_b_miss);
      static_assert(has_b_count);
      if constexpr (sizeof...(rest) > 0) {
        return validate_counters<has_instructions, has_b_count, has_b_count,
                                 true, rest...>();
      }
    } else {
      static_assert(seen_ratio == false);
      if constexpr (C == Counter::instructions) {
        if constexpr (sizeof...(rest) > 0) {
          return validate_counters<true, has_b_count, has_b_miss, seen_ratio,
                                   rest...>();
        }
      } else if constexpr (C == Counter::branch_miss) {
        if constexpr (sizeof...(rest) > 0) {
          return validate_counters<has_instructions, has_b_count, true,
                                   seen_ratio, rest...>();
        }
      } else if constexpr (C == Counter::branches) {
        if constexpr (sizeof...(rest) > 0) {
          return validate_counters<has_instructions, true, has_b_miss,
                                   seen_ratio, rest...>();
        }
      } else if constexpr (sizeof...(rest) > 0) {
        return validate_counters<has_instructions, has_b_miss, has_b_count,
                                 seen_ratio, rest...>();
      }
    }
    return true;
  }

  static_assert(validate_counters<false, false, false, false, counters...>());

  template <Counter c, Counter... rest>
  static constexpr uint16_t count_counters(uint16_t total = 1) {
    if (c == Counter::IPC || c == Counter::branch_miss_rate) {
      return total;
    }
    if constexpr (sizeof...(rest) > 0) {
      return count_counters<rest...>(total + 1);
    } else {
      return total + 1;
    }
  }

  static const constexpr uint16_t num_counters_ = count_counters<counters...>();
  static const constexpr uint16_t num_values = sizeof...(counters) + 1;
  static const constexpr uint32_t MMAP_SIZE = 4096;
  std::array<perf_event_mmap_page*, num_counters_ - 1> mmaps_;
  std::array<uint64_t, num_counters_> base_counts_;
  std::array<std::array<uint64_t, num_counters_>, sections>
      section_cumulatives_;
  std::array<long, num_counters_ - 1> pmc_id_;

  uint32_t mmap_id(int fd, uint32_t pemmap_index) {
    int err;
    mmaps_[pemmap_index] = (perf_event_mmap_page*)mmap(
        NULL, MMAP_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (mmaps_[pemmap_index] == MAP_FAILED) {
      err = errno;
      std::cerr << "mmap error for counter " << pemmap_index << std::endl;
      std::cerr << err << ": " << strerror(err) << std::endl;
      exit(1);
    }
    if (mmaps_[pemmap_index]->cap_user_rdpmc == 0) {
      std::cerr << "missing rdpmc support for counter " << pemmap_index
                << std::endl;
      exit(1);
    }

    uint32_t r = mmaps_[pemmap_index]->index;
    if (r == 0) {
      std::cerr << "invalid rdpmc id for counter " << pemmap_index << std::endl;
      exit(1);
    }
    return r - 1;
  }

  template <Counter C>
  void set_values(perf_event_attr& pe) {
    if constexpr (C == Counter::instructions) {
      pe.type = PERF_TYPE_HARDWARE;
      pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    } else if constexpr (C == Counter::branch_miss) {
      pe.type = PERF_TYPE_HARDWARE;
      pe.config = PERF_COUNT_HW_BRANCH_MISSES;
    } else if constexpr (C == Counter::branches) {
      pe.type = PERF_TYPE_HARDWARE;
      pe.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
    } else if constexpr (C == Counter::L1D_access) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    } else if constexpr (C == Counter::L1D_miss) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_MISS) << 16;
    } else if constexpr (C == Counter::L1I_access) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    } else if constexpr (C == Counter::L1I_miss) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    } else if constexpr (C == Counter::DTLB_miss) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_DTLB |
                  (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    } else if constexpr (C == Counter::ITLB_miss) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_ITLB |
                  (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    } else if constexpr (C == Counter::LL_access) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    } else if constexpr (C == Counter::LL_miss) {
      pe.type = PERF_TYPE_HW_CACHE;
      pe.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    } else {
      static_assert(false, "Invalid peformance counter");
    }
  }

  template <Counter C, Counter... rest>
  void create_counters(size_t idx = 0) {
    if constexpr (C == Counter::IPC or C == Counter::branch_miss_rate) {
      return;
    } else {
      perf_event_attr pe;
      memset(&pe, 0, sizeof(perf_event_attr));
      pe.size = sizeof(perf_event_attr);
      pe.exclude_kernel = true;
      pe.exclude_hv = true;
      pe.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;
      pe.disabled = true;
      set_values<C>(pe);
      int fd = idx ? pmc_id_[0] : -1;
      pmc_id_[idx] = syscall(SYS_perf_event_open, &pe, 0, -1, fd, 0);
      int err = errno;
      if (pmc_id_[idx] == -1) {
        std::cerr << "Error creating counter " << idx << std::endl;
        std::cerr << err << ": " << strerror(err) << std::endl;
        exit(1);
      }

      if constexpr (sizeof...(rest) > 0) {
        create_counters<rest...>(idx + 1);
      }
    }
  }

  template <Counter c, Counter... rest>
  void output_counter(uint16_t section, size_t div, std::ostream& out,
                      size_t idx, uint64_t cycles, uint64_t instructions = 0,
                      uint64_t branch_miss = 0, uint64_t branch_count = 0) {
    uint64_t val;
    double v;
    if constexpr (c == Counter::IPC or c == Counter::branch_miss_rate) {
      if constexpr (c == Counter::IPC) {
        v = instructions;
        v /= cycles;
        out << "IPC:\t";
      } else {
        v = branch_miss;
        v /= branch_count;
        out << "Branch missprediction ratio:\t";
      }
      out << v << std::endl;
    } else {
      val = section_cumulatives_[section][idx++];
      if constexpr (c == Counter::instructions) {
        instructions = val;
        out << "Instructions:\t";
      } else if constexpr (c == Counter::branch_miss) {
        branch_miss = val;
        out << "Branch misspredictions:\t";
      } else if constexpr (c == Counter::branches) {
        branch_count = val;
        out << "Branch instructions:\t";
      } else if constexpr (c == Counter::L1D_access) {
        out << "L1D hits:\t";
      } else if constexpr (c == Counter::L1D_miss) {
        out << "L1D misses:\t";
      } else if constexpr (c == Counter::L1I_access) {
        out << "L1I hits:\t";
      } else if constexpr (c == Counter::L1I_miss) {
        out << "L1I misses:\t";
      } else if constexpr (c == Counter::DTLB_miss) {
        out << "DTLB misses:\t";
      } else if constexpr (c == Counter::ITLB_miss) {
        out << "ITLB misses:\t";
      } else if constexpr (c == Counter::LL_access) {
        out << "LL hits:\t";
      } else if constexpr (c == Counter::LL_miss) {
        out << "LL misses:\t";
      } else {
        static_assert(false, "Invalid peformance counter");
      }
      if (div > 1) {
        v = val;
        v /= div;
        out << v << std::endl;
      } else {
        out << val << std::endl;
      }
    }
    if constexpr (sizeof...(rest) > 0) {
      output_counter<rest...>(section, div, out, idx, cycles, instructions,
                              branch_miss, branch_count);
    }
  }

  void serialize() {
#if defined(__znver1__) || defined(__znver2__) || defined(__znver3__) || \
    defined(__znver4__) || defined(__znver5__)
    __asm__ __volatile__("cpuid" ::"a"(0) : "%ebx", "%ecx", "%edx");
#elif !defined(__aarch64__) && !defined(__arm__)
    int dummy = 0;
    __asm__ __volatile__("xchg %0, %0" : "+r"(dummy)::"rax", "memory");
#else
    static_assert(false, "serialization not implemented on arm");
#endif
  }

 public:
  /**
   * Create and start the counters defined in the template.
   */
  Counters() : mmaps_(), base_counts_(), section_cumulatives_(), pmc_id_() {
    create_counters<counters...>();

    int err = prctl(PR_TASK_PERF_EVENTS_ENABLE);
    if (err < 0) {
      err = errno;
      std::cerr << "Error enabling counter group" << std::endl;
      std::cerr << err << ": " << strerror(err) << std::endl;
      exit(1);
    }
#if !defined(__aarch64__) && !defined(__arm__)
    for (size_t i = 0; i < pmc_id_.size(); ++i) {
      pmc_id_[i] = mmap_id(pmc_id_[i], i);
    }
#endif
    reset();
  }

  /**
   * Sets the zero point for the timer.
   */
  void reset() {
#if defined(__aarch64__) || defined(__arm__)
    uint64_t val;
    asm volatile("mrs %0, pmccntr_el0" : "=r"(val));
    base_counts_[0] = val;
#else
    base_counts_[0] = __rdtsc();
#endif
    for (size_t i = 0; i < pmc_id_.size(); ++i) {
#if defined(__aarch64__) || defined(__arm__)
      if (read(pmc_id_[i], base_counts_.data() + i + 1, sizeof(uint64_t)) != sizeof(uint64_t)) [[unlikely]] {
        int err = errno;
        std::cerr << "Error reading counter i" << std::endl;
        std::cerr << err << ": " << strerror(err) << std::endl;
      }
#else
      base_counts_[i + 1] = __rdpmc(pmc_id_[i]);
#endif
    }
    if constexpr (pipeline_flush) {
      serialize();
    }
  }

  /**
   * Clears all accumulators.
   */
  void clear() {
    for (auto& arr : section_cumulatives_) {
      std::fill(arr.begin(), arr.end(), 0);
    }
    reset();
  }

  /**
   * Adds counter accumulation from last reset for `section`
   *
   * Templated version is probably a bit lower overhead...
   *
   * @tparam section  Section to accumulate.
   */
  template <uint16_t section = 0>
  void accumulate() {
    if constexpr (pipeline_flush) {
      serialize();
    }
#if defined(__aarch64__) || defined(__arm__)
    uint64_t c;
    asm volatile("mrs %0, pmccntr_el0" : "=r"(c));
#else
    uint64_t c = __rdtsc();
#endif
    section_cumulatives_[section][0] += c - base_counts_[0];
    base_counts_[0] = c;
    for (uint16_t i = 0; i < pmc_id_.size(); ++i) {
#if defined(__aarch64__) || defined(__arm__)
      if (read(pmc_id_[i], &c, sizeof(uint64_t)) != sizeof(uint64_t)) [[unlikely]] {
        int err = errno;
        std::cerr << "Error reading counter i" << std::endl;
        std::cerr << err << ": " << strerror(err) << std::endl;
      }
#else
      c = __rdpmc(pmc_id_[i]);
#endif
      section_cumulatives_[section][i + 1] += c - base_counts_[i + 1];
      base_counts_[i + 1] = c;
    }
    if constexpr (pipeline_flush) {
      serialize();
    }
  }

  /**
   * Adds counter accumulation from last reset for `section`
   *
   * Templated version is probably a bit lower overhead...
   *
   * @param section  Section to accumulate.
   */
  void accumulate(uint16_t section) {
    if constexpr (pipeline_flush) {
      serialize();
    }
#if defined(__aarch64__) || defined(__arm__)
    uint64_t c;
    asm volatile("mrs %0, pmccntr_el0" : "=r"(c));
#else
    uint64_t c = __rdtsc();
#endif
    section_cumulatives_[section][0] += c - base_counts_[0];
    base_counts_[0] = c;
    for (uint16_t i = 0; i < pmc_id_.size(); ++i) {
#if defined(__aarch64__) || defined(__arm__)
      if (read(pmc_id_[i], &c, sizeof(uint64_t)) != sizeof(uint64_t)) [[unlikely]] {
        int err = errno;
        std::cerr << "Error reading counter i" << std::endl;
        std::cerr << err << ": " << strerror(err) << std::endl;
      }
#else
      c = __rdpmc(pmc_id_[i]);
#endif
      section_cumulatives_[section][i + 1] += c - base_counts_[i + 1];
      base_counts_[i + 1] = c;
    }
    if constexpr (pipeline_flush) {
      serialize();
    }
  }

  /**
   * Get acumulated counter data as an array reference.
   *
   * You probably don't want to do this...
   */
  const std::array<uint64_t, num_counters_>& get(uint16_t section) {
    return section_cumulatives_[section];
  }

  /**
   * Output perf results for `section` to an output stream (`std::cout`).
   *
   * Optionally takes a divisor to allow dividing counter values with the
   * number of runs.
   *
   * @param section  Section to output counters for.
   * @param div      Divisor to devide non-ratio timers.
   * @param o_stream Stream to output perf results.
   */
  void output_counters(uint16_t section, size_t div = 1,
                       std::ostream& out = std::cout) {
    if (div == 1 or div == 0) {
      out << "Cycles:\t" << section_cumulatives_[section][0] << "\n";
    } else {
      double v = section_cumulatives_[section][0];
      out << "Cycles:\t" << v / div << "\n";
    }
    output_counter<counters...>(section, div, out, 1,
                                section_cumulatives_[section][0]);
  }

  ~Counters() {
    prctl(PR_TASK_PERF_EVENTS_DISABLE);
    for (long fd : pmc_id_) {
      close(fd);
    }
    for (perf_event_mmap_page* p : mmaps_) {
      munmap(p, MMAP_SIZE);
    }
  }
};

template <uint16_t sect = 1>
using Default = Counters<false, sect, Counter::instructions,
                         Counter::branch_miss, Counter::L1D_miss, Counter::IPC>;

}  // namespace count