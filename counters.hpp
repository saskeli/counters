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
    static const constexpr uint32_t MMAP_SIZE = 4096;
    perf_event_mmap_page* mmaps[3];
    std::array<uint64_t, num_counters_> base_counts_;
    std::array<std::array<uint64_t, num_counters_>, sections>
        section_cumulatives_;
    int group_fd, bm_fd, l1dm_fd;
    uint32_t pmc_id[3];

    uint32_t mmap_id(int fd, const std::string& counter_name, uint32_t pemmap_index) {
        int err;
        mmaps[pemmap_index] = (perf_event_mmap_page*)mmap(NULL, MMAP_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (mmaps[pemmap_index] == MAP_FAILED) {
            err = errno;
            std::cerr << "mmap error for " << counter_name << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }
        if (mmaps[pemmap_index]->cap_user_rdpmc == 0) {
            std::cerr << "missing rdpmc support for " << counter_name << std::endl;
            exit(1);
        }

        std::cerr << counter_name << " pmc width: " << mmaps[pemmap_index]->pmc_width << ", index: " << mmaps[pemmap_index]->index << std::endl;

        uint32_t r = mmaps[pemmap_index]->index;
        if (r == 0) {
            std::cerr << "invalid rdpmc id for " << counter_name << std::endl;
            exit(1);
        }
        /*err = munmap(mmaps[pemmap_index], MMAP_SIZE);
        if (err != 0) {
            err = errno;
            std::cerr << "munmap failed for " << counter_name << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }*/
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

        pmc_id[0] = mmap_id(group_fd, "group leader (instruction counter)", 0);
        pmc_id[1] = mmap_id(bm_fd, "branch missprediction counter", 1);
        pmc_id[2] = mmap_id(l1dm_fd, "cache miss counter", 2);
        std::cerr << "counters initialized and running" << std::endl;
        std::cerr << " counter ids: " << pmc_id[0] << ", " << pmc_id[1] << ", " << pmc_id[2] << std::endl;
        reset();
    }

    void reset() {
        std::cerr << "reset __rdtsc" << std::endl;
        base_counts_[0] = __rdtsc();
        std::cerr << " -> " << base_counts_[0] << "\nrdpmc(" << pmc_id[0] << ", " << mmaps[0]->index << ")" << std::endl;
        base_counts_[1] = __rdpmc(pmc_id[0]);
        std::cerr << " -> " << base_counts_[1] << "\nrdpmc(" << pmc_id[1] << ", " << mmaps[1]->index << ")" << std::endl;
        base_counts_[2] = __rdpmc(pmc_id[1]);
        std::cerr << " -> " << base_counts_[2] << "\nrdpmc(" << pmc_id[2] << ", " << mmaps[2]->index << ")" << std::endl;
        base_counts_[3] = __rdpmc(pmc_id[2]);
        std::cerr << " -> " << base_counts_[3] << "\nDone!" << std::endl;
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
        for (uint32_t i = 0; i < 3; ++i) {
            munmap(mmaps[i], MMAP_SIZE);
        }
    }
};