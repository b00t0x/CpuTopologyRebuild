#define P_CORE_MAX_COUNT 32
#define E_CORE_MAX_COUNT 64

#define APIC_MAX 256
#define APIC_ID_UNIT 8

#include "CpuTopologyRebuild.h"
#include <i386/cpu_topology.h>
#include <i386/cpuid.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_efi.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_version.hpp>

OSDefineMetaClassAndStructors(CpuTopologyRebuild, IOService)

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

bool print_only = false;
bool smt_spoof = true;
bool hide_e_core = false;
bool fix_core_count = false;

x86_lcpu_t *p0_cpus[P_CORE_MAX_COUNT]; // P-Cores
x86_lcpu_t *p1_cpus[P_CORE_MAX_COUNT]; // P-Cores HT
x86_lcpu_t *e0_cpus[E_CORE_MAX_COUNT]; // E-Cores
int p0_count, p1_count, e0_count = 0;

extern "C" void x86_validate_topology(void);
extern x86_topology_parameters_t topoParms;

static void print_topo_parms(void) {
    DBGLOG("ctr", "topoParms:");
    DBGLOG("ctr", "  nPackages           = %3d", topoParms.nPackages);
    DBGLOG("ctr", "  nPDiesPerPackage    = %3d | nLDiesPerPackage    = %3d", topoParms.nPDiesPerPackage, topoParms.nLDiesPerPackage);
    DBGLOG("ctr", "  nPCoresPerPackage   = %3d | nLCoresPerPackage   = %3d", topoParms.nPCoresPerPackage, topoParms.nLCoresPerPackage);
    DBGLOG("ctr", "  nPCoresPerDie       = %3d | nLCoresPerDie       = %3d", topoParms.nPCoresPerDie, topoParms.nLCoresPerDie);
    DBGLOG("ctr", "  nPThreadsPerPackage = %3d | nLThreadsPerPackage = %3d", topoParms.nPThreadsPerPackage, topoParms.nLThreadsPerPackage);
    DBGLOG("ctr", "  nPThreadsPerDie     = %3d | nLThreadsPerDie     = %3d", topoParms.nPThreadsPerDie, topoParms.nLThreadsPerDie);
    DBGLOG("ctr", "  nPThreadsPerCore    = %3d | nLThreadsPerCore    = %3d", topoParms.nPThreadsPerCore, topoParms.nLThreadsPerCore);
}

static void print_lcpu_topology(x86_lcpu_t *lcpu) {
    DBGLOG("ctr", "      lcpu(%p): pnum=%2d, lnum=%2d, cpu_num=%2d, primary=%d, master=%d",
        lcpu, lcpu->pnum, lcpu->lnum, lcpu->cpu_num, lcpu->primary, lcpu->master);
}

static void print_core_topology(x86_core_t *core) {
    DBGLOG("ctr", "    Core(%p): pcore_num=%2d, lcore_num=%2d, num_lcpus=%d",
        core, core->pcore_num, core->lcore_num, core->num_lcpus);
}

static void print_die_topology(x86_die_t *die) {
    DBGLOG("ctr", "  Die(%p): pdie_num=%d, ldie_num=%d, num_cores=%2d",
        die, die->pdie_num, die->ldie_num, die->num_cores);
}

static void print_pkg_topology(x86_pkg_t *pkg) {
    DBGLOG("ctr", "Pkg(%p): ppkg_num=%d, lpkg_num=%d, num_dies=%d",
        pkg, pkg->ppkg_num, pkg->lpkg_num, pkg->num_dies);
}

