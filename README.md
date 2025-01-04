# CpuTopologyRebuild
An Lilu plugin that optimizes Intel heterogeneous core configuration.

Currently these generations are supported.
* Alder Lake
* Raptor Lake
* Arrow Lake (experimental)

For example, this kext is possible to recognize the Core i9-12900K's topology as 16 cores 24 threads or 8 cores 24 threads.

### Background
OpenCore's `ProvideCurrentCpuInfo` quirk enables Alder Lake's both P-Cores and E-Cores, but all cores and threads are recognized as equivalent. This can cause potentially single thread or real world performance degradation.

This kext aims to improve performance by rebuild the topology for these cores and threads.

The effect of this kext is not yet clear, but I've seen not only a cosmetic effect, but also some performance improvements. For example, the Geekbench 5 score on the Windows VM have increased in the case of allocating a small number of cores to VMware Fusion.  
Other examples : [Japanese](https://github.com/b00t0x/CpuTopologyRebuild/wiki/%E3%83%91%E3%83%95%E3%82%A9%E3%83%BC%E3%83%9E%E3%83%B3%E3%82%B9%E3%81%AB%E9%96%A2%E3%81%99%E3%82%8B%E6%8E%A8%E5%AF%9F) / [English(Translated)](https://github-com.translate.goog/b00t0x/CpuTopologyRebuild/wiki/%E3%83%91%E3%83%95%E3%82%A9%E3%83%BC%E3%83%9E%E3%83%B3%E3%82%B9%E3%81%AB%E9%96%A2%E3%81%99%E3%82%8B%E6%8E%A8%E5%AF%9F?_x_tr_sl=ja&_x_tr_tl=en)

### Usage
* For Alder Lake and Raptor Lake CPU, use this kext with `ProvideCurrentCpuInfo` quirk.
  * Arrow Lake CPUs don't seem to require this quirk.

### Options
Both boot-args and NVRAM (`4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102`) variables can be used.
Performance effect is currenlty uncertain.
|NVRAM|boot-arg|Description|
|-----|--------|-----------|
|(none)|(none)|CPU topology will be spoofed as SMT to get better performance. E-Cores to be recognized as the N-way SMT logical threads of the P-Cores.<br>System Information show actual core count. (P+E)|
|`ctrsmt` : `"off"`  |`ctrsmt=off` |No SMT spoofing. CPU topology will be same as real structure, but performance will be degraded.<br>Only works with Alder/Raptor Lake CPUs with HT enabled.<br>Same as [v1.x.x](https://github.com/b00t0x/CpuTopologyRebuild/releases/tag/1.1.0) default behavior.|
|`ctrsmt` : `"full"` |`ctrsmt=full`|System Information shows # of P-Cores only. Performance effect is uncertain.<br>Same as [v1.x.x](https://github.com/b00t0x/CpuTopologyRebuild/releases/tag/1.1.0) with `-ctrsmt` behavior.|
|`ctrfixcnt` : `true`|`-ctrfixcnt` |Enable `machdep.cpu.core_count` fix. Performance effect is uncertain.<br>[AppleMCEReporterDisabler.kext](https://github.com/mikigal/ryzen-hackintosh/tree/master/OC/Kexts/AppleMCEReporterDisabler.kext) will be **required** to boot with this option.|

### Internal topology examples
|||Original|CpuTopologyRebuild<br>+ `ctrsmt=off`|CpuTopologyRebuild|
|-|:-|-:|-:|-:|
|Core i9-13900K   |8P+16E+HT|32c32t|24c32t|8c32t|
|Core i9-12900K   |8P+8E+HT |24c24t|16c24t|8c24t|
|                 |8P+8E    |16c16t| 8c16t|8c16t|
|Core i7-12700K   |8P+4E+HT |20c20t|12c20t|8c20t|
|                 |8P+4E    |12c12t| 8c12t|8c12t|
|Core i5-12600K   |6P+4E+HT |16c16t|10c16t|6c16t|
|                 |6P+4E    |10c10t| 6c10t|6c10t|
|Core Ultra 9 285K|8P+16E   |24c24t| 8c24t|8c24t|

### About patches.plist
#### patches_ht.plist
With `ProvideCurrentCpuInfo`, Hyper Threading is recognized as disabled due to that core and thread count are considered equal while initializing. [patches_ht.plist](patches_ht.plist) forces the kernel to recognize that Hyper Threading is enabled.  
(Performance effect is currenlty uncertain.)

#### patches_legacy.plist
[patches_legacy.plist](patches_legacy.plist) can be used instead of `ProvideCurrentCpuInfo` quirk. It is not needed normally, but ProvideCurrentCpuInfo doesn't work for High Sierra and earlier, so you can use this patch for older macOS.

### Current problems
* May cause random boot failure with verbose (`-v`) boot

### Credits
- [Apple](https://www.apple.com) for macOS
- [vit9696](https://github.com/vit9696) for original [CpuTopologySync](https://github.com/acidanthera/CpuTopologySync/tree/b2ce2619ea7e58ec4553ed3441aa03af6b771cdf)
- [bootmacos](https://bootmacos.com/) for confirmation about Raptor Lake (i9-13900KF) and Ventura
- [taruyato](https://github.com/taruyato) for confirmation 8P+4E configuration (i7-12700F) and Ventura ( #12 )
- [AnaCarolina1980](https://github.com/AnaCarolina1980) for testing Arrow Lake CPU ( #22 )
- [b00t0x](https://github.com/b00t0x) for writing the software and maintaining it
