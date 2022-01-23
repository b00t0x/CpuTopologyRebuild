# CpuTopologyRebuild
An **experimental** Lilu plugin that optimizes Alder Lake's heterogeneous core configuration.

For example, this kext is possible to recognize the Core i9-12900K's topology as 16 cores 24 threads or 8 cores 24 threads.

### Background
OpenCore's `ProvideCurrentCpuInfo` quirk enables Alder Lake's both P-Cores and E-Cores, but all cores and threads are recognized as equivalent. This can cause potentially single thread or real world performance degradation.

This kext aims to improve performance by rebuild the topology for these cores and threads.

The effect of this kext is not yet clear, but I've seen not only a cosmetic effect, but also some performance improvements. For example, the Geekbench 5 score on the Windows VM have increased in the case of allocating a small number of cores to VMware Fusion.  
Other examples : https://github.com/b00t0x/CpuTopologyRebuild/wiki/%E3%83%91%E3%83%95%E3%82%A9%E3%83%BC%E3%83%9E%E3%83%B3%E3%82%B9%E3%81%AB%E9%96%A2%E3%81%99%E3%82%8B%E6%8E%A8%E5%AF%9F (Japanese)

### Usage
* Use this kext with `ProvideCurrentCpuInfo` quirk.
* `-ctrsmt` boot arg makes E-Cores to be recognized as the 3-way SMT logical threads of the P-Cores. For example, it is recognized as 8 cores 24 threads in Core i9. This option seems to give the closest performance to E-Cores disabled configuration.
* When HT disabled, E-Cores are recognized as a logical thread of P-Cores with or without `-ctrsmt`. 

### Topology examples
|||ProvideCurrentCpuInfo|ProvideCurrentCpuInfo<br>+ CpuTopologyRebuild|ProvideCurrentCpuInfo<br>+ CpuTopologyRebuild<br>+ `-ctrsmt`|
|-|:-|-:|-:|-:|
|Core i9|8P+8E+HT|24c24t|16c24t|8c24t|
|       |8P+8E   |16c16t| 8c16t|8c16t|
|Core i7|8P+4E+HT|20c20t|12c20t|8c20t|
|       |8P+4E   |12c12t| 8c12t|8c12t|
|Core i5|6P+4E+HT|16c16t|10c16t|6c16t|
|       |6P+4E   |10c10t| 6c10t|6c10t|

### About patches.plist
patches.plist can be used instead of `ProvideCurrentCpuInfo` quirk. It is not needed normally, but ProvideCurrentCpuInfo doesn't work for High Sierra and earlier, so you can use this patch for older macOS.

### Current problems
* Changing the number of cores in cpuid info causes instability such as random boot failing, so it has not been changed now. Therefore, the number of cores and threads changes can be seen only in limited situations such as a Geekbench result window.
* For the same reason, RestrictEvents.kext's `revcpuname` does not work because the number of cores recognized by RestrictEvents.kext and About This Mac window are different. I added the [modded RestrictEvents.kext](https://github.com/b00t0x/CpuTopologyRebuild/releases/download/1.0.0/RestrictEvents-1.0.6-RELEASE.zip) with [a temporary fix](https://gist.github.com/b00t0x/a13cefd4644e9d6d57908ce3420c3002#file-restrictevents-cpp-diff) to solve the issue.
  * This issue expected to be [fixed](https://github.com/acidanthera/RestrictEvents/pull/6) in RestrictEvents.kext 1.0.7.
* Untested with Core i7-12xxx.

### Credits
- [Apple](https://www.apple.com) for macOS
- [vit9696](https://github.com/vit9696) for original [CpuTopologySync](https://github.com/acidanthera/CpuTopologySync/tree/b2ce2619ea7e58ec4553ed3441aa03af6b771cdf)
- [b00t0x](https://github.com/b00t0x) for writing the software and maintaining it
