#define P_CORE_MAX_COUNT 32
#define E_CORE_MAX_COUNT 64

#include "CpuTopologyRebuild.h"
#include <i386/cpu_topology.h>
// #include <i386/cpuid.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_version.hpp>

OSDefineMetaClassAndStructors(CpuTopologyRebuild, IOService)

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

bool print_only = false;
bool smt_spoof = false;
x86_lcpu_t *p0_cpus[P_CORE_MAX_COUNT]; // P-Cores
x86_lcpu_t *p1_cpus[P_CORE_MAX_COUNT]; // P-Cores HT
x86_lcpu_t *e0_cpus[E_CORE_MAX_COUNT]; // E-Cores
int p0_count, p1_count, e0_count;
int e_core_first = -1;

extern "C" void x86_validate_topology(void);
extern "C" int kdb_printf_unbuffered(const char *fmt, ...);

static void print_cpu_topology(void) {
    x86_pkg_t  *pkg = x86_pkgs;
    x86_core_t *core;
    x86_lcpu_t *cpu;

    SYSLOG("ctr", "CPU: physical_cpu_max=%d, logical_cpu_max=%d", machine_info.physical_cpu_max, machine_info.logical_cpu_max);
    core = pkg->cores;
    while (core != nullptr) {
        SYSLOG("ctr", "  Core(p/l): %d/%d (lcpus: %d)", core->pcore_num, core->lcore_num, core->num_lcpus);
        cpu = core->lcpus;
        while (cpu != nullptr) {
            const char *type = cpu->pnum < e_core_first ? cpu->pnum % 2 == 0 ? "P0" : "P1" : "E0";
            SYSLOG("ctr", "    LCPU_%s(n/p/l): %2d/%2d/%d", type, cpu->cpu_num, cpu->pnum, cpu->lnum);
            cpu = cpu->next_in_core;
        }
        core = core->next_in_pkg;
    }
}

static void print_lcpu_topology(x86_lcpu_t *lcpu) {
    SYSLOG("ctr", "      lcpu(%p): pnum=%d, lnum=%d, cpu_num=%d, primary=%d, master=%d",
        lcpu, lcpu->pnum, lcpu->lnum, lcpu->cpu_num, lcpu->primary, lcpu->master);
}

static void print_core_topology(x86_core_t *core) {
    x86_lcpu_t *lcpu = core->lcpus;

    SYSLOG("ctr", "    Core(%p): pcore_num=%d, lcore_num=%d, num_lcpus=%d", core, core->pcore_num, core->lcore_num, core->num_lcpus);

    SYSLOG("ctr", "    Core->lcpu chain:");
    while (lcpu != nullptr) {
        SYSLOG("ctr", "      %p(%d/%d)", lcpu, lcpu->pnum, lcpu->lnum);
        lcpu = lcpu->next_in_core;
    }

    lcpu = core->lcpus;
    while (lcpu != nullptr) {
        print_lcpu_topology(lcpu);
        lcpu = lcpu->next_in_core;
    }
}

static void print_die_topology(x86_die_t *die) {
    x86_core_t *core = die->cores;
    x86_lcpu_t *lcpu = die->lcpus;

    SYSLOG("ctr", "  Die(%p): pdie_num=%d, ldie_num=%d, num_cores=%d", die, die->pdie_num, die->ldie_num, die->num_cores);

    SYSLOG("ctr", "  Die->Core chain:");
    while (core != nullptr) {
        SYSLOG("ctr", "    %p(%d/%d)", core, core->pcore_num, core->lcore_num);
        core = core->next_in_die;
    }
    SYSLOG("ctr", "  Die->lcpu chain:");
    while (lcpu != nullptr) {
        SYSLOG("ctr", "    %p(%d/%d)", lcpu, lcpu->pnum, lcpu->lnum);
        lcpu = lcpu->next_in_die;
    }

    core = die->cores;
    while (core != nullptr) {
        print_core_topology(core);
        core = core->next_in_die;
    }
}

static void print_pkg_topology(x86_pkg_t *pkg) {
    x86_die_t  *die = pkg->dies;
    x86_core_t *core = pkg->cores;
    x86_lcpu_t *lcpu = pkg->lcpus;

    SYSLOG("ctr", "Pkg(%p): ppkg_num=%d, lpkg_num=%d, num_dies=%d", pkg, pkg->ppkg_num, pkg->lpkg_num, pkg->num_dies);

    SYSLOG("ctr", "Pkg->Die chain:");
    while (die != nullptr) {
        SYSLOG("ctr", "  %p(%d/%d)", die, die->pdie_num, die->ldie_num);
        die = die->next_in_pkg;
    }
    SYSLOG("ctr", "Pkg->Core chain:");
    while (core != nullptr) {
        SYSLOG("ctr", "  %p(%d/%d)", core, core->pcore_num, core->lcore_num);
        core = core->next_in_pkg;
    }
    SYSLOG("ctr", "Pkg->lcpu chain:");
    while (lcpu != nullptr) {
        SYSLOG("ctr", "  %p(%d/%d)", lcpu, lcpu->pnum, lcpu->lnum);
        lcpu = lcpu->next_in_pkg;
    }

    die = pkg->dies;
    while (die != nullptr) {
        print_die_topology(die);
        die = die->next_in_pkg;
    }
}

