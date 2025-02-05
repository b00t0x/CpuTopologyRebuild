---
name: Bug report
about: Create a report
title: ''
labels: ''
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.

**Expected behavior**
A clear and concise description of what you expected to happen.

**Terminal output**
Output of the command below.
```
$ sysctl hw.cputhreadtype hw.physicalcpu_max hw.logicalcpu_max machdep.cpu.core_count machdep.cpu.thread_count
```

**Environment (please complete the following information):**
 - CPU: [e.g. i9-12900K]
 - macOS version: [e.g. macOS 15.3]
 - CpuTopologyRebuild version: [e.g. 2.0.0]
 - CpuTopologyRebuild options: [e.g. `ctrsmt=full`, `ctrfixcnt=1`]

**Checklist (you must complete these items):**
- [ ] I attached APIC.aml of my CPU. (see [dortania guide](https://dortania.github.io/Getting-Started-With-ACPI/Manual/dump.html))
- [ ] I attached Lilu debug log by following [this guide](https://github.com/b00t0x/CpuTopologyRebuild/wiki/How-to-get-debug-log).

**Additional context**
Add any other context about the problem here.
