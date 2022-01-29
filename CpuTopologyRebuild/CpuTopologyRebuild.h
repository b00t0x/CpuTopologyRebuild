#include <IOKit/IOService.h>

struct machine_info {
    integer_t major_version;    /* kernel major version id */
    integer_t minor_version;    /* kernel minor version id */
    integer_t max_cpus;         /* max number of CPUs possible */
    uint32_t  memory_size;      /* size of memory in bytes, capped at 2 GB */
    uint64_t  max_mem;          /* actual size of physical memory */
    uint32_t  physical_cpu;     /* number of physical CPUs now available */
    integer_t physical_cpu_max; /* max number of physical CPUs possible */
    uint32_t  logical_cpu;      /* number of logical cpu now available */
    integer_t logical_cpu_max;  /* max number of physical CPUs possible */
};

extern struct machine_info machine_info;

class CpuTopologyRebuild : public IOService {
    OSDeclareDefaultStructors(CpuTopologyRebuild)
public:
    virtual IOService* probe(IOService* provider, SInt32* score) override;
};
