#include <linux/perf_event.h>
#include <sys/syscall.h>

#include <array>
#include <cstdint>

template <uint16_t sections>
class Counters {
   private:
    static const constexpr uint16_t num_counters_;
    std::array<uint64_t, num_counters> base_counts_;
    std::array<std::array<uint64_t, num_counters>, sections>
        section_cumulatives_;

    public:
    Counters() : base_counts_(), section_cumulatives_() {
        perf_event_attr pe;
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(pe);
        pe.config = PERF_COUNT_HW_CPU_CYCLES;
        pe.exclude_kernel = true;
        pe.exclude_hv = true;

        syscall(SYS_perf_event_open, pe, 0, -1, -1, 0);
    }
};