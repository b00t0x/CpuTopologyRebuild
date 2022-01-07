#define ACIDANTHERA_PRIVATE
#define MSR_CORE_THREAD_COUNT 0x35

#include "CpuTopologyRebuild.h"
#include <i386/cpu_topology.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_version.hpp>

OSDefineMetaClassAndStructors(CpuTopologyRebuild, IOService)

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

bool smt_spoof = false;
x86_lcpu_t *p0_cpus[8]; // P-Cores
x86_lcpu_t *p1_cpus[8]; // P-Cores HT
x86_lcpu_t *e0_cpus[8]; // E-Cores
int p0_count = 0;
int p1_count = 0;
int e0_count = 0;

extern "C" void x86_validate_topology(void);

static void print_cache_info(x86_cpu_cache_t *cache) {
    x86_lcpu_t *cpu;

    const char *cache_name;
    if (cache->type == 1) {
        cache_name = "L1D";
    } else if (cache->type == 2) {
        cache_name = "L1I";
    } else if (cache->level == 2) {
        cache_name = "L2";
    } else {
        cache_name = "LLC";
    }

    char buf[5];
    char lcpus[256];
    lcpus[0] = '\0';
    for (int i=0; i<32; ++i) {
        cpu = cache->cpus[i];
        if (cpu == NULL) {
            break;
        }
        snprintf(buf, 5, "%d,", cpu->pnum);
        strlcat(lcpus, buf, 256);
    }

    DBGLOG("ctr", "  %s/type=%d/level=%d/%dKB/maxcpus=%d/nlcpus=%d/lcpus=%s",
        cache_name, cache->type, cache->level, cache->cache_size / 1024, cache->maxcpus, cache->nlcpus, lcpus);
}

static void print_cache_topology(void) {
    x86_pkg_t  *pkg = x86_pkgs;
    x86_lcpu_t *cpu;
    x86_cpu_cache_t *cache;
    x86_cpu_cache_t *caches[256];
    int cache_count = 0;

    DBGLOG("ctr", "Cache info:");
    for (int i=2; i>=0; --i) { // LLC->L2->L1
        cpu = pkg->lcpus;
        while (cpu != NULL) {
            cache = cpu->caches[i];
            bool new_cache = true;
            for (int j=0; j<cache_count; ++j) {
                if (cache == caches[j]) {
                    new_cache = false;
                    break;
                }
            }
            if (new_cache) {
                print_cache_info(cache);
                caches[cache_count++] = cache;
            }
            cpu = cpu->next_in_pkg;
        }
    }
}

static void print_cpu_topology(void) {
    x86_pkg_t  *pkg = x86_pkgs;
    x86_core_t *core;
    x86_lcpu_t *cpu;

    DBGLOG("ctr", "CPU info:");
    core = pkg->cores;
    while (core != NULL) {
        DBGLOG("ctr", "  Core(p/l): %d/%d (lcpus: %d)", core->pcore_num, core->lcore_num, core->num_lcpus);
        cpu = core->lcpus;
        while (cpu != NULL) {
            DBGLOG("ctr", "    LCPU(n/p/l): %2d/%2d/%d", cpu->cpu_num, cpu->pnum, cpu->lnum);
            cpu = cpu->next_in_core;
        }
        core = core->next_in_pkg;
    }
}

static void load_cpus(void) {
    x86_pkg_t  *pkg = x86_pkgs;
    x86_lcpu_t *cpu;
    x86_lcpu_t *cpus_reverse[24];
    int count = 0;

    cpu = pkg->lcpus;
    while (cpu != NULL) {
        cpus_reverse[count++] = cpu;
        cpu = cpu->next_in_pkg;
    }
    for (int i=0; i<count; ++i) {
        cpu = cpus_reverse[count - 1 - i];
        if (cpu->pnum < 64) { // P-Core
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

static void rebuild_cache_topology(void) {
    x86_lcpu_t *cpu;
    x86_cpu_cache_t *l1;
    x86_cpu_cache_t *l2;

    // E-Core fix
    x86_lcpu_t *e_primary;

    for (int i=0; i<(e0_count/4); ++i) {
        e_primary = e0_cpus[i*4];
        e_primary->caches[0]->cache_size = 64 * 1024; // 64KB
        l2 = e_primary->caches[1];
        l2->cache_size = 2 * 1024 * 1024; // 2MB
        for (int j=1; j<4; ++j) {
            cpu = e0_cpus[i*4+j];
            cpu->caches[0]->cache_size = 64 * 1024; // 64KB
            cpu->caches[1] = l2;
            l2->cpus[j] = cpu;
            l2->nlcpus++;
        }
    }

    // P-Core HTT fix
    x86_lcpu_t *p0;
    x86_lcpu_t *p1;

    for (int i=0; i<p1_count; ++i) {
        p0 = p0_cpus[i];
        p1 = p1_cpus[i];
        l1 = p0->caches[0];
        l2 = p0->caches[1];

        p1->caches[0] = l1;
        p1->caches[1] = l2;

        l1->nlcpus = 2;
        l2->nlcpus = 2;
        l1->cpus[1] = p1;
        l2->cpus[1] = p1;
    }
}

static void rebuild_cpu_topology(void) {
    // do nothing if E-Cores disabled
    if (e0_count == 0) {
        return;
    }

    x86_pkg_t  *pkg = x86_pkgs;
    x86_die_t  *die = pkg->dies;
    x86_core_t *core;
    x86_lcpu_t *cpu;

    x86_core_t *p0_last = p0_cpus[p0_count - 1]->core;
    if (smt_spoof) {
        pkg->cores = p0_last;
        die->cores = p0_last;
    } else if (p1_count != 0) {
        core = e0_cpus[0]->core;
        core->next_in_die = p0_last;
        core->next_in_pkg = p0_last;
    }

    for (int i=0; i<p0_count; ++i) {
        cpu = p0_cpus[i];
        cpu->lnum = 0;
        core = cpu->core;
        core->lcore_num = i;
        core->pcore_num = i;
        if (i != 0) {
            core->next_in_pkg = p0_cpus[i-1]->core;
            core->next_in_die = p0_cpus[i-1]->core;
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
            cpu->core->lcore_num = p0_count + i;
            cpu->core->pcore_num = p0_count + i;
        }
    }

    rebuild_cache_topology();
}

static uintptr_t org_x86_validate_topology;
void my_x86_validate_topology(void) {
    load_cpus();
    DBGLOG("ctr", "---- CPU/Cache topology before rebuild ----");
    print_cpu_topology();
    print_cache_topology();
    rebuild_cpu_topology();
    DBGLOG("ctr", "---- CPU/Cache topology after rebuild ----");
    print_cpu_topology();
    print_cache_topology();
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
        smt_spoof = checkKernelArgument("-ctrsmt");

        done = true;

        KernelPatcher &p = lilu.getKernelPatcher();
        KernelPatcher::RouteRequest request(
            "_x86_validate_topology",
            my_x86_validate_topology,
            org_x86_validate_topology
        );
        PANIC_COND(!p.routeMultiple(KernelPatcher::KernelID, &request, 1), "ctr", "failed to route _x86_validate_topology");

        setProperty("VersionInfo", kextVersion);
        return this;
    }

    return nullptr;
}
