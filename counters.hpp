#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <array>
#include <cstdint>
#include <iostream>

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
        if (group_fd == -1) {
            group_fd = errno;
            std::cerr << "Error creating group leader (instruction counter)" << std::endl;
            std::cerr << group_fd << ": " << strerror(group_fd) << std::endl;
            exit(1);
        }
        int err = read(group_fd, &counter_data, sizeof(read_format));
        if (err == -1) {
            err = errno;
            std::cerr << "Error reading counter data from group leader" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }
        //Retrieve group id from cyc_fd?

        perf_event_attr pe_bm;
        pe_bm.type = PERF_TYPE_HARDWARE;
        pe_bm.size = sizeof(perf_event_attr);
        pe_bm.config = PERF_COUNT_HW_BRANCH_MISSES;
        pe_bm.exclude_kernel = true;
        pe_bm.exclude_hv = true;
        //More options and sumit to group?
        bm_fd = syscall(SYS_perf_event_open, &pe_bm, 0, -1, counter_data.values[0].id, 0);
        if (bm_fd == -1) {
            err = errno;
            std::cerr << "Error creating branch misprediction counter" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }

        perf_event_attr pe_l1dm;
        pe_l1dm.type = PERF_TYPE_HW_CACHE;
        pe_l1dm.size = sizeof(perf_event_attr);
        pe_l1dm.config1 = int(PERF_COUNT_HW_CACHE_L1D) | int(PERF_COUNT_HW_CACHE_OP_READ) | int(PERF_COUNT_HW_CACHE_RESULT_MISS);
        pe_l1dm.exclude_kernel = true;
        pe_l1dm.exclude_hv = true;
        // More options? and submit to group?
        l1dm_fd = syscall(SYS_perf_event_open, &pe_l1dm, 0, -1, counter_data.values[0].id, 0);
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

        uint32_t mm_size = (1 + (1u << num_counters_)) * sizeof(perf_event_mmap_page);
        char* mm_fd = (char*)mmap(NULL, mm_size, PROT_READ, MAP_PRIVATE, group_fd, 0);
        if (mm_fd == MAP_FAILED) {
            err = errno;
            std::cerr << "Error mapping perf event pages" << std::endl;
            std::cerr << err << ": " << strerror(err) << std::endl;
            exit(1);
        }
        perf_event_mmap_page* mm_p = reinterpret_cast<perf_event_mmap_page*>(mm_fd);
        std::cout << mm_p->time_cycles << std::endl;
        munmap(mm_fd, mm_size);
    }
/*
    reset() {
        base_counts_[0] = __builtin_ia32_rdtsc();
        int id = 1; //?
        base_counts_[1] = __builtin_ia32_rdpmc(id);
        id = 2;
        base_counts_[2] = __builtin_ia32_rdpmc(id);
        id = 3;
        base_counts_[3] = __builtin_ia32_rdpmc(id);
    }
*/
    ~Counters() {
        
        ioctl(group_fd, PERF_EVENT_IOC_DISABLE, 0);
        close(group_fd);
        close(bm_fd);
        close(l1dm_fd);
    }
};