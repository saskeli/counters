#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

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
    uint32_t inst_id, bm_id, l1dm_id;

    struct read_format {
        uint64_t nr;            /* The number of events */
        uint64_t time_enabled;  /* if PERF_FORMAT_TOTAL_TIME_ENABLED */
        uint64_t time_running;  /* if PERF_FORMAT_TOTAL_TIME_RUNNING */
        struct {
            uint64_t value;     /* The value of the event */
            uint64_t id;        /* if PERF_FORMAT_ID */
        } values[num_counters_];
    } counter_data;

    public:
    Counters() : base_counts_(), section_cumulatives_() {
        perf_event_attr pe_ins;
        memset(&pe_ins, 0, sizeof(perf_event_attr));
        pe_ins.type = PERF_TYPE_HARDWARE;
        pe_ins.size = sizeof(perf_event_attr);
        pe_ins.config = PERF_COUNT_HW_INSTRUCTIONS;
        pe_ins.exclude_kernel = true;
        pe_ins.exclude_hv = true;
        pe_ins.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;
        pe_ins.disabled = true;
        pe_ins.pinned = true;
        //More options?
        group_fd = syscall(SYS_perf_event_open, &pe_ins, 0, -1, -1, 0);
        int err = errno;
        if (group_fd == -1) {
            std::cerr << "Error creating group leader (instruction counter)" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            if (pe_ins.size != sizeof(perf_event_attr)) {
                std::cout << pe_ins.size << " != " << sizeof(perf_event_attr) << std::endl;
            }
            exit(1);
        }
        err = read(group_fd, &counter_data, sizeof(read_format));
        if (err == -1) {
            err = errno;
            std::cerr << "Error reading counter data from group leader" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            ioctl(group_fd, PERF_EVENT_IOC_DISABLE, 0);
            close(group_fd);
            exit(1);
        }
        //Retrieve group id from cyc_fd?

        perf_event_attr pe_bm;
        memset(&pe_bm, 0, sizeof(perf_event_attr));
        pe_bm.type = PERF_TYPE_HARDWARE;
        pe_bm.size = sizeof(perf_event_attr);
        pe_bm.config = PERF_COUNT_HW_CACHE_MISSES;
        pe_bm.exclude_kernel = true;
        pe_bm.exclude_hv = true;
        bm_fd = syscall(SYS_perf_event_open, &pe_bm, 0, -1, group_fd, 0);
        if (bm_fd == -1) {
            err = errno;
            std::cerr << "Error creating branch misprediction counter" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        perf_event_attr pe_l1dm;
        memset(&pe_l1dm, 0, sizeof(perf_event_attr));
        pe_l1dm.type = PERF_TYPE_HARDWARE;
        pe_l1dm.size = sizeof(perf_event_attr);
        pe_l1dm.config1 = PERF_COUNT_HW_BRANCH_MISSES;
        pe_l1dm.exclude_kernel = true;
        pe_l1dm.exclude_hv = true;
        l1dm_fd = syscall(SYS_perf_event_open, &pe_l1dm, 0, -1, group_fd, 0);
        if (l1dm_fd == -1) {
            err = errno;
            std::cerr << "Error creating L1D miss counter" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        err = ioctl(group_fd, PERF_EVENT_IOC_RESET, 0);
        if (err < 0) {
            err = errno;
            std::cerr << "Error resetting counters for init" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }
        err = ioctl(group_fd, PERF_EVENT_IOC_ENABLE, 0);
        if (err < 0) {
            err = errno;
            std::cerr << "Error enabling counter group" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        read(group_fd, &counter_data, sizeof(read_format));
        std::cerr << counter_data.nr << ", " << counter_data.time_enabled << ", " << counter_data.time_running << std::endl;
        for (size_t i = 0; i < counter_data.nr; ++i) {
            std::cerr << counter_data.values[i].value << ", " << counter_data.values[i].id << std::endl;
        }
        inst_id = counter_data.values[0].id;
        bm_id = counter_data.values[1].id;
        l1dm_id = counter_data.values[2].id;
        base_counts_[0] = __builtin_ia32_rdtsc();
        base_counts_[1] = __builtin_ia32_rdpmc(inst_id);
        base_counts_[2] = __builtin_ia32_rdpmc(bm_id);
        base_counts_[3] = __builtin_ia32_rdpmc(l1dm_id);
    }

    void reset() {
        base_counts_[0] = __builtin_ia32_rdtsc();
        base_counts_[1] = __builtin_ia32_rdpmc(inst_id);
        base_counts_[2] = __builtin_ia32_rdpmc(bm_id);
        base_counts_[3] = __builtin_ia32_rdpmc(l1dm_id);
    }

    const std::array<uint64_t, num_counters_>& accumulate(uint16_t i) {
        uint64_t c = __builtin_ia32_rdtsc();
        section_cumulatives_[i][0] = c - base_counts_[0];
        base_counts_[0] = c;
        c = __builtin_ia32_rdpmc(inst_id);
        section_cumulatives_[i][1] = c - base_counts_[1];
        base_counts_[1] = c;
        c = __builtin_ia32_rdpmc(bm_id);
        section_cumulatives_[i][2] = c - base_counts_[2];
        base_counts_[2] = c;
        c = __builtin_ia32_rdpmc(l1dm_id);
        section_cumulatives_[i][3] = c - base_counts_[3];
        base_counts_[3] = c;
        return section_cumulatives_[i];
    }

    ~Counters() {
        ioctl(group_fd, PERF_EVENT_IOC_DISABLE, 0);
        close(group_fd);
        close(bm_fd);
        close(l1dm_fd);
    }
};