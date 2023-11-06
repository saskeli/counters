#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <x86intrin.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <iomanip>

template <uint16_t sections>
class Counters {
   private:
    static const constexpr uint16_t num_counters_ = 4;
    std::array<uint64_t, num_counters_> base_counts_;
    std::array<std::array<uint64_t, num_counters_>, sections>
        section_cumulatives_;
    int group_fd, bm_fd, l1dm_fd;
    uint32_t pmc_id[3];

    struct read_format {
        uint64_t nr;            /* The number of events */
        struct {
            uint64_t value;     /* The value of the event */
            uint64_t id;
        } values[num_counters_ - 1];
    } counter_data;

    static uint32_t mmap_id(int fd, const std::string& counter_name) {
        struct perf_event_mmap_page* perf_mm;
        const constexpr MMAP_SIZE = 4096;
        int err;
        perf_mm = (perf_event_mmap_page*)mmap(NULL, MMAP_SIZE, PROT_READ, MAP_SHARED, group_fd, 0);
        if (perf_mm == MAP_FAILED) {
            err = errno;
            std::cerr << "mmap error for " << counter_name << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }
        if (perf_mm->cap_user_rdpmc == 0) {
            std::cerr << "missing rdpmc support for " << counter_name << std::endl;
            exit(1);
        }
        uint32_t r = perf_mm->index;
        if (r == 0) {
            std::cerr << "invalid rdpmc id for " << counter_name << std::endl;
            exit(1);
        }
        err = munmap(perf_mm, MMAP_SIZE);
        if (err != 0) {
            err = errno:
            std::cerr << "munmap failed for " << counter_name << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }
        return r - 1;
    }

    public:
    Counters() : base_counts_(), section_cumulatives_() {
        perf_event_attr pe;
        memset(&pe, 0, sizeof(perf_event_attr));
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(perf_event_attr);
        pe.config = PERF_COUNT_HW_INSTRUCTIONS;
        pe.exclude_kernel = true;
        pe.exclude_hv = true;
        pe.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;
        pe.disabled = true;
        group_fd = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
        int err = errno;
        if (group_fd == -1) {
            std::cerr << "Error creating group leader (instruction counter)" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            if (pe.size != sizeof(perf_event_attr)) {
                std::cout << pe.size << " != " << sizeof(perf_event_attr) << std::endl;
            }
            exit(1);
        }

        memset(&pe, 0, sizeof(perf_event_attr));
        pe.config = PERF_COUNT_HW_BRANCH_MISSES;
        bm_fd = syscall(SYS_perf_event_open, &pe, 0, -1, group_fd, 0);
        if (bm_fd == -1) {
            err = errno;
            std::cerr << "Error creating branch misprediction counter" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        memset(&pe, 0, sizeof(perf_event_attr));
        pe.config = PERF_COUNT_HW_CACHE_MISSES;
        l1dm_fd = syscall(SYS_perf_event_open, &pe, 0, -1, group_fd, 0);
        if (l1dm_fd == -1) {
            err = errno;
            std::cerr << "Error creating cache miss counter" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        err = prctl(PR_TASK_PERF_EVENTS_ENABLE);
        if (err < 0) {
            err = errno;
            std::cerr << "Error enabling counter group" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        pmc_id[0] = mmap_id(group_fd, "group leader (instruction counter)");
        pmc_id[1] = mmap_id(bm_fd, "branch missprediction counter");
        pmc_id[2] = mmap_id(l1dm_fd, "cache miss counter");
        std::cerr << "counters initialized and running" << std::endl;
        reset();
    }

    void reset() {
        base_counts_[0] = __rdtsc();
        base_counts_[1] = __rdpmc(pmc_id[0]);
        base_counts_[2] = __rdpmc(pmc_id[1]);
        base_counts_[3] = __rdpmc(pmc_id[2]);
    }

    const std::array<uint64_t, num_counters_>& accumulate(uint16_t i) {
        uint64_t c = __rdtsc();
        section_cumulatives_[i][0] = c - base_counts_[0];
        base_counts_[0] = c;
        c = __rdpmc(pmc_id[0]);
        section_cumulatives_[i][1] = c - base_counts_[1];
        base_counts_[1] = c;
        c = __rdpmc(pmc_id[1]);
        section_cumulatives_[i][2] = c - base_counts_[2];
        base_counts_[2] = c;
        c = __rdpmc(pmc_id[2]);
        section_cumulatives_[i][3] = c - base_counts_[3];
        base_counts_[3] = c;
        return section_cumulatives_[i];
    }

    ~Counters() {
        prctl(PR_TASK_PERF_EVENTS_DISABLE);
        close(group_fd);
        close(bm_fd);
        close(l1dm_fd);
    }
};