static void print_cpu_topology(void) {
    x86_pkg_t  *pkg;
    x86_die_t  *die;
    x86_core_t *core;
    x86_lcpu_t *lcpu;

    i386_cpu_info_t *info = cpuid_info();
    DBGLOG("ctr", "CPU info:");
    DBGLOG("ctr", "  physical_cpu_max = %2d | logical_cpu_max = %2d", machine_info.physical_cpu_max, machine_info.logical_cpu_max);
    DBGLOG("ctr", "  core_count       = %2d | thread_count    = %2d", info->core_count, info->thread_count);
    DBGLOG("ctr", "Pkg->Die->Core->lcpu chain:");
    pkg = x86_pkgs;
    while (pkg != nullptr) {
        print_pkg_topology(pkg);
        die = pkg->dies;
        while (die != nullptr) {
            print_die_topology(die);
            core = die->cores;
            while (core != nullptr) {
                print_core_topology(core);
                lcpu = core->lcpus;
                while (lcpu != nullptr) {
                    print_lcpu_topology(lcpu);
                    lcpu = lcpu->next_in_core;
                }
                core = core->next_in_die;
            }
            die = die->next_in_pkg;
        }
        pkg = pkg->next;
    }
    DBGLOG("ctr", "Pkg->Die->lcpu chain:");
    pkg = x86_pkgs;
    while (pkg != nullptr) {
        print_pkg_topology(pkg);
        die = pkg->dies;
        while (die != nullptr) {
            print_die_topology(die);
            lcpu = die->lcpus;
            while (lcpu != nullptr) {
                print_lcpu_topology(lcpu);
                lcpu = lcpu->next_in_die;
            }
            die = die->next_in_pkg;
        }
        pkg = pkg->next;
    }
    DBGLOG("ctr", "Pkg->Core chain:");
    pkg = x86_pkgs;
    while (pkg != nullptr) {
        print_pkg_topology(pkg);
        core = pkg->cores;
        while (core != nullptr) {
            print_core_topology(core);
            core = core->next_in_pkg;
        }
        pkg = pkg->next;
    }
    DBGLOG("ctr", "Pkg->lcpu chain:");
    pkg = x86_pkgs;
    while (pkg != nullptr) {
        print_pkg_topology(pkg);
        lcpu = pkg->lcpus;
        while (lcpu != nullptr) {
            print_lcpu_topology(lcpu);
            lcpu = lcpu->next_in_pkg;
        }
        pkg = pkg->next;
    }
}

static bool load_cpus(void) {
    x86_lcpu_t *lcpu;
    x86_lcpu_t *lcpus_by_apic[APIC_MAX];

    // load lcpus by apic id order
    lcpu = x86_pkgs->lcpus;
    while (lcpu != nullptr) {
        lcpus_by_apic[lcpu->pnum] = lcpu;
        lcpu = lcpu->next_in_pkg;
    }
    // determine P/E-core
    for (int i = 0; i < APIC_MAX / APIC_ID_UNIT; ++i) {
        if (lcpus_by_apic[i * APIC_ID_UNIT] == nullptr) {
            continue;
        }
        bool is_e_cores = lcpus_by_apic[i * APIC_ID_UNIT + 2] != nullptr;
        for (int j = 0; j < APIC_ID_UNIT; ++j) {
            lcpu = lcpus_by_apic[i * APIC_ID_UNIT + j];
            if (lcpu == nullptr) {
                continue;
            } else if (is_e_cores) {
                DBGLOG("ctr", "ApicID %02d -> E-Core", lcpu->pnum);
                e0_cpus[e0_count++] = lcpu;
            } else if (lcpu->pnum % 2 == 0) {
                DBGLOG("ctr", "ApicID %02d -> P-Core", lcpu->pnum);
                p0_cpus[p0_count++] = lcpu;
            } else {
                DBGLOG("ctr", "ApicID %02d -> P-Core(HT)", lcpu->pnum);
                p1_cpus[p1_count++] = lcpu;
            }
        }
    }

    SYSLOG("ctr", "%d P-Cores + %d E-Cores", p0_count, e0_count);
    if (p1_count == 0) {
        smt_spoof = true;
    } else if (p0_count != p1_count) {
        SYSLOG("ctr", "abort: P-Core count(%d) and P-Core logical thread count(%d+%d) do not match!", p0_count, p0_count, p1_count);
        return false;
    }

    return true;
}

static void rebuild_cpu_topology(void) {
    x86_pkg_t  *pkg = x86_pkgs;
    x86_die_t  *die = pkg->dies;
    x86_core_t *core;
    x86_lcpu_t *lcpu;

    if (!load_cpus()) {
        return;
    }

    // Rebuild P-Core chain (required for Arrow Lake?)
    for (int i = 1; i < p0_count; ++i) {
        core = p0_cpus[i]->core;
        core->lcore_num = core->pcore_num = i;
        core->next_in_die = core->next_in_pkg = p0_cpus[i - 1]->core;
    }

    // Rebuild P-Cores HT
    for (int i = 0; i < p1_count; ++i) {
        lcpu = p1_cpus[i];
        core = p0_cpus[i]->core;

        // Merge secondary thread into primary
        lcpu->core = core;
        lcpu->lnum = core->num_lcpus++;
        lcpu->next_in_core = lcpu->next_in_die = lcpu->next_in_pkg = core->lcpus;
        core->lcpus = lcpu;
    }

    // Rebuild E-Cores
    if (smt_spoof) {
        int e_core_per_core = e0_count / p0_count;
        int e_core_mod = e0_count % p0_count;

        // Merge E-Core into P-Core
        int idx = 0;
        for (int i = 0; i < p0_count; ++i) {
            core = p0_cpus[i]->core;
            int e_core_count = i < e_core_mod ? e_core_per_core + 1 : e_core_per_core;
            for (int j = 0; j < e_core_count; ++j) {
                lcpu = e0_cpus[idx++];
                lcpu->core = core;
                lcpu->lnum = core->num_lcpus++;
                lcpu->next_in_core = lcpu->next_in_die = lcpu->next_in_pkg = core->lcpus;
                core->lcpus = lcpu;
            }
        }
    } else {
        for (int i = 0; i < e0_count; ++i) {
            // Rebuild core order
            core = e0_cpus[i]->core;
            core->lcore_num = core->pcore_num = p0_count + i;
            if (i == 0) {
                core->next_in_die = core->next_in_pkg = p0_cpus[p0_count - 1]->core;
            } else {
                core->next_in_die = core->next_in_pkg = e0_cpus[i - 1]->core;
            }
        }
    }

    // Rebuild lcpu order
    core = smt_spoof ? p0_cpus[p0_count - 1]->core : e0_cpus[e0_count - 1]->core;
    pkg->cores = die->cores = core;
    pkg->lcpus = die->lcpus = core->lcpus;
    while (core->lcore_num != 0) {
        lcpu = core->lcpus;
        while (lcpu->lnum != 0) {
            lcpu = lcpu->next_in_core;
        }
        core = core->next_in_pkg;
        lcpu->next_in_die = lcpu->next_in_pkg = core->lcpus;
    }

    // fix core count info
    die->num_cores = die->cores->lcore_num + 1;
    machine_info.physical_cpu_max = hide_e_core ? die->num_cores : p0_count + e0_count;
    if (fix_core_count) {
        cpuid_info()->core_count = machine_info.physical_cpu_max;
    }
}

// used for debugging
static void count_override() {
    int physical_cpu_max, core_count = 0;
    PE_parse_boot_argn("ctrphys", &physical_cpu_max, sizeof(physical_cpu_max));
    PE_parse_boot_argn("ctrccnt", &core_count, sizeof(core_count));
    if (physical_cpu_max) {
        machine_info.physical_cpu_max = physical_cpu_max;
    }
    if (core_count) {
        cpuid_info()->core_count = core_count;
    }
}

void my_x86_validate_topology(void) {
    print_topo_parms();
    DBGLOG("ctr", "---- CPU topology before rebuild ----");
    print_cpu_topology();
    if (print_only) {
        return;
    }
    rebuild_cpu_topology();
    count_override();
    DBGLOG("ctr", "---- CPU topology after rebuild ----");
    print_cpu_topology();
    // FunctionCast(my_x86_validate_topology, org_x86_validate_topology)(); // skip topology validation
}

static void load_params(void) {
    char ctrsmt[128] { "default" };

    auto rt = EfiRuntimeServices::get(true);
    if (rt) {
        uint32_t attr;
        uint64_t size;

        size = sizeof(ctrsmt);
        rt->getVariable(u"ctrsmt", &EfiRuntimeServices::LiluVendorGuid, &attr, &size, ctrsmt);
        DBGLOG("ctr", "NVRAM     | ctrsmt    | %s", ctrsmt);

        size = sizeof(fix_core_count);
        rt->getVariable(u"ctrfixcnt", &EfiRuntimeServices::LiluVendorGuid, &attr, &size, &fix_core_count);
        DBGLOG("ctr", "NVRAM     | ctrfixcnt | %d", fix_core_count);

        rt->put();
    } else {
        SYSLOG("ctr", "failed to get EfiRuntimeServices");
    }
    PE_parse_boot_argn("ctrsmt", ctrsmt, sizeof(ctrsmt));
    DBGLOG("ctr", "boot-args | ctrsmt    | %s", ctrsmt);

    if (strstr(ctrsmt, "full") || checkKernelArgument("-ctrsmt")) { // -ctrsmt is old argument
        DBGLOG("ctr", "type | smt_spoof + hide_e_core");
        smt_spoof = true;
        hide_e_core = true;
    } else if (strstr(ctrsmt, "off")) {
        DBGLOG("ctr", "type | none");
        smt_spoof = false;
        hide_e_core = false;
    } else {
        DBGLOG("ctr", "type      | smt_spoof");
    }
    if (checkKernelArgument("-ctrfixcnt")) {
        DBGLOG("ctr", "boot-args | ctrfixcnt | 1");
        fix_core_count = true;
    } else {
        DBGLOG("ctr", "boot-args | ctrfixcnt | 0");
    }
    DBGLOG("ctr", "configuration : smt_spoof = %d, hide_e_core = %d, fix_core_count = %d", smt_spoof, hide_e_core, fix_core_count);

    print_only = checkKernelArgument("-ctrprt");
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
        load_params();

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