static void print_cpu_topology2(void) {
    x86_pkg_t  *pkg = x86_pkgs;

    SYSLOG("ctr", "CPU: physical_cpu_max=%d, logical_cpu_max=%d", machine_info.physical_cpu_max, machine_info.logical_cpu_max);
    while (pkg != nullptr) {
        print_pkg_topology(pkg);
        pkg = pkg->next;
    }
}

static void load_cpus(void) {
    x86_pkg_t  *pkg = x86_pkgs;
    x86_lcpu_t *cpu;
    x86_lcpu_t *cpus_reverse[P_CORE_MAX_COUNT * 2 + E_CORE_MAX_COUNT];
    int count = 0;

    p0_count = p1_count = e0_count = 0;

    cpu = pkg->lcpus;
    while (cpu != nullptr) {
        cpus_reverse[count++] = cpu;
        if (e_core_first == -1 || e_core_first - cpu->pnum == 2) {
            e_core_first = cpu->pnum;
        }
        cpu = cpu->next_in_pkg;
    }
    for (int i=0; i<count; ++i) {
        cpu = cpus_reverse[count - 1 - i];
        if (cpu->pnum < e_core_first) { // P-Core
            if (cpu->pnum % 2 == 0) { // primary
                p0_cpus[p0_count++] = cpu;
            } else { // HT core
                p1_cpus[p1_count++] = cpu;
            }
        } else { // E-Core
            e0_cpus[e0_count++] = cpu;
        }
    }

    if (p1_count == 0) {
        smt_spoof = true;
    }
}

static void rebuild_cpu_topology(void) {
    // do nothing if E-Cores disabled
    if (e0_count == 0) return;

    x86_pkg_t  *pkg = x86_pkgs;
    x86_die_t  *die = pkg->dies;
    x86_core_t *core;
    x86_lcpu_t *cpu;

    // i386_cpu_info_t *info = cpuid_info();
    x86_core_t *p_core_last = p0_cpus[p0_count - 1]->core;
    if (smt_spoof) {
        pkg->cores = die->cores = p_core_last;
        die->num_cores = p0_count;
        machine_info.physical_cpu_max = p0_count;
        // info->core_count = p0_count;
    } else {
        core = e0_cpus[0]->core;
        core->next_in_die = core->next_in_pkg = p_core_last;
        die->num_cores = p0_count + e0_count;
        machine_info.physical_cpu_max = p0_count + e0_count;
        // info->core_count = p0_count + e0_count;
    }

    for (int i=0; i<p0_count; ++i) {
        cpu = p0_cpus[i];
        cpu->lnum = 0;
        core = cpu->core;
        core->lcore_num = core->pcore_num = i;
        core->num_lcpus = 1;
        core->lcpus = cpu;
        if (i != 0) {
            core->next_in_pkg = core->next_in_die = p0_cpus[i-1]->core;
        }
    }
    for (int i=0; i<p1_count; ++i) {
        cpu = p1_cpus[i];
        core = p0_cpus[i]->core;
        cpu->lnum = core->num_lcpus++;
        cpu->core = core;
        cpu->next_in_core = core->lcpus;
        core->lcpus = cpu;
    }
    for (int i=0; i<e0_count; ++i) {
        cpu = e0_cpus[i];
        if (smt_spoof) {
            core = p0_cpus[i % p0_count]->core;
            cpu->core = core;
            cpu->lnum = core->num_lcpus++;
            cpu->next_in_core = core->lcpus;
            core->lcpus = cpu;
        } else {
            cpu->core->lcore_num = cpu->core->pcore_num = p0_count + i;
        }
    }

    rebuild_cache_topology();
}

void my_x86_validate_topology(void) {
    load_cpus();
    SYSLOG("ctr", "---- CPU topology before rebuild ----");
    print_cpu_topology2();
    print_cpu_topology();
    if (print_only) {
        return;
    }
    rebuild_cpu_topology();
    SYSLOG("ctr", "---- CPU topology after rebuild ----");
    print_cpu_topology();
    // FunctionCast(my_x86_validate_topology, org_x86_validate_topology)(); // skip topology validation
}

IOService *CpuTopologyRebuild::probe(IOService *provider, SInt32 *score) {
    if (!IOService::probe(provider, score)) return nullptr;
    if (!provider) return nullptr;

    OSNumber *cpuNumber = OSDynamicCast(OSNumber, provider->getProperty("processor-index"));
    if (!cpuNumber || cpuNumber->unsigned32BitValue() != 0) return nullptr;

    static bool done = false;
    if (!done) {
        lilu_get_boot_args("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
        ADDPR(debugEnabled) = checkKernelArgument("-ctrdbg") || checkKernelArgument("-liludbgall");
        print_only = checkKernelArgument("-ctrprt") || true;
        smt_spoof = checkKernelArgument("-ctrsmt");

        done = true;

        uint8_t patchData[6 + sizeof(uintptr_t)] = {0xFF, 0x25};

        void (*fn)(void) = my_x86_validate_topology;
        lilu_os_memcpy(&patchData[6], &fn, sizeof(fn));

        if (UNLIKELY(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)) {
            SYSLOG("ctr", "failed to obtain write permissions for f/r");
            return nullptr;
        }

        lilu_os_memcpy((void *)x86_validate_topology, patchData, sizeof(patchData));

        if (UNLIKELY(MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)) {
            SYSLOG("ctr", "failed to restore write permissions for f/r");
        }

        setProperty("VersionInfo", kextVersion);
        return this;
    }

    return nullptr;
}
