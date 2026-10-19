<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# OAI 7.2 Fronthaul Interface Tutorial

**Table of Contents**

[[_TOC_]]

## Prerequisites

Check the [supported hardware operating system guide](./Supported_Hardware_Operating_System.md)
to know if you have the appropriate hardware for your testing requirements.
This tutorial has been tested with following OS and kernels feel free to
install one of the tested operating systems.

|Operating System      | Kernel    |
|----------------------|-----------|
|Ubuntu 22.04/24.04    | 6.8/6.14  |
|Red Hat 9.X           | 5.14      |

**NOTE**:

- Compute Hardware:
  - For 4x4 100MHz 4L DL and 2L UL, it is important to have a clock speed
    higher than 4GHz or use accelerators as AMD T2, Intel V-RAN Boost or Nvidia
    Aerial L1.
  - Minimum functional hardware requires clock speed of minimum 2.5GHz, 10 SFP
    NIC
  - Recommended servers for 8/9 bit compression:
    - Intel: 3rd Generation (Ice Lake) or higher
    - AMD: 4th Generation (Genoa) or higher
    - ARM: Neoverse V2
  - Uncompressed mode does not require AVX512
- The NIC card should support hardware PTP time stamping.
- With AMD servers/desktop machines with PCIe 5.0 we have only used E810 cards.
- If you are using Mellanox NIC, please be aware that DPDK can't bind the NIC
  as `vfio-pci`. Instead it must be bind with `mlx` driver.

PTP enabled switches and Grandmaster clock we have tested with:

|Vendor                  |
|------------------------|
|CISCO C93180YC-FX3      |
|Fibrolan Falcon-RX/812/G|
|Qulsar Qg2 (Grandmaster)|

**S-Plane synchronization is mandatory.** S-plane support is done via 
`ptp4l` and `phc2sys`. Make sure your version matches. 

| Software  | Software Version|
|-----------|-----------------|
| `ptp4l`   | 4.4             |
| `phc2sys` | 4.4             |

In our Lab we only use LLS-C3 configuration, i.e. using an external
Grandmaster, a switch as a boundary clock, and the gNB/DU and RU. We have not
tested any RU without S-plane. Though some community members use LLS-C1 and its
support depends on the NIC.

We tested the category A radio units listed below.

|Vendor                 |Recommended SW version  |
|-----------------------|------------------------|
|VVDN LPRU              |03-v3.0.5               |
|LiteON RU FR1          |02.00.10                |
|LiteON RU FR2          |02.00.07                |
|Metanoia RU FR1 (Jura) |2.0.6                   |
|Metanoia RU FR1 (Cobra)|1.0.1                   |
|Metanoia RU FR2 (Cobra)|1.0.1                   |
|Benetel 650            |RAN650-1v2.1.0-M-0820797|
|Benetel 550            |RAN550-1v2.1.0-M-0820797|
|Foxconn RPQN           |v3.1.15q.551_rc10       |
|Microamp RU (FR2)      |0.1.174                 |

Supported libxran releases:

| Vendor                                  |
|-----------------------------------------|
| `oran_k_release_v1.0`                   |

**Note**: The libxran driver of OAI identifies the above K release as "11.1.0".
E/F releases not supported starting from tags `2026.w08`/`2026.w29`, respectively.

### Configure your server

1. Disable Hyperthreading (HT) in your BIOS for ease of CPU pining. If you want
   to enable it then adjust the boot line command to isolate the whole physical
   CPU including the threads.
2. Configure the system with in performance mode. In some situation it will be
   better to avoid BIOS telco profile as it uses HT and lowers the CPU clock
   speed.
3. We recommend you to start with a fresh installation of OS (Operating
   System), either RPM or Debian.
4. Change the boot commands based on the below section.

##### x86 (Intel/AMD)

We are taking an example of a 32 core Intel(R) Xeon(R) Gold 6433N, depending on
your system you would need to adjust the isolated and non-isolated cores.
Though we don't recommend hyper-threading but if you want to use then make sure
you isolate the whole physical CPU.

1. Install realtime kernel,

Ubuntu (to know more about the below commands please check Canonical's official
website):

```bash
sudo apt install ubuntu-realtime
# using ubuntu pro if you have the subscription
pro enable realtime-kernel
```

Red Hat (to know more about the below commands please check Red Hat's official
website):

```bash
subscription-manager repos --enable rhel-10-for-x86_64-rt-rpms
dnf groupinstall RT
```

For other operating systems you would need to either build the kernel from
source or search if you can install it using a package manager.

2. Update the Grub via creating a file `/etc/default/grub.d/cmdline.cfg`.

```bash
GRUB_CMDLINE_LINUX="$GRUB_CMDLINE_LINUX isolcpus=domain,4-31 nohz_full=4-31 irqaffinity=0,1,2,3 rcu_nocbs=4-31 hugepagesz=2M hugepages=0 default_hugepagesz=1G hugepagesz=1G hugepages=20 intel_iommu=on iommu=pt selinux=0 enforcing=0 intel_pstate=disable"
```

In the above command:

1. Isolated CPUs: 4-31, we recommend to leave the initial CPUs for the kernel
   - `isolcpus=domain,4-31`: removes CPUs 4–31 from normal scheduler
     load-balancing domains
   - `nohz_full=4-31` enables full tickless operation on those CPUs
   - `rcu_nocbs=4-31` moves RCU callback processing off those CPUs
2. Non-isolated CPUs: 0,1,2,3
   - `irqaffinity=0,1,2,3` directs default interrupt handling to CPUs 0–3 so
     the isolated CPUs are disturbed less.
3. Hugepages: The default hugepage size is 1G (`default_hudepagesz=1G`) and the
   command above reserves 20G (`hugepages=20`). If you want to reduce then you
   can put `hugepages=9` as OAI gNB/DU by default initializes with 8G
   hugepages.
4. IOMMU: It is needed for DPDK, for Intel servers its `intel_iommu` but AMD it
   is `amd_iommu`
5. P-State: For Intel the Pstate driver is `intel_pstate` and for AMD it is
   `amd_pstate`

After changing you would need to rebuild the grub and reboot the system:

```bash
sudo update-grub
sudo reboot
```

3. Configure your server to maximum performance mode either via OS or in BIOS.
If you want to disable CPU sleep states via OS then use the below command:

```bash
# Disable cpu cstates
sudo cpupower idle-set -D 0
# you can make a crontab to automate this step at restart
```

##### Aarch64

We are taking an example from a Gracehopper GH200 Neoverse-V2

1. Install nvidia-64k pagesize kernel using `apt/dnf`

2. Update the Grub via creating a file `/etc/default/grub.d/cmdline.cfg`.

```bash
GRUB_CMDLINE_LINUX="$GRUB_CMDLINE_LINUXpci=realloc=off pci=pcie_bus_safe default_hugepagesz=512M hugepagesz=512M hugepages=256 tsc=reliable processor.max_cstate=0 audit=0 idle=poll rcu_nocb_poll nosoftlockup irqaffinity=0 isolcpus=managed_irq,domain,4-71 nohz_full=4-71 rcu_nocbs=4-71 earlycon module_blacklist=nouveau acpi_power_meter.force_cap_on=y numa_balancing=disable init_on_alloc=0 preempt=none"
```

In the above command (the explanation of the parameters is same as x86):

1. Isolated CPUs: 4-71, we recommend to leave the initial CPUs for the kernel
2. Non-isolated CPUs: 0,1,2,3
3. Hugepages: The default hugepage size is 512M (`default_hudepagesz=512M`) and the command above reserves 131072M (`hugepages=256`).

After changing you would need to rebuild the grub and reboot the system:

```bash
sudo update-grub
sudo reboot
```

You can adapt the above command for a DGX spark.

##### Common for both Architectures

1. Configuration for realtime scheduling:

```bash
# create a file in sysctl.d
vi /etc/sysctl.d/rt.conf
# disables the RT throttling limit, allowing real-time tasks to use unlimited CPU time
kernel.sched_rt_runtime_us=-1
# prevents timers from being migrated between CPUs, reducing jitter on isolated/real-time cores.
kernel.timer_migration=0
```

2. If you are using systemd you can configure `CPUAffinity` in `/etc/systemd/
system.conf` to non-isolated CPUs. It helps if you are having issues with SSH
or tcpdump when OAI-gNB/DU is running.

After applying both the steps you can reboot the system.

### PTP configuration

**Note**: You may run OAI with O-RAN 7.2 Fronthaul without a RU attached (e.g.
for benchmarking).
In such case, you can skip PTP configuration and go to DPDK section.

1. You can install `linuxptp` rpm or debian package. It will install ptp4l and phc2sys.

```bash
# Sometimes in the package repository the PTP version is old, so its better to compile from source
git clone https://github.com/richardcochran/linuxptp.git && cd linuxptp
git checkout v4.4
make
cp ptp4l phc2sys /usr/sbin/
```

Once installed you can use this configuration file for ptp4l (`/etc/ptp4l.conf`). Here the clock domain is 24 so you can adjust it according to your PTP GM clock domain

```
[global]
domainNumber            24
clientOnly              1
time_stamping           hardware
tx_timestamp_timeout    50
logging_level           6
summary_interval        0
#priority1               127

[PTP_ENABLED_NIC_INTERFACE]
network_transport       L2
hybrid_e2e              0
```

You need to increase `tx_timestamp_timeout` to 100 if needed. You will see that in the logs of ptp.

Create the configuration file for ptp4l (`/etc/sysconfig/ptp4l`)

```
OPTIONS="-f /etc/ptp4l.conf"
```

Create the configuration file for phc2sys (`/etc/sysconfig/phc2sys`)

```
OPTIONS="-s PTP_ENABLED_NIC_INTERFACE -w -n 24 -r -r -m -R 8"
```

The service of ptp4l (`/usr/lib/systemd/system/ptp4l.service`) should be configured as below:

```
[Unit]
Description=Precision Time Protocol (PTP) service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
EnvironmentFile=-/etc/sysconfig/ptp4l
ExecStart=/usr/sbin/ptp4l $OPTIONS

[Install]
WantedBy=multi-user.target
```

and service of phc2sys (`/usr/lib/systemd/system/phc2sys.service`) should be configured as below:

```
[Unit]
Description=Synchronize system clock or PTP hardware clock (PHC)
After=ntpdate.service ptp4l.service

[Service]
Type=simple
EnvironmentFile=-/etc/sysconfig/phc2sys
ExecStart=/usr/sbin/phc2sys $OPTIONS

[Install]
WantedBy=multi-user.target
```

#### Debugging PTP issues

You can see these steps in case your ptp logs have erorrs or `rms` reported in `ptp4l` logs is more than 100ms.
Beware that PTP issues may show up only when running OAI and XRAN. If you are using the `ptp4l` service, have a look back in time in the journal: `journalctl -u ptp4l.service -S <hours>:<minutes>:<seconds>`

1. Make sure that you have `skew_tick=1` in `/proc/cmdline`
2. For Intel E-810 cards set `tx_timestamp_timeout` to 50 or 100 if there are errors in ptp4l logs
3. Other time sources than PTP, such as NTP or chrony timesources, should be disabled. Make sure they are enabled as further below.
4. If `rms` or `delay` in `ptp4l` or `offset` in `phc2sys` logs remain high then you can try pinning the `ptp4l` and `phc2sys` processes to an isolated CPU.

```bash
#to check there is NTP enabled or not
timedatectl | grep NTP
#to disable
timedatectl set-ntp false
```

### DPDK (Data Plane Development Kit)

> [!NOTE]
> DPDK version 20.11 or superior is required  
> We recommend using DPDK 22.11.11 while possible

Download DPDK version 22.11.11 (K release).

```bash
# on debian
sudo apt install wget xz-utils libnuma-dev libibverbs-dev rdma-core python3-pyelftools meson
# on Fedora/RHEL
sudo dnf install wget xz numactl-devel rdma-core-devel libibverbs-devel python3-pyelftools meson
cd
wget http://fast.dpdk.org/rel/dpdk-22.11.11.tar.xz # K release
```

#### DPDK Compilation and Installation

```bash
tar xvf dpdk-22.11.11.tar.xz && cd dpdk-stable-22.11.11 # K release

meson build
ninja -C build
sudo ninja install -C build
```

#### Verify the installation is complete

Check if the LD cache contains the DPDK Shared Objects after update:

```bash
# output for DPDK 20.11.9
sudo ldconfig -v | grep rte_
	librte_fib.so.0.200.2 -> librte_fib.so.0.200.2
	librte_telemetry.so.0.200.2 -> librte_telemetry.so.0.200.2
	librte_compressdev.so.0.200.2 -> librte_compressdev.so.0.200.2
	librte_gro.so.20.0 -> librte_gro.so.20.0.2
	librte_mempool_dpaa.so.20.0 -> librte_mempool_dpaa.so.20.0.2
	librte_distributor.so.20.0 -> librte_distributor.so.20.0.2
	librte_rawdev_dpaa2_cmdif.so.20.0 -> librte_rawdev_dpaa2_cmdif.so.20.0.2
	librte_mempool.so.20.0 -> librte_mempool.so.20.0.2
	librte_pmd_octeontx2_crypto.so.20.0 -> librte_pmd_octeontx2_crypto.so.20.0.2
	librte_common_cpt.so.20.0 -> librte_common_cpt.so.20.0.2
....
```

You may not have the `/usr/local/lib`, `/usr/local/lib64`, or custom DPDK
installation paths in the `LD_LIBRARY_PATH`. In this case, add it as below; if
you installed into a
custom

```bash
sudo echo "/usr/local/lib" > /etc/ld.so.conf.d/local-lib.conf
sudo echo "/usr/local/lib64" >> /etc/ld.so.conf.d/local-lib.conf
sudo ldconfig
sudo ldconfig -v | grep rte_
```

Check if the PKG-CONFIG tool discovers the libraries:

```bash
pkg-config --libs libdpdk --static
```

<details>
<summary>Possible output</summary>

```console
-lrte_node -lrte_graph -lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_rcu -lrte_ring -lrte_eal -lrte_telemetry -lrte_kvargs -Wl,--whole-archive -lrte_common_cpt -lrte_common_dpaax -lrte_common_iavf -lrte_common_octeontx -lrte_common_octeontx2 -lrte_bus_dpaa -lrte_bus_fslmc -lrte_bus_ifpga -lrte_bus_pci -lrte_bus_vdev -lrte_bus_vmbus -lrte_mempool_bucket -lrte_mempool_dpaa -lrte_mempool_dpaa2 -lrte_mempool_octeontx -lrte_mempool_octeontx2 -lrte_mempool_ring -lrte_mempool_stack -lrte_pmd_af_packet -lrte_pmd_ark -lrte_pmd_atlantic -lrte_pmd_avp -lrte_pmd_axgbe -lrte_pmd_bond -lrte_pmd_bnxt -lrte_pmd_cxgbe -lrte_pmd_dpaa -lrte_pmd_dpaa2 -lrte_pmd_e1000 -lrte_pmd_ena -lrte_pmd_enetc -lrte_pmd_enic -lrte_pmd_failsafe -lrte_pmd_fm10k -lrte_pmd_i40e -lrte_pmd_hinic -lrte_pmd_hns3 -lrte_pmd_iavf -lrte_pmd_ice -lrte_pmd_igc -lrte_pmd_ixgbe -lrte_pmd_kni -lrte_pmd_liquidio -lrte_pmd_memif -lrte_pmd_netvsc -lrte_pmd_nfp -lrte_pmd_null -lrte_pmd_octeontx -lrte_pmd_octeontx2 -lrte_pmd_pfe -lrte_pmd_qede -lrte_pmd_ring -lrte_pmd_sfc -lrte_pmd_softnic -lrte_pmd_tap -lrte_pmd_thunderx -lrte_pmd_vdev_netvsc -lrte_pmd_vhost -lrte_pmd_virtio -lrte_pmd_vmxnet3 -lrte_rawdev_dpaa2_cmdif -lrte_rawdev_dpaa2_qdma -lrte_rawdev_ioat -lrte_rawdev_ntb -lrte_rawdev_octeontx2_dma -lrte_rawdev_octeontx2_ep -lrte_rawdev_skeleton -lrte_pmd_caam_jr -lrte_pmd_dpaa_sec -lrte_pmd_dpaa2_sec -lrte_pmd_nitrox -lrte_pmd_null_crypto -lrte_pmd_octeontx_crypto -lrte_pmd_octeontx2_crypto -lrte_pmd_crypto_scheduler -lrte_pmd_virtio_crypto -lrte_pmd_octeontx_compress -lrte_pmd_qat -lrte_pmd_ifc -lrte_pmd_dpaa_event -lrte_pmd_dpaa2_event -lrte_pmd_octeontx2_event -lrte_pmd_opdl_event -lrte_pmd_skeleton_event -lrte_pmd_sw_event -lrte_pmd_dsw_event -lrte_pmd_octeontx_event -lrte_pmd_bbdev_null -lrte_pmd_bbdev_turbo_sw -lrte_pmd_bbdev_fpga_lte_fec -lrte_pmd_bbdev_fpga_5gnr_fec -Wl,--no-whole-archive -lrte_node -lrte_graph -lrte_bpf -lrte_flow_classify -lrte_pipeline -lrte_table -lrte_port -lrte_fib -lrte_ipsec -lrte_vhost -lrte_stack -lrte_security -lrte_sched -lrte_reorder -lrte_rib -lrte_rawdev -lrte_pdump -lrte_power -lrte_member -lrte_lpm -lrte_latencystats -lrte_kni -lrte_jobstats -lrte_ip_frag -lrte_gso -lrte_gro -lrte_eventdev -lrte_efd -lrte_distributor -lrte_cryptodev -lrte_compressdev -lrte_cfgfile -lrte_bitratestats -lrte_bbdev -lrte_acl -lrte_timer -lrte_hash -lrte_metrics -lrte_cmdline -lrte_pci -lrte_ethdev -lrte_meter -lrte_net -lrte_mbuf -lrte_mempool -lrte_rcu -lrte_ring -lrte_eal -lrte_telemetry -lrte_kvargs -Wl,-Bdynamic -pthread -lm -ldl
```
</details>

If DPDK was installed into `/usr/local/lib`, `/usr/local/lib64`, or another
custom path, you have to point to the right directory with `PKG_CONFIG_PATH`,
for instance:

```bash
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib64/pkgconfig/
pkg-config --libs libdpdk --static
```

#### If you want to de-install this version of DPDK

Go back to the version folder you used to build and install

```
cd ~/dpdk-stable-22.11.11 # K release
sudo ninja deinstall -C build
```

## Build OAI-FHI gNB

Clone OAI code base in a suitable repository, here we are cloning in `~/openairinterface5g` directory,

```bash
git clone https://github.com/duranta-project/openairinterface5g.git ~/openairinterface5g
cd ~/openairinterface5g/
```

### Build ORAN Fronthaul Interface Library

#### K release
```bash
git clone https://github.com/openairinterface/o-du-phy.git ~/phy
cd ~/phy
git checkout 11.1.8 # the tag points to the `main` branch which has all patches applied that are relevant for OAI integration; the tag matches the value of cmake variable `K_VERSION`
```
or use `xran_DOWNLOAD` option when compiling OAI gNB.

Compile the fronthaul interface library by calling `make` and the option
`XRAN_LIB_SO=1` to have it build a shared object. Note that we provide two
environment variables `RTE_SDK` for the path to the source tree of DPDK, and
`XRAN_DIR` to set the path to the fronthaul library.

**Note**: you need at least gcc-11 and g++-11.

```bash
cd ~/phy/fhi_lib/lib
make clean
WIRELESS_SDK_TOOLCHAIN=gcc RTE_SDK=~/dpdk-stable-22.11.11/ XRAN_DIR=~/phy/fhi_lib make XRAN_LIB_SO=1 # K release
...
[AR] build/libxran.so
./build/libxran.so
```

The shared library object `~/phy/fhi_lib/lib/build/libxran.so` must be present
before proceeding.

### For Arm targets only: Install the Arm RAN Acceleration library

DU execution on Arm systems is yet not functional.
This feature is intended to enable experiments and future improvements on Arm systems.

Clone, configure and build ArmRAL:

Note: Use option `-DCMAKE_INSTALL_PREFIX=<install-dir>` of cmake to set the installation directory of ArmRAL to `<install-dir>`.
If you omit this option, ArmRAL is installed into `/usr/local`.

```
git clone https://git.gitlab.arm.com/networking/ral.git ~/ral
cd ~/ral
git checkout armral-25.01
mkdir build
cd build
cmake -GNinja -DBUILD_SHARED_LIBS=On ../
ninja
```

Once ArmRAL is configured at your convenience and built, you can install it:
```
ninja install
```

### Build OAI gNB

You can now proceed building OAI. You build it the same way as for other
radios, providing option `-t oran_fhlib_5g`. Additionally, you need to provide
it the location of the FH library: `--cmake-opt -Dxran_LOCATION=PATH`. Note
that since we cannot use `~` here, we resort to `$HOME`, which is equivalent.
Finally, if you needed to define `PKG_CONFIG_PATH` previously, you need to do
so now, too.

```bash
# You should have already cloned above
cd ~/openairinterface5g/cmake_targets
# if you installed DPDK in a custom path as described above
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib64/pkgconfig/
./build_oai -I  # if you never installed OAI, use this command once before the next line
./build_oai --gNB --ninja -t oran_fhlib_5g --cmake-opt -Dxran_LOCATION=$HOME/phy/fhi_lib/lib
```

You can optionally check that everything has been linked properly with
```bash
ldd ran_build/build/liboran_fhlib_5g.so
```

<details>
<summary>Possible output</summary>

```console
#check if all the libraries are properly linked to liboai_transpro.so
ldd ran_build/build/liboran_fhlib_5g.so
    linux-vdso.so.1 (0x00007fffb459e000)
    librte_node.so.21 => /usr/local/lib64/librte_node.so.21 (0x00007fd358690000)
    librte_graph.so.21 => /usr/local/lib64/librte_graph.so.21 (0x00007fd358685000)
    librte_bpf.so.21 => /usr/local/lib64/librte_bpf.so.21 (0x00007fd358672000)
    librte_flow_classify.so.21 => /usr/local/lib64/librte_flow_classify.so.21 (0x00007fd35866c000)
    librte_pipeline.so.21 => /usr/local/lib64/librte_pipeline.so.21 (0x00007fd35862f000)
    librte_table.so.21 => /usr/local/lib64/librte_table.so.21 (0x00007fd358612000)
    librte_port.so.21 => /usr/local/lib64/librte_port.so.21 (0x00007fd3585f8000)
    librte_fib.so.21 => /usr/local/lib64/librte_fib.so.21 (0x00007fd3585e9000)
...
    libm.so.6 => /lib64/libm.so.6 (0x00007fd357eb1000)
    libnuma.so.1 => /lib64/libnuma.so.1 (0x00007fd357ea1000)
    libc.so.6 => /lib64/libc.so.6 (0x00007fd357c98000)
    /lib64/ld-linux-x86-64.so.2 (0x00007fd3587c7000)
    libelf.so.1 => /lib64/libelf.so.1 (0x00007fd357c7d000)
    libz.so.1 => /lib64/libz.so.1 (0x00007fd357c61000)
```
</details>

Note that you might also call cmake directly instead of using `build_oai`:
```
cd ~/openairinterface5g
mkdir build && cd build
# build RAN after manually building xran K release
cmake .. -GNinja -DOAI_FHI72=ON -Dxran_LOCATION=$HOME/phy/fhi_lib/lib
# build RAN and xran K release automatically
cmake .. -GNinja -DOAI_FHI72=ON -Dxran_DOWNLOAD=ON
ninja nr-softmodem oran_fhlib_5g params_libconfig
```

## Configuration

RU and DU configurations have a circular dependency: you have to configure DU MAC address in the RU configuration and the RU MAC address, VLAN and Timing advance parameters in the DU configuration.

**Note**: You may run OAI with O-RAN 7.2 Fronthaul without a RU attached (e.g. for benchmarking).
In such case, skip RU configuration and only configure Network Interfaces, DPDK VFs and OAI configuration by using arbitrary values for RU MAC addresses and VLAN tags.

### Configure the RU

Contact the RU vendor and get the configuration manual to understand the below commands. The below configuration only corresponds to the RU firmware version indicated at the start of this document. If your firmware version does not correspond to the indicated version, then please don't try these commands.

**NOTE**: Please understand all the changes you are doing at the RU, especially if you are manipulating anything related to output power. 

#### Benetel 650

The OAI configuration file [`gnb-du.sa.band77.273prb.fhi72.4x4-benetel650.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band77.273prb.fhi72.4x4-benetel650.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 100MHz
- MTU 9216
- 4TX4R

##### RU configuration

After switching on the radio or rebooting, wait for the radio bring up to complete, which you can follow using `tail -f  /tmp/logs/radio_status`. Once you will see `[INFO] Radio bringup complete`, you can configure the RU via editing `/etc/ru_config.cfg`

```bash
cat /etc/ru_config.cfg

mimo_mode=1_2_3_4_4x4
downlink_scaling=0
prach_format=short
compression=static_compressed # if `dynamic_compressed` used, set the `comp_hdr_type` to `dynamic` in the gNB config file
lf_prach_compression_enable=true
cplane_per_symbol_workaround=disabled
cuplane_dl_coupling_sectionID=disabled
flexran_prach_workaround=disabled
dl_ul_tuning_special_slot=0xfd00000
```

#### Benetel 550

The OAI configuration file [`gnb.sa.band77.273prb.fhi72.4x2-benetel550-dyn-compression.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x2-benetel550-dyn-compression.conf) corresponds to:
- TDD pattern `DDDDDDDSUU`, 5ms (special slot with 6 DL, 4 guard and 4 UL symbols)
- Bandwidth 100MHz, center frequency 3950.4MHz (band n77)
- MTU 9216
- 4TX2R
- BFP 9 bit dynamic compression
- UL 256QAM disabled

##### RU configuration

After switching on the radio or rebooting, wait for the radio bring up to complete, which you can follow using `tail -f  /tmp/logs/radio_status`. Once you will see `[INFO] Radio bringup complete`, you can configure the RU by editing `/etc/ru_config.cfg`, then start the DU/gNB.
The following RU configuration corresponds to the gNB configuration file above:

```bash
mimo_mode=1_2_3_4_4x2
downlink_scaling=0
ul_gain_correction=0
prach_format=short
prach_freq_offset_dynamic=true
prach_freq_offset=0
compression=dynamic_compressed
prach_msg1_freq_start=0
lf_prach_compression_enable=true
cplane_per_symbol_workaround=disabled
cuplane_dl_coupling_sectionID=disabled
flexran_prach_workaround=disabled
dpd_reset_timer=43200
c_plane_du_mac=00:11:22:44:88:99
u_plane_du_mac=00:11:22:44:88:99
u_plane_du_vlan_uplink=6
u_plane_du_vlan_downlink=6
c_plane_du_vlan=6
tdd_switching=1500
bandwidth_hz=100000000
centre_frequency_hz=3950400000
tx_power_dbm=24.000000
tdd_pattern_1=DDDD
tdd_pattern_2=DDDSUU
special_slots_symbols=DDDDDDGGGGUUUU
lf_prach_slot_id=0
dl_tuning_special_slot=0x13b6
cplane_per_symbol_timing=false
non_ideal_c4_timing_enable=false
m_plane_u_plane_config_v16=false
synce_enable=false
```

###### Compression

The compression type must match on the RU and the gNB. For dynamic compression, set `compression=dynamic_compressed` on the RU and `comp_hdr_type = "dynamic"` in the gNB configuration file.
For static compression, set `compression=static_compressed` on the RU and remove `comp_hdr_type` from the gNB configuration file (`static` is the default).

For uncompressed 16b mode (tested in 2x2), set the IQ width to 16 in the gNB configuration file (no `comp_hdr_type`):
```
pdsch_AntennaPorts_N1      = 1;  # 2x2
...
nb_tx = 2;
nb_rx = 2;
...

fhi_72 = {
  ...
  fh_config = ({
    ...
    ru_config = {
      iq_width = 16;
      iq_width_prach = 16;
    };
    prach_config = {
      eAxC_offset = 8;
    };
  });
};
```

On the RU side, modify the following parameters with respect to the configuration above:
```bash
mimo_mode=1_3
compression=static_uncompressed
```

###### Long PRACH (format 0)

For testing with long PRACH (format 0), on RU side modify the following parameters:
```bash
mimo_mode=1_3
prach_format=long
prach_freq_offset_dynamic=false
compression=static_compressed
lf_prach_compression_enable=true
lf_prach_slot_id=0
```
And on DU side, with respect to [`gnb.sa.band77.273prb.fhi72.4x2-benetel550-dyn-compression.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x2-benetel550-dyn-compression.conf):
```
pdsch_AntennaPorts_N1      = 1;  # 2x2
prach_ConfigurationIndex   = 7;  # format 0
prach_RootSequenceIndex_PR = 1;  # L=839
# msg1_SubcarrierSpacing   = 1;  # remove, derived from prach_ConfigurationIndex (1.25 kHz)
...
nb_tx = 2;
nb_rx = 2;
...
fhi_72 = {
  ...
  fh_config = ({
    ...
    Ta4 = (0, 831);  # format 0 spans more than one slot
    ru_config = {
      # no comp_hdr_type: static compression, matching the RU
      iq_width = 9;
      iq_width_prach = 9;
    };
    prach_config = {
      eAxC_offset = 8;
    };
  });
};
```

#### LITEON

##### RU configuration

SSH to the unit as user `user`. Write `enable` in the terminal to enter the configuration console; the password should be in the user guide. Use the command `show oru-status` to check the RU status. The output should be similar to:
```bash
# show oru-status 
Sync State  : SYNCHRONIZED
RF State    : Ready
DPD         : Ready
DuConnected : notReady
```

Also, you can use `show running-config` to display the current RU configuration. 

Once the RU is PTP synced, and RF state and DPD are `Ready`, write `configure terminal` to set:
- Center frequency 
- Bandwidth
- Compression Bitwidth
- TX/RX attenuation
- PRACH eAxC IDs
- DU MAC address
...

###### FR1

The OAI configuration file [`gnb.sa.band78.273prb.fhi72.4x4-liteon.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.273prb.fhi72.4x4-liteon.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 100MHz
- MTU 1500
- MTU 9216: v02.00.10

The RU configuration mode example:
```bash
compression-bit 9 # set IQ bitwidth for PxSCH/PRACH
eAXC_id 4 5 6 7 # set PRACH eAxC IDs
jumboframe 1 # enable jumbo frame
...
```

###### FR2

The OAI configuration file [`gnb.sa.band257.132prb.fhi72.2x2-liteon.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band257.132prb.fhi72.2x2-liteon.conf) corresponds to:
- TDD pattern `DDDSU`, 0.625ms
- Bandwidth 200MHz
- FW v02.00.07
- DL uses jumbo frame, UL uses standard MTU of 1500 bytes

The RU configuration mode example:
```bash
compression-bit 8 # set IQ bitwidth for PxSCH/PRACH
eAXC_id 0 1 # set PRACH eAxC IDs
...
```

#### Microamp FR2

Two OAI configuration files are provided for this RU, both with TDD pattern
`DDDSU` (0.625ms) and 2x2 on band n257:
- 200MHz: [`gnb.sa.band257.132prb.fhi72.2x2-microamp.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band257.132prb.fhi72.2x2-microamp.conf)
- 100MHz: [`gnb.sa.band257.66prb.fhi72.2x2-microamp.conf`](../ci-scripts/conf_files/gnb.sa.band257.66prb.fhi72.2x2-microamp.conf)

Both use the same carrier frequency (28.04928GHz), so switching between them
only requires to change the bandwidth of the RU (see below).

#### Firmware starting from 0.1.174

Requirements:
- sshpass:
  ```bash
  #RHEL
  sudo dnf install sshpass -y
  #Ubuntu
  sudo apt install sshpass -y
  ``` 

To check PTP status, you can use the following command:
```bash
sshpass -p microampcfg ssh remctl@<RU_IP_ADDR> "logs-ptp"
```
<details>
  <summary>PTP output will be similar to:</summary>

  ```
  Apr 27 11:58:35 bbv1 ptp4l[74545]: ptp4l[357120.237]: rms    8 max   16 freq   -100 +/- 142 delay  1183 +/-   0
  Apr 27 11:58:34 bbv1 ptp4l[74545]: ptp4l[357119.114]: rms    1 max    1 freq   +189 +/-  25 delay  1183 +/-   0
  Apr 27 11:58:33 bbv1 ptp4l[74545]: ptp4l[357117.991]: rms    1 max    1 freq    +99 +/-  30 delay  1183 +/-   0
  Apr 27 11:58:32 bbv1 ptp4l[74545]: ptp4l[357116.869]: rms    1 max    1 freq    +22 +/-  18 delay  1183 +/-   0
  Apr 27 11:58:31 bbv1 ptp4l[74545]: ptp4l[357115.746]: rms    1 max    1 freq    -51 +/-  20 delay  1183 +/-   0
  Apr 27 11:58:30 bbv1 ptp4l[74545]: ptp4l[357114.624]: rms    1 max    2 freq   -126 +/-  29 delay  1183 +/-   0
  Apr 27 11:58:29 bbv1 ptp4l[74545]: ptp4l[357113.501]: rms    8 max   16 freq    -16 +/- 200 delay  1184 +/-   0
  Apr 27 11:58:28 bbv1 ptp4l[74545]: ptp4l[357112.378]: rms    1 max    2 freq   +164 +/-  29 delay  1183 +/-   0
  Apr 27 11:58:26 bbv1 ptp4l[74545]: ptp4l[357111.256]: rms    1 max    2 freq    +54 +/-  39 delay  1183 +/-   0
  Apr 27 11:58:25 bbv1 ptp4l[74545]: ptp4l[357110.133]: rms    1 max    2 freq    -34 +/-  19 delay  1183 +/-   0
  Apr 27 11:58:24 bbv1 ptp4l[74545]: ptp4l[357109.010]: rms    1 max    2 freq   -112 +/-  26 delay  1183 +/-   0
  Apr 27 11:58:23 bbv1 ptp4l[74545]: ptp4l[357107.888]: rms    8 max   16 freq    -48 +/- 174 delay  1183 +/-   0
  Apr 27 11:58:22 bbv1 ptp4l[74545]: ptp4l[357106.766]: rms    1 max    1 freq   +197 +/-  25 delay  1183 +/-   0
  Apr 27 11:58:21 bbv1 ptp4l[74545]: ptp4l[357105.642]: rms    1 max    2 freq   +149 +/-  26 delay  1183 +/-   0
  Apr 27 11:58:20 bbv1 ptp4l[74545]: ptp4l[357104.519]: rms    1 max    2 freq    +66 +/-  27 delay  1183 +/-   0
  Apr 27 11:58:19 bbv1 ptp4l[74545]: ptp4l[357103.396]: rms    1 max    2 freq    -17 +/-  24 delay  1183 +/-   0
  Apr 27 11:58:17 bbv1 ptp4l[74545]: ptp4l[357102.273]: rms    1 max    1 freq    -94 +/-  23 delay  1183 +/-   0
  Apr 27 11:58:16 bbv1 ptp4l[74545]: ptp4l[357101.150]: rms    7 max   16 freq   +107 +/- 180 delay  1183 +/-   0
  Apr 27 11:58:15 bbv1 ptp4l[74545]: ptp4l[357100.027]: rms    1 max    2 freq   +166 +/-  11 delay  1184 +/-   0
  Apr 27 11:58:14 bbv1 ptp4l[74545]: ptp4l[357098.906]: rms    1 max    1 freq   +140 +/-  21 delay  1183 +/-   0
  ```

</details>

###### RU configuration

You can use the following command to display the current RU configuration:

```bash
sshpass -p microampcfg ssh remctl@<RU_IP_ADDR> get-cfg
```

The OAI configuration file [`gnb.sa.band257.132prb.fhi72.2x2-microamp.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band257.132prb.fhi72.2x2-microamp.conf) corresponds to the following RU configuration:

```
  PRACH0 CC ID: 0
  PRACH0 RU port ID: 0
  PRACH1 CC ID: 1
  PRACH1 RU port ID: 1
  Bandwidth: 200M [Hz]
  Carrier frequency: 28049280000.0 [Hz]
  Compression enable: True
  TDD config: DDDSU
  eCPRI VLAN support enable: True
  eCPRI VLAN tag: 600
    MAC address: 10:70:fd:b8:86:02
    VLAN PTP status: ENABLED
    VLAN MGMT enabled: ENABLED
    State: ENABLED_WITH_VLAN
      Dynamic beamforming with mirrored beams
  PL MAC address:
    RU MAC: 10-70-FD-B8-86-02
    DU MAC: 50-7C-6F-31-00-61
  RF Power level: -5 dB - relative to maximum
```


Execute the following command to check how to configure the RU:

```bash
sshpass -p microampcfg ssh remctl@<RU_IP_ADDR> "help"
```
<details>
  <summary>Help output will be similar to:</summary>

  ```
  Usage: remctl <subcommand> [options]

  Allowed Subcommands:
    set-cfg, get-cfg, stat, logs-ptp, reboot, set-power, help, clear-cfg

  Allowed 'set-cfg' options:
    --bandwidth
    --tdd-cfg
    --compression-enable
    --compression-disable
    --ecpri-vlan-tag
    --ecpri-vlan-support-enable
    --ecpri-vlan-support-disable
    --ru-mac
    --du-mac
    --beamforming
    --carrier-freq
    --eth-ipv4
    --prach0-cc-id
    --prach0-ru-port-id
    --prach1-cc-id
    --prach1-ru-port-id

  ```

</details>



You can also execute the following command to show fronthaul statistics including on-time/late packet' counters:
```bash
sshpass -p microampcfg ssh remctl@<RU_IP_ADDR> "stat"
```

<details>
  <summary>Statistics output will be similar to:</summary>

  ```
  ORAN RX C-plane on time:    71645874
  ORAN RX C-plane early:      0
  ORAN RX C-plane late:       16
  ORAN TX U-plane:            229686268
  ORAN RX DL C-plane on time: 53987559
  ORAN RX DL C-plane early:   0
  ORAN RX DL C-plane late:    12
  ORAN RX DL U-plane on time: 701836967
  ORAN RX DL U-plane early:   0
  ORAN RX DL U-plane late:    52
  ORAN RX UL C-plane on time: 17658315
  ORAN RX UL C-plane early:   0
  ORAN RX UL C-plane late:    4
  CLASSIFIER LLC: overflow protection:    0
  CLASSIFIER LLC: frame err:              28
  CLASSIFIER LLC: packet too long:        0
  CLASSIFIER LLC: dropped:                28
  CLASSIFIER LLC: passed:                 804719078
  CLASSIFIER ETH: dst MAC addr err:       0
  CLASSIFIER ETH: VLAN LEGACY:            275571
  CLASSIFIER ETH: VLAN Dot1Q:             804443507
  CLASSIFIER ETH: VLAN Dot1AD:            0
  CLASSIFIER ETH: VLAN Tag ID pass:       773482909
  CLASSIFIER ETH: VLAN Tag ID error:      0
  CLASSIFIER ETH: multicast:              31233828
  CLASSIFIER ETH: PTP:                    29502626
  CLASSIFIER ETH: eCPRI:                  773482909
  CLASSIFIER ETH: to OS:                  31236169
  CLASSIFIER ETH: dropped:                0
  CLASSIFIER ETH: passed:                 804719078
  CLASSIFIER ECPRI: prot rev unsupported: 0
  CLASSIFIER ECPRI: concat unsupported:   0
  CLASSIFIER ECPRI: msg type unsupported: 0
  CLASSIFIER ECPRI: pld size err:         0
  CLASSIFIER ECPRI: U-Plane:              701837019
  CLASSIFIER ECPRI: C-Plane:              71645890
  CLASSIFIER ECPRI: delay:                0
  CLASSIFIER ECPRI: dropped:              0
  CLASSIFIER ECPRI: passed:               773482909
  CPLANE PARSER: ul early:                        0
  CPLANE PARSER: ul late:                         4
  CPLANE PARSER: ul on time:                      17658315
  CPLANE PARSER: dl early:                        0
  CPLANE PARSER: dl late:                         12
  CPLANE PARSER: dl on time:                      53987559
  CPLANE PARSER: timing dropped:                  16
  CPLANE PARSER: timing passed:                   71645874
  CPLANE PARSER: bad du port id:                  0
  CPLANE PARSER: bad bandsector id:               0
  CPLANE PARSER: bad cc id:                       663059
  CPLANE PARSER: bad ru port id:                  2794
  CPLANE PARSER: bad sequence id:                 0
  CPLANE PARSER: bad e bit:                       0
  CPLANE PARSER: bad subsequence id:              0
  CPLANE PARSER: ecpri transport header dropped:  665623
  CPLANE PARSER: ecpri transport header passed:   70980251
  CPLANE PARSER: bad payloadversion:              0
  CPLANE PARSER: bad filterindex:                 0
  CPLANE PARSER: bad subframeid:                  0
  CPLANE PARSER: bad slotid:                      0
  CPLANE PARSER: bad startsymbolid:               0
  CPLANE PARSER: bad numberofsections:            0
  CPLANE PARSER: bad sectiontype:                 0
  CPLANE PARSER: bad timeoffset:                  0
  CPLANE PARSER: bad fftsize:                     0
  CPLANE PARSER: bad subcarrierspacing:           0
  CPLANE PARSER: bad cplength:                    0
  CPLANE PARSER: bad udiqwidth:                   0
  CPLANE PARSER: radio aplication header dropped: 0
  CPLANE PARSER: radio aplication header passed:  70980251
  CPLANE PARSER: bad rb:                          0
  CPLANE PARSER: bad startprbc:                   0
  CPLANE PARSER: bad numprbc:                     0
  CPLANE PARSER: bad remask:                      0
  CPLANE PARSER: bad numsymbol:                   0
  CPLANE PARSER: bad ef:                          0
  CPLANE PARSER: bad freqoffset:                  0
  CPLANE PARSER: common section fields dropped:   0
  CPLANE PARSER: common section fields passed:    70980251
  CPLANE PARSER: bad section to symbol map:       0
  CPLANE PARSER: dropped:                         0
  CPLANE PARSER: passed:                          70980251
  CPLANE PARSER: expected uplanes number:         701813698
  UPLANE PARSER: sequence ID err:       0
  UPLANE PARSER: IQ pld size err:       0
  UPLANE PARSER: C-Plane match err:     114225
  UPLANE PARSER: eAxC ID unsupported:   24631
  UPLANE PARSER: any value unsupported: 24631
  UPLANE PARSER: dropped:               138886
  UPLANE PARSER: passed:                701698133
  UPLANE ENCAPSULATOR 0: false drop error: 0
  UPLANE ENCAPSULATOR 0: passed:           94598642
  UPLANE ENCAPSULATOR 1: false drop error: 0
  UPLANE ENCAPSULATOR 1: passed:           94598642
  UPLANE ENCAPSULATOR 2: false drop error: 0
  UPLANE ENCAPSULATOR 2: passed:           0
  UPLANE ENCAPSULATOR 3: false drop error: 0
  UPLANE ENCAPSULATOR 3: passed:           0
  COMMON COMMON: symbol counter: 57757250505
  DSP CHAIN 0: dl fft overflow: 0
  DSP CHAIN 0: ul fft overflow: 0
  DSP CHAIN 1: dl fft overflow: 0
  DSP CHAIN 1: ul fft overflow: 0
  ```

</details>


##### Firmware older than 0.1.174
Interaction with RU is performed using `rucfg` utility provided by Microamp.

To check PTP status, you can use `rucfg ptp`. 


<details>
  <summary>PTP output will be similar to:</summary>

  ```
  [INFO] Check if RU is available
  [INFO] RU available
  [INFO] Check SSH to RU available
  [INFO] SSH to RU available
  [INFO] Getting PTP status
  [INFO] ptp4l status = 
  {
  Feb 12 16:26:02 bbv1 ptp4l[23822]: ptp4l[6280.658]: rms    0 max    1 freq     +2 +/-   7 delay  1184 +/-   0
  Feb 12 16:26:00 bbv1 ptp4l[23822]: ptp4l[6279.535]: rms    0 max    1 freq     -8 +/-  13 delay  1184 +/-   0
  Feb 12 16:25:59 bbv1 ptp4l[23822]: ptp4l[6278.413]: rms    1 max    1 freq    -35 +/-  13 delay  1184 +/-   0
  Feb 12 16:25:58 bbv1 ptp4l[23822]: ptp4l[6277.290]: rms    0 max    1 freq    -68 +/-   9 delay  1184 +/-   0
  Feb 12 16:25:57 bbv1 ptp4l[23822]: ptp4l[6276.167]: rms    1 max    1 freq    -69 +/-  10 delay  1184 +/-   0
  Feb 12 16:25:56 bbv1 ptp4l[23822]: ptp4l[6275.044]: rms    0 max    1 freq    -46 +/-   9 delay  1184 +/-   0
  Feb 12 16:25:55 bbv1 ptp4l[23822]: ptp4l[6273.921]: rms    1 max    1 freq    -65 +/-  10 delay  1184 +/-   0
  Feb 12 16:25:54 bbv1 ptp4l[23822]: ptp4l[6272.800]: rms    0 max    1 freq    -91 +/-   6 delay  1184 +/-   0
  Feb 12 16:25:53 bbv1 ptp4l[23822]: ptp4l[6271.677]: rms    4 max   17 freq   -107 +/-  48 delay  1184 +/-   0
  Feb 12 16:25:52 bbv1 ptp4l[23822]: ptp4l[6270.554]: rms    6 max   17 freq   +179 +/- 106 delay  1184 +/-   0
  Feb 12 16:25:50 bbv1 ptp4l[23822]: ptp4l[6269.431]: rms    1 max    2 freq   +191 +/-  12 delay  1184 +/-   0
  Feb 12 16:25:49 bbv1 ptp4l[23822]: ptp4l[6268.308]: rms    1 max    1 freq   +119 +/-  21 delay  1184 +/-   0
  Feb 12 16:25:48 bbv1 ptp4l[23822]: ptp4l[6267.186]: rms    1 max    1 freq   +103 +/-  13 delay  1184 +/-   0
  Feb 12 16:25:47 bbv1 ptp4l[23822]: ptp4l[6266.063]: rms    7 max   17     freq   -101 +/- 160 delay  1184 +/-   0
  Feb 12 16:25:46 bbv1 ptp4l[23822]: ptp4l[6264.940]: rms    7 max   17 freq    -65 +/- 154 delay  1184 +/-   0
  Feb 12 16:25:45 bbv1 ptp4l[23822]: ptp4l[6263.817]: rms    1 max    2 freq   +176 +/-  33 delay  1183 +/-   0
  Feb 12 16:25:44 bbv1 ptp4l[23822]: ptp4l[6262.695]: rms    0 max    1 freq   +100 +/-   7 delay  1184 +/-   0
  Feb 12 16:25:43 bbv1 ptp4l[23822]: ptp4l[6261.572]: rms    1 max    2 freq    +56 +/-  34 delay  1184 +/-   0
  Feb 12 16:25:41 bbv1 ptp4l[23822]: ptp4l[6260.449]: rms    1 max    1 freq    -37 +/-  14 delay  1184 +/-   0
  Feb 12 16:25:40 bbv1 ptp4l[23822]: ptp4l[6259.326]: rms    7 max   16 freq   +114 +/- 149 delay  1183 +/-   0
  } 
  ```

</details>

###### RU configuration

You can use `rucfg show` command to display the current RU configuration. 


The OAI configuration file [`gnb.sa.band257.132prb.fhi72.2x2-microamp.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band257.132prb.fhi72.2x2-microamp.conf) corresponds to the following RU configuration:

```
  [INFO] Check if RU is available
  [INFO] RU available
  [INFO] Check SSH to RU available
  [INFO] SSH to RU available
  [INFO] Downloading oran_autostart
  [INFO] Downloading ructl_config.sh
  [INFO] Downloading udc_pll_configurator_startup
  [INFO] Downloaded Cellbox config = 
  {
    eCPRI Compression: True
    RF Bandwidth: 200MHz
    CC Bandwidth: 200MHz
    CCs: 1
    LO frequency: 7.91642667e9
    UL compensation frequency: 28.04928e9
    DL compensation frequency: -28.04928e9
    RU MAC: 10:70:FD:B8:86:02
    DU MAC: 50:7C:6F:31:00:61
    TDD config: dddsu
    RF Power level: -5 dB - relative to maximum
    VLAN ORAN: Enabled, tag: 600
    VLAN PTP: Enabled, tag: 1
    VLAN MGMT: False
    Beamforming: dynamic-mirrored-beam
  }
```

Execute `rucfg config -h` to check how to configure the RU.

<details>
<summary>Help output will be similar to:</summary>

```
usage: rucfg config [-h] [-f frequency] [-b bandwidth] [-t TDD pattern] [-p RU Power]
                    [--cc carrier components] [--compression compression] [--vlan-oran vlan-oran]
                    [--vlan-ptp vlan-ptp] [--mac DU MAC address]

options:
  -h, --help            show this help message and exit
  -f frequency, --freq frequency
                        Sets the 5G NR frequency. Allowed range: 24.0-29.9 GHz, default: <no conf
                        change>
  -b bandwidth, --bandwidth bandwidth
                        Sets RF bandwidth 100/200 [MHz] per CC, default: <no conf change>
  -t TDD pattern, --tdd TDD pattern
                        Sets TDD pattern DDDSU/DDSUU/DSUUU, default: <no conf change>
  -p RU Power, --power RU Power
                        Sets Power in (0-100), default: <no conf change>
  --cc carrier components
                        Sets number of carrier components 1/2, default: <no conf change>
  --compression compression
                        Sets eCPRI compression True/False, default: <no conf change>
  --vlan-oran vlan-oran
                        Sets ORAN VLAN to None or <TAG>, default: <no conf change>
  --vlan-ptp vlan-ptp   Sets PTP VLAN to None or <TAG>, default: <no conf change>
  --mac DU MAC address  Sets DU MAC address (where RU sends eCPRI packets), format
                        AA:BB:CC:DD:EE:FF, default: <no conf change>
```

</details>



You can also execute `rucfg stats` to show fronthaul statistics including on-time/late packet' counters.


<details>
  <summary>Statistics output will be similar to:</summary>

  ```
  [INFO] Check if RU is available
  [INFO] RU available
  [INFO] Check SSH to RU available
  [INFO] SSH to RU available
  [INFO] Getting RU stats
  [WARN] Received non-zero rc from ructl: 255
  [INFO] ru stats = 
  {
  Preaccumulator overflow detected
  ORAN RX C-plane on time:    5442384
  ORAN RX C-plane early:      0
  ORAN RX C-plane late:       4
  ORAN TX U-plane:            15908424
  ORAN RX DL C-plane on time: 3349166
  ORAN RX DL C-plane early:   0
  ORAN RX DL C-plane late:    0
  ORAN RX DL U-plane on time: 43538742
  ORAN RX DL U-plane early:   0
  ORAN RX DL U-plane late:    93
  ORAN RX UL C-plane on time: 2093218
  ORAN RX UL C-plane early:   0
  ORAN RX UL C-plane late:    4
  CLASSIFIER LLC: overflow protection:    0
  CLASSIFIER LLC: frame err:              0
  CLASSIFIER LLC: packet too long:        0
  CLASSIFIER LLC: dropped:                0
  CLASSIFIER LLC: passed:                 49044133
  CLASSIFIER ETH: dst MAC addr err:       0
  CLASSIFIER ETH: VLAN LEGACY:            1524
  CLASSIFIER ETH: VLAN Dot1Q:             49042609
  CLASSIFIER ETH: VLAN Dot1AD:            0
  CLASSIFIER ETH: VLAN Tag ID pass:       48981223
  CLASSIFIER ETH: VLAN Tag ID error:      0
  CLASSIFIER ETH: multicast:              60260
  CLASSIFIER ETH: PTP:                    56086
  CLASSIFIER ETH: eCPRI:                  48981223
  CLASSIFIER ETH: to OS:                  62910
  CLASSIFIER ETH: dropped:                0
  CLASSIFIER ETH: passed:                 49044133
  CLASSIFIER ECPRI: prot rev unsupported: 0
  CLASSIFIER ECPRI: concat unsupported:   0
  CLASSIFIER ECPRI: msg type unsupported: 0
  CLASSIFIER ECPRI: pld size err:         0
  CLASSIFIER ECPRI: U-Plane:              43538835
  CLASSIFIER ECPRI: C-Plane:              5442388
  CLASSIFIER ECPRI: delay:                0
  CLASSIFIER ECPRI: dropped:              0
  CLASSIFIER ECPRI: passed:               48981223
  CPLANE PARSER: ul early:                        0
  CPLANE PARSER: ul late:                         4
  CPLANE PARSER: ul on time:                      2093218
  CPLANE PARSER: dl early:                        0
  CPLANE PARSER: dl late:                         0
  CPLANE PARSER: dl on time:                      3349166
  CPLANE PARSER: timing dropped:                  4
  CPLANE PARSER: timing passed:                   5442384
  CPLANE PARSER: bad du port id:                  0
  CPLANE PARSER: bad bandsector id:               0
  CPLANE PARSER: bad cc id:                       0
  CPLANE PARSER: bad ru port id:                  0
  CPLANE PARSER: bad sequence id:                 0
  CPLANE PARSER: bad e bit:                       0
  CPLANE PARSER: bad subsequence id:              0
  CPLANE PARSER: ecpri transport header dropped:  0
  CPLANE PARSER: ecpri transport header passed:   5442384
  CPLANE PARSER: bad payloadversion:              0
  CPLANE PARSER: bad filterindex:                 0
  CPLANE PARSER: bad subframeid:                  0
  CPLANE PARSER: bad slotid:                      0
  CPLANE PARSER: bad startsymbolid:               0
  CPLANE PARSER: bad numberofsections:            0
  CPLANE PARSER: bad sectiontype:                 0
  CPLANE PARSER: bad timeoffset:                  0
  CPLANE PARSER: bad fftsize:                     0
  CPLANE PARSER: bad subcarrierspacing:           0
  CPLANE PARSER: bad cplength:                    0
  CPLANE PARSER: bad udiqwidth:                   0
  CPLANE PARSER: radio aplication header dropped: 0
  CPLANE PARSER: radio aplication header passed:  5442384
  CPLANE PARSER: bad rb:                          0
  CPLANE PARSER: bad startprbc:                   0
  CPLANE PARSER: bad numprbc:                     0
  CPLANE PARSER: bad remask:                      0
  CPLANE PARSER: bad numsymbol:                   0
  CPLANE PARSER: bad ef:                          0
  CPLANE PARSER: bad freqoffset:                  0
  CPLANE PARSER: common section fields dropped:   0
  CPLANE PARSER: common section fields passed:    5442384
  CPLANE PARSER: bad section to symbol map:       0
  CPLANE PARSER: dropped:                         0
  CPLANE PARSER: passed:                          5442384
  UPLANE PARSER: sequence ID err:       0
  UPLANE PARSER: IQ pld size err:       0
  UPLANE PARSER: C-Plane match err:     7138
  UPLANE PARSER: eAxC ID unsupported:   0
  UPLANE PARSER: any value unsupported: 0
  UPLANE PARSER: dropped:               7231
  UPLANE PARSER: passed:                43531604
  UPLANE ENCAPSULATOR 0: false drop error: 0
  UPLANE ENCAPSULATOR 0: passed:           6698292
  UPLANE ENCAPSULATOR 1: false drop error: 0
  UPLANE ENCAPSULATOR 1: passed:           6698292
  UPLANE ENCAPSULATOR 2: false drop error: 0
  UPLANE ENCAPSULATOR 2: passed:           0
  UPLANE ENCAPSULATOR 3: false drop error: 0
  UPLANE ENCAPSULATOR 3: passed:           0
  COMMON COMMON: symbol counter: 111095058
  }
  ```

</details>


#### VVDN LPRU

**Version 3.x**

The OAI configuration file [`gnb.sa.band77.273prb.fhi72.4x4-vvdn.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x4-vvdn.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 100MHz
- MTU 9216

##### RU configuration

Check in the RU user manual how to configure the center frequency. There are multiple ways to do it. We set the center frequency by editing `sysrepocfg` database. You can use `sysrepocfg --edit=vi -d running` to do the same. You can edit the `startup` database to make the center frequency change persistent. 

To make any change in the RU, always wait for it to be PTP synced. You can check the sync status via `tail -f /var/log/synctimingptp2.log`.

To set the RU for 4TX and 4RX antennas with 9 bit compression, you need to create this XML file and apply it. 

Login to the ru and create this file as `4x4-config.xml`.

```xml
<vvdn_config>
    <du_mac_address>YOUR-DU-MAC-ADDR</du_mac_address>
    <cu_plane_vlan>YOUR-RU-VLAN</cu_plane_vlan>  
    <dl_compression_method>1</dl_compression_method>  
    <dl_compression_value>9</dl_compression_value>  
    <ul_compression_method>1</ul_compression_method> 
    <ul_compression_value>9</ul_compression_value>
    <num_prb>273</num_prb>
    <prach_layer0_PCID>4</prach_layer0_PCID>
    <prach_layer1_PCID>5</prach_layer1_PCID>
    <prach_layer2_PCID>6</prach_layer2_PCID>
    <prach_layer3_PCID>7</prach_layer3_PCID>
    <pxsch_layer0_PCID>0</pxsch_layer0_PCID>
    <pxsch_layer1_PCID>1</pxsch_layer1_PCID>
    <pxsch_layer2_PCID>2</pxsch_layer2_PCID>
    <pxsch_layer3_PCID>3</pxsch_layer3_PCID>
</vvdn_config>
```
Execute the below commands on every restart

```bash
xml_parser 4x4-config.xml
## To enable prach compression
mw.l a0010024 1919 # format `<PRACH-comp-method><PRACH-compr-value><PUSCH-comp-method><PUSCH-compr-value>
## This will show the current configuration
/etc/scripts/lpru_configuration.sh
## Edit the sysrepo to ACTIVATE the carrier when you want to use the RU
## option 1 - activation by writing directly in register
mw.l a0050010 <YOUR-RU-VLAN>3 # e.g. VLAN = 4 => `mw.l a0050010 43`
## option 2 - activation via sysrepocfg command
sysrepocfg --edit=vi -d running
```

#### Metanoia RU

##### FR1 Jura
**Version 2.0.6**

The OAI configuration file [`gnb.sa.band78.273prb.fhi72.4x4-metanoia.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.273prb.fhi72.4x4-metanoia.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms (`DDDDDDDSUU`, 5ms, also supported)
- Bandwidth 100MHz
- 4TX4R

The RU configuration is stored in `/etc/rumanager.conf`. The required modifications:
1. `processing_element/vlan_id`
2. `processing_element/du_mac_address`
3. `low_level_tx_endpoint/compression_type` -> `STATIC`
4. `low_level_rx_endpoint/compression_type` -> `STATIC`
5. `low_level_rx_endpoint/compression/fs-offset` -> `8`
6. `center-of-channel-bandwidth` -> `3750000000`
7. `tx_gain_correction` -> tested with `6020` (please be careful to not fry the RU)
8. `rx_gain_correction` -> tested with `-903` (please be careful to not fry the RU)

At this stage, RU must be rebooted so the changes apply.


##### FR1 Cobra
**Version 1.0.1**

The OAI configuration file [`gnb-du.sa.band78.273prb.fhi72.4x4-metanoia-cobra.conf`](../ci-scripts/conf_files/gnb-du.sa.band78.273prb.fhi72.4x4-metanoia-cobra.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 100MHz
- 4TX4R

The RU configuration is stored in `/etc/rumanager.conf`. The required modifications:
1. `processing_element/vlan_id`
2. `processing_element/du_mac_address`
3. `low_level_tx_endpoint/compression_type` -> `STATIC`
4. `low_level_rx_endpoint/compression_type` -> `STATIC`
5. `iop_cfg_flag` -> `0x0008` (this will enable O-RAN mode for DL digital power, PS: UL digital power is not supported in this version yet)
5. `low_level_tx_endpoint/compression/fs-offset` -> `7`
6. `center-of-channel-bandwidth` -> `3500250000`
7. `l2_mtu` -> `9000`

At this stage, RU must be rebooted so the changes apply.

##### FR2 Cobra
**Version 1.0.1**

The OAI configuration file [`gnb.sa.band258.132prb.fhi72.2x2-metanoia.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band258.132prb.fhi72.2x2-metanoia.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 200MHz
- 2TX2R

The RU configuration is stored in `/etc/rumanager.conf`. The required modifications:
1. `processing_element/vlan_id`
2. `processing_element/du_mac_address`
3. `low_level_tx_endpoint/compression_type` -> `STATIC`
4. `low_level_rx_endpoint/compression_type` -> `STATIC`
5. `low_level_rx_endpoint/compression/fs-offset` -> `8`
6. `iop_cfg_flag` -> `0x0008` (this will enable O-RAN mode for DL digital power, PS: UL digital power is not supported in this version yet)
7. `low_level_tx_endpoint/compression/fs-offset` -> `7`
8. `center-of-channel-bandwidth` -> `26700000000`
9. `l2_mtu` -> `9000`
10. `rf_tx_gain_manual` -> `17`

At this stage, RU must be rebooted so the changes apply.

#### Foxconn RPQN RU

**Version v3.1.15q.551_rc10**

The OAI configuration file [`gnb.sa.band78.273prb.fhi72.4X4-foxconn.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.273prb.fhi72.4x4-foxconn.conf) corresponds to:
- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 100MHz
- MTU 9216

##### RU configuration

After switching on or rebooting the RU, the `/home/root/test/init_rrh_config_enable_cuplane` script should be run.

The RU configuration file is located in `/home/root/test/RRHconfig_xran.xml`. The required modifications:
1.  `RRH_DST_MAC_ADDR`
2.  `RRH_SRC_MAC_ADDR`
3.  `RRH_EAXC_ID_TYPE1`
4.  `RRH_EAXC_ID_TYPE3`
5.  `RRH_CMPR_HDR_PRESENT` -> `0`
6.  `RRH_C_PLANE_VLAN_TAG`
7.  `RRH_U_PLANE_VLAN_TAG`
8.  `RRH_LO_FREQUENCY_KHZ` -> `3750000, 0`
9.  `RRH_DISABLE_USING_CAL_TABLES` -> `YES`
10. `RRH_TX_ATTENUATION` -> must be larger than 10dB
11. `RRH_RX_ATTENUATION` -> must be lower than 30dB

RU must be rebooted so the changes apply.

**Note**

- The RU was tested with the `2024.w30` tag of OAI.
- The measured throughput was **520 Mbps DL** and **40 Mbps UL**.
- With newer OAI versions, throughput degrades. This issue is currently under investigation.

#### ProtO-RU

[ProtO-RU](https://github.com/NUS-CIR/ProtO-RU) is a software implementation of an O-RAN 7.2 RU using a NI USRP.
Different from other COTS RUs, ProtO-RU requires a larger DU delay profile which is larger than the TTI interval.

The OAI configuration file [`gnb.sa.band78.106prb.fhi72.1x1-proto-ru.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.106prb.fhi72.1x1-proto-ru.conf) corresponds to:

- TDD pattern `DDDSU`, 2.5ms
- Bandwidth 40MHz
- MTU 9216
- 1T1R

##### RU configuration

First, compile the RU as outlined in the [building ProtO-RU tutorial](https://github.com/NUS-CIR/ProtO-RU/tree/proto-ru?tab=readme-ov-file#building-proto-ru).
Then, ensure that both your DU and ProtO-RU host are PTP synchronized.

Next, use the RU config, [protoru-OAI-B210-TDD-n78-40MHz-1x1-30kHz.yml](https://github.com/NUS-CIR/ProtO-RU/blob/proto-ru/proto-ru/conf-files/protoru-OAI-B210-TDD-n78-40MHz-1x1-30kHz.yml), which corresponds to the above mentioned DU config file. Please note that the RU delay profile might need to be adjusted according to the setup. The E2E test with xran K release required `T2a_max_cp_ul: 2985`.
In addition, please adapt the DU MAC address and VLAN tag to your needs.

ProtO-RU was successfully tested with USRP B210.
If you are using a different SDR (e.g., N310), you will need to update the ProtO-RU and the DU configurations accordingly.

Launch ProtO-RU with the adapted configuration file with the command:
```bash
sudo ./ru_emulator -c <path-to/protoru-OAI-B210-TDD-n78-40MHz-1x1-30kHz.yml>
```

Finally, start the OAI gNB.

#### WNC R1220

**Version 1.9.0**

The OAI configuration file [gnb.sa.band77.273prb.fhi72.4x4-wnc.conf](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x4-wnc.conf) corresponds to:

- TDD pattern `DDDDDDSUUU`, 5ms
- Bandwidth 100MHz
- 4TX4R

##### RU configuration

After switching on or rebooting the RU, you can check the RU PTP status with `show ptp clock` to make sure it's locked and run `show running-config` to check the current configuration.
To enable RU transmission, use the `radio enable` command. Then verify the configuration with `show running-config`, which should reflect the enabled state:
```
radio 1
  no shutdown
```

The required parameters to configure are:

1. `center-frequency` →  3849990
2. `transmit-power` →  24
3. `lna-shutdown` →  **disabled**
4. `transport-interface` →  configure the sub-interface (VLAN ID is defined during sub-interface creation)
5. `transport-peer-mac` →  must match the DU MAC address
6. `phase-compensation` →  **enabled**
7. `bandwidth` →  100
8. `sub-carrier` →  30
9. `compress-oran-compliant` →  **enabled**

**Note**
  * Ensure that the VLAN configuration and MAC addressing are consistent with the DU setup.
  * The RU must be PTP synchronized before starting the gNB.
  * After reboot, the RU loads its `startup-config`. To save the current configuration, use `copy running-config startup-config`.

## Configure Network Interfaces and DPDK VFs

The 7.2 fronthaul uses the xran library, which requires DPDK. In this step, we
need to configure network interfaces to send data to the RU, and configure DPDK
to bind to the corresponding PCI interfaces. More specifically, in the
following we use [SR-IOV](https://en.wikipedia.org/wiki/Single-root_input/output_virtualization)
to create one or multiple virtual functions (VFs) through which Control plane (C
plane) and User plane (U plane) traffic will flow. The following commands are
not persistant, and have to be repeated after reboot.

In the following, we will use these short hands:

- `IF_NAME`: Physical network interface through which you can access the RU
- `VLAN`: the VLAN tag as recommended by the RU vendor
- `MTU`: this MTU must be higher than supported by the RU vendor due to additional ethernet header of 14 B and DPDK packet header `RTE_PKTMBUF_HEADROOM` of 128 B
- `DU_U_PLANE_MAC_ADD`: DU U plane MAC address
- `U_PLANE_PCI_BUS_ADD`: PCI bus address of the VF for U plane
- `DU_C_PLANE_MAC_ADD`: DU C plane MAC address
- `C_PLANE_PCI_BUS_ADD`: PCI bus address of the VF for C plane

In the configuration file, in option `fhi_72.dpdk_devices`, the first PCI address is for U-plane and the second for C-plane.

RU might support either one DU MAC address for both CU planes or two different.
i.e. VVDN Gen3, Metanoia support only one, Benetel550 supports both cases


**Note**

- X710 NIC supports the same DU MAC address for multiple VFs
- E-810 NIC requires different DU MAC addresses for multiple VFs

If the RU vendor requires untagged traffic, remove the VLAN tagging
in the below command and configure VLAN on the switch as "access VLAN". In case
the MTU is different than 1500, you have to update the MTU on the switch
interface as well.

#### Set maximum ring buffers:

As a first step, please set up the maximum allowed buffer size to your desired interface. To check the maximum value, please execute the following command:
```bash
sudo ethtool -g $IF_NAME
```

```bash
set -x
IF_NAME=<YOUR_PHYSICAL_INTERFACE_NAME>
MAX_RING_BUFFER_SIZE=<YOUR_PHYSICAL_INTERFACE_MAX_BUFFER_SIZE>

sudo ethtool -G $IF_NAME rx $MAX_RING_BUFFER_SIZE tx $MAX_RING_BUFFER_SIZE
```

#### Set the maximum MTU in the physical interface:
```bash
set -x
IF_NAME=<YOUR_PHYSICAL_INTERFACE_NAME>
MTU=<RU_MTU>

sudo ip link set $IF_NAME mtu $MTU
```

#### (Re-)create VF(s)

##### one VF

```bash
set -x
IF_NAME=<YOUR_PHYSICAL_INTERFACE_NAME>
DU_CU_PLANE_MAC_ADD=<YOUR_DU_CU_PLANE_MAC_ADDRESS>
VLAN=<RU_VLAN>
MTU=<RU_MTU>

sudo modprobe iavf
sudo sh -c 'echo 0 > /sys/class/net/$IF_NAME/device/sriov_numvfs'
sudo sh -c 'echo 1 > /sys/class/net/$IF_NAME/device/sriov_numvfs'
sudo ip link set $IF_NAME vf 0 mac $DU_CU_PLANE_MAC_ADD vlan $VLAN mtu $MTU spoofchk off # set CU planes PCI address
```

##### two VFs

```bash
set -x
IF_NAME=<YOUR_PHYSICAL_INTERFACE_NAME>
DU_U_PLANE_MAC_ADD=<YOUR_DU_U_PLANE_MAC_ADDRESS>
DU_C_PLANE_MAC_ADD=<YOUR_DU_C_PLANE_MAC_ADDRESS> # can be same as for U plane -> depends if the NIC supports the same MAC address
VLAN=<RU_VLAN>
MTU=<RU_MTU>

sudo modprobe iavf
sudo sh -c 'echo 0 > /sys/class/net/$IF_NAME/device/sriov_numvfs'
sudo sh -c 'echo 2 > /sys/class/net/$IF_NAME/device/sriov_numvfs'
sudo ip link set $IF_NAME vf 0 mac $DU_U_PLANE_MAC_ADD vlan $VLAN mtu $MTU spoofchk off # set U plane PCI address
sudo ip link set $IF_NAME vf 1 mac $DU_C_PLANE_MAC_ADD vlan $VLAN mtu $MTU spoofchk off # set C plane PCI address
```

After running the above commands, the kernel created VF(s) that
have been assigned a PCI address under the same device and vendor ID. For
instance, use `sudo lshw -c network -businfo` to get a list of PCI addresses
and interface names, locate the PCI address of `$IF_NAME`, then use
`lspci | grep Virtual` to get all virtual interfaces and use the ones with the
same Device/Vendor ID parts (first two numbers).

<details>
<summary>Example with two VFs</summary>

The machine in this example has an Intel X710 card. The interface
<physical-interface> in question is `eno12409`. Running `lshw` gives:

```bash
$ sudo lshw -c network -businfo
Bus info          Device     Class          Description
=======================================================
[...]
pci@0000:31:00.1  eno12409   network        Ethernet Controller X710 for 10GbE SFP+
[...]
```

We see the PCI address `31:00.1`. Listing the virtual interfaces through
`lspci`, we get

```bash
$ lspci | grep Virtual
31:06.0 Ethernet controller: Intel Corporation Ethernet Virtual Function 700 Series (rev 02)
31:06.1 Ethernet controller: Intel Corporation Ethernet Virtual Function 700 Series (rev 02)
98:11.0 Ethernet controller: Intel Corporation Ethernet Adaptive Virtual Function (rev 02)
98:11.1 Ethernet controller: Intel Corporation Ethernet Adaptive Virtual Function (rev 02)
98:11.2 Ethernet controller: Intel Corporation Ethernet Adaptive Virtual Function (rev 02)
98:11.3 Ethernet controller: Intel Corporation Ethernet Adaptive Virtual Function (rev 02)
98:11.4 Ethernet controller: Intel Corporation Ethernet Adaptive Virtual Function (rev 02)
98:11.5 Ethernet controller: Intel Corporation Ethernet Adaptive Virtual Function (rev 02)
```

The hardware card `31:00.1` has two associated virtual functions `31:06.0` and
`31:06.1`.
</details>

#### Bind VF(s)

Now, unbind any pre-existing DPDK devices, load the "Virtual Function I/O"
driver `vfio_pci` or `mlx5_core`, and bind DPDK to these devices.

##### Bind one VF

```bash
set -x
CU_PLANE_PCI_BUS_ADD=<YOUR_CU_PLANE_PCI_BUS_ADDRESS>
DRIVER=<YOUR_DRIVER> # set to `vfio_pci` or `mlx5_core`, depending on your NIC

sudo /usr/local/bin/dpdk-devbind.py --unbind $CU_PLANE_PCI_BUS_ADD
sudo modprobe $DRIVER
sudo /usr/local/bin/dpdk-devbind.py --bind $DRIVER $CU_PLANE_PCI_BUS_ADD
```

##### Bind two VFs

```bash
set -x
U_PLANE_PCI_BUS_ADD=<YOUR_U_PLANE_PCI_BUS_ADDRESS>
C_PLANE_PCI_BUS_ADD=<YOUR_C_PLANE_PCI_BUS_ADDRESS>
DRIVER=<YOUR_DRIVER> # set to `vfio_pci` or `mlx5_core`, depending on your NIC

sudo /usr/local/bin/dpdk-devbind.py --unbind $U_PLANE_PCI_BUS_ADD
sudo /usr/local/bin/dpdk-devbind.py --unbind $C_PLANE_PCI_BUS_ADD
sudo modprobe $DRIVER
sudo /usr/local/bin/dpdk-devbind.py --bind $DRIVER $U_PLANE_PCI_BUS_ADD
sudo /usr/local/bin/dpdk-devbind.py --bind $DRIVER $C_PLANE_PCI_BUS_ADD
```

We recommand to put the above four steps into one script file to quickly repeat them.

<details>
<summary>Example script for Benetel 550-A/650 with Intel X710 on host with two VFs</summary>

```bash
set -x
IF_NAME=eno12409
MAX_RING_BUFFER_SIZE=4096
MTU=9216
DU_U_PLANE_MAC_ADD=00:11:22:33:44:66
DU_C_PLANE_MAC_ADD=00:11:22:33:44:67
VLAN=3
U_PLANE_PCI_BUS_ADD=31:06.0
C_PLANE_PCI_BUS_ADD=31:06.1
DRIVER=vfio_pci

sudo ethtool -G $IF_NAME rx $MAX_RING_BUFFER_SIZE tx $MAX_RING_BUFFER_SIZE
sudo ip link set $IF_NAME mtu $MTU
sudo modprobe iavf
sudo sh -c 'echo 0 > /sys/class/net/$IF_NAME/device/sriov_numvfs'
sudo sh -c 'echo 2 > /sys/class/net/$IF_NAME/device/sriov_numvfs'
sudo ip link set $IF_NAME vf 0 mac $DU_U_PLANE_MAC_ADD vlan $VLAN mtu $MTU spoofchk off # set U plane PCI address
sudo ip link set $IF_NAME vf 1 mac $DU_C_PLANE_MAC_ADD vlan $VLAN mtu $MTU spoofchk off # set C plane PCI address
sleep 1
sudo /usr/local/bin/dpdk-devbind.py --unbind $U_PLANE_PCI_BUS_ADD
sudo /usr/local/bin/dpdk-devbind.py --unbind $C_PLANE_PCI_BUS_ADD
sudo modprobe $DRIVER
sudo /usr/local/bin/dpdk-devbind.py --bind $DRIVER $U_PLANE_PCI_BUS_ADD
sudo /usr/local/bin/dpdk-devbind.py --bind $DRIVER $C_PLANE_PCI_BUS_ADD
```
</details>



### Configure OAI gNB

**Beware in the following section to let in the range of isolated cores the parameters that should be (i.e. `L1s.L1_rx_thread_core`, `L1s.L1_tx_thread_core`, `RUs.ru_thread_core`, `fhi_72.io_core` and `fhi_72.worker_cores`)**

Sample configuration files for OAI gNB, specific to the manufacturer of the radio unit, are available at:
1. LITEON RU:
[`gnb.sa.band78.273prb.fhi72.4x4-liteon.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.273prb.fhi72.4x4-liteon.conf)
2. VVDN RU:
[`gnb.sa.band77.273prb.fhi72.4x4-vvdn.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x4-vvdn.conf)
[`gnb.sa.band77.106prb.fhi72.4x4-vvdn.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.106prb.fhi72.4x4-vvdn.conf)
[`gnb.sa.band77.273prb.fhi72.2x2-vvdn-16b.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.2x2-vvdn-16b.conf)
3. Benetel 650 RU:
[`gnb-du.sa.band77.273prb.fhi72.4x4-benetel650.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band77.273prb.fhi72.4x4-benetel650.conf)
4. Benetel 550 RU:
[`gnb.sa.band77.273prb.fhi72.4x4-benetel550.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x4-benetel550.conf)
[`gnb.sa.band77.273prb.fhi72.4x2-benetel550-dyn-compression.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.273prb.fhi72.4x2-benetel550-dyn-compression.conf)
5. Metanoia RU FR1 (Jura):
[`gnb.sa.band78.273prb.fhi72.4x4-metanoia.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.273prb.fhi72.4x4-metanoia.conf)
6. Metanoia RU FR1 (Cobra):
[`gnb-du.sa.band78.273prb.fhi72.4x4-metanoia-cobra.conf`](../ci-scripts/conf_files/gnb-du.sa.band78.273prb.fhi72.4x4-metanoia-cobra.conf)
7. Metanoia RU FR2 (Cobra):
[`gnb.sa.band258.132prb.fhi72.2x2-metanoia.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band258.132prb.fhi72.2x2-metanoia.conf)
Edit the sample OAI gNB configuration file and check following parameters:

* `gNBs` section
  * The PLMN section shall match the one defined in the AMF
  * `amf_ip_address` shall be the correct AMF IP address in your system
  * `GNB_IPV4_ADDRESS_FOR_NG_AMF` shall match your gNB N2 interface IP address
  * `GNB_IPV4_ADDRESS_FOR_NGU` shall match your gNB N3 interface IP address
  * `prach_ConfigurationIndex`
  * `prach_msg1_FrequencyStart`
  * `ssPBCH_BlockPower` is average EPRE of the resource elements carrying the secondary synchronization signals, expressed in dBm. It can be estimated using the following formula:

    > ssPBCH_BlockPower = P_TX − 10 x log10(N_RE) + 10 x log10(N_antenna_SSB)

    where:
    * `P_TX` is the total transmit power of the RU in dBm (configured on the RU).
    * `N_RE` is the number of resource elements used for the transmission.
    * `N_antenna_SSB` is the number of antenna ports used for SSB transmission (currently, a single antenna port is used for SSB transmission).
  * Adjust the frequency, bandwidth and SSB position

* `L1s` section
  * Set an isolated core for L1 thread `L1_rx_thread_core`, in our environment we are using CPU 8
  * Set an isolated core for L1 thread `L1_tx_thread_core`, in our environment we are using CPU 10
  * `phase_compensation` should be set to 0 to disable when it is performed in the RU and set to 1 when it should be performed on the DU side
  * `tx_amp_backoff_dB` controls the output level of OAI with respect to a full-scale output. The exact value for `tx_amp_backoff_dB` should be obtained from the O-RU documentation. This documentation typically includes detailed sections on Downlink (DL) signal scaling and gain setup. **Warning:** Exceeding the recommended power limits may permanently damage the RU.

* `RUs` section
  * Set an isolated core for RU thread `ru_thread_core`, in our environment we are using CPU 6

* `fhi_72` (FrontHaul Interface) section: this config follows the structure
  that is employed by the xRAN library (`xran_fh_init` and `xran_fh_config`
  structs in the code):
  * `dpdk_devices`: PCI addresses of NIC VFs binded to the DPDK (not the physical NIC but the VFs, use `lspci | grep Virtual`) in the format `{VF-U-plane, VF-C-plane}`;
    if one VF used per RU, U and C planes will share the same VF => depends on the RU capabilities
  * `system_core`: absolute CPU core ID for DPDK control threads, it should be an isolated core, in our environment we are using CPU 0
    (`rte_mp_handle`, `eal-intr-thread`, `iavf-event-thread`)
  * `io_core`: absolute CPU core ID for XRAN library, it should be an isolated core, in our environment we are using CPU 4
  * `worker_cores`: array of absolute CPU core IDs for XRAN library, they should be isolated cores, in our environment we are using CPU 2
  * `ru_addr`: RU U- and C-plane MAC-addresses (format `UU:VV:WW:XX:YY:ZZ`, hexadecimal numbers)
  * `mtu`: Maximum Transmission Unit for the RU, specified by RU vendor; either 1500 or 9600 B (Jumbo Frames); if not set, 1500 is used; if the testbed contains a switch, and its max supported MTU < 9600, then please set the same value in the config file as well
  * `file_prefix` : used to specify a unique prefix for shared memory and files created by multiple DPDK processes; if not set, default value of `wls_0` is used
  * `dpdk_mem_size`: the huge page size that should be pre-allocated by DPDK
    _for NUMA node 0_; by default, this is 8192 MiB (corresponding to 8 huge
    pages à 1024 MiB each, see above). In the current implementation, you
    cannot preallocate memory on NUMA nodes other than 0; in this case, set
    this to 0 (no pre-allocation) and so that DPDK will allocate it on-demand
    on the right NUMA node.
  * `dpdk_iova_mode`: Specifies DPDK IO Virtual Address (IOVA) mode:
    * `PA`: IOVA as Physical Address (PA) mode, where DPDK IOVA memory layout
      corresponds directly to the physical memory layout.
    * `VA`: IOVA as Virtual Address (VA) mode, where DPDK IOVA addresses do not
      follow the physical memory layout. Uses IOMMU to remap physical memory.
      Requires kernel support and IOMMU for address translation.
    * If not specified, default value of "PA" is used (for backwards compabilibity;
      it was hardcoded to PA in the past). However, we recommend using "VA" mode
      as it offers several benefits. For a detailed explanation of DPDK IOVA,
      including the advantages and disadvantages of each mode, refer to
      [Memory in DPDK](https://www.dpdk.org/memory-in-dpdk-part-2-deep-dive-into-iova/)
  * `owdm_enable`: used for eCPRI One-Way Delay Measurements; it depends if the RU supports it; if not set to 1 (enabled), default value is 0 (disabled)
  * `fh_config`
    * `RunSlotPrbMapBySymbol`: enable CP multisection (one symbol per section); default value is 0
    *  DU delay profile (`T1a` and `Ta4`): pairs of numbers `(x, y)` specifying minimum and maximum delays
    * `ru_config`: RU-specific configuration:
      * `comp_hdr_type`: compression header type; `dynamic` or `static` (default)
      * `iq_width`: Width of DL/UL IQ samples: if 16, no compression, if <16, applies
        compression
      * `iq_width_prach`: Width of PRACH IQ samples: if 16, no compression, if <16, applies
        compression
    * `prach_config`: PRACH-specific configuration
      * `eAxC_offset`:  PRACH antenna offset; if not set, default value of `N = max(Nrx,Ntx)` is used
      * `kbar`: the PRACH guard interval, provided in RU
  * `app_id`: `DU` or `RU`. Sets the application `id` value in xRAN. Use the default value: `DU`.

Layer mapping (eAxC offsets) happens as follows:
- For PUSCH/PDSCH, the layers are mapped to `[0,1,...,Nrx-1]/[0,1,...,Ntx-1]` where `Nrx/Ntx` is the
  respective RX/TX number of antennas.
- For PRACH, the layers are mapped to `[No,No+1,...No+Nrx-1]` where `No` is the
  `fhi_72.fh_config.[0].prach_config.eAxC_offset`. xran assumes PRACH offset `No >= max(Nrx,Ntx)`.
  However, we made a workaround that xran supports PRACH eAxC IDs same as PUSCH eAxC IDs. This is achieved with `is_prach` and `filter_id` parameters in the patch.
  Please note that this approach only applies to the RUs that support this functionality, e.g. LITEON RU.

**Note**

- At the moment, OAI is compatible with CAT A O-RU only. Therefore, SRS is not supported.
- XRAN retrieves DU MAC address with `rte_eth_macaddr_get()` function. Hence, `fhi_72.du_addr` parameter is not taken into account.

## Start and Operation of OAI gNB

Run the `nr-softmodem` from the build directory:
```bash
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ./nr-softmodem -O <configuration file> --thread-pool <list of non isolated cpus>
```

**Warning**: Make sure that the configuration file you add after the `-O` option is adapted to your machine, especially to its isolated cores.

**Note**: You may run OAI with O-RAN 7.2 Fronthaul without a RU attached (e.g. for benchmarking).
In such case, you would generate artificial traffic by adding the `--phy-test` option.

You have to set the thread pool option to non-isolated CPUs, since the thread
pool is used for L1 processing which should not interfere with DPDK threads.
For example if you have two NUMA nodes in your system (for example 18 CPUs per
socket) and odd cores are non-isolated, then you can put the thread-pool on
`1,3,5,7,9,11,13,15`. On the other hand, if you have one NUMA node, you can use
either isolated cores or non isolated cores, but make sure that isolated cores
are not the ones defined earlier for DPDK/xran.

<details>
<summary>Once the gNB runs, you should see counters for PDSCH/PUSCH/PRACH per
antenna port, as follows (4x2 configuration):</summary>

```
[NR_PHY]   [o-du 0][rx   24604 pps   24520 kbps  455611][tx  126652 pps  126092 kbps 2250645][Total Msgs_Rcvd 24604]
[NR_PHY]   [o_du0][pusch0   10766 prach0    1536]
[NR_PHY]   [o_du0][pusch1   10766 prach1    1536]
```

</details>

The first line show RX/TX packet counters, i.e., packets received from the RU
(RX), and sent to the RU (TX). In the second and third line, it shows the
counters for the PUSCH and PRACH ports (2 receive antennas, so two counters
each). These numbers should be equal, otherwise it indicates that you don't
receive enough packets on either port.

<details>
<summary>If you see many zeroes, then it means that OAI does not receive
packets on the fronthaul from the RU (RX is almost 0, all PUSCH/PRACH counters
are 0).</summary>

```
[NR_PHY]   [o-du 0][rx       2 pps       0 kbps       0][tx 1020100 pps  127488 kbps 4717971][Total Msgs_Rcvd 2]
[NR_PHY]   [o_du0][pusch0       0 prach0       0]
[NR_PHY]   [o_du0][pusch1       0 prach1       0]
[NR_PHY]   [o_du0][pusch2       0 prach2       0]
[NR_PHY]   [o_du0][pusch3       0 prach3       0]
```

</details>

In this case, please make sure that the O-RU has been configured with the right
ethernet address of the gNB, and has been activated. You might enable port
mirroring at your switch to capture the fronthaul packets: check that you see
(1) packets at all (2) they have the right ethernet address (3) the right VLAN
tag. Although we did not test this, you might make use of the [DPDK packet
capture feature](https://doc.dpdk.org/guides/howto/packet_capture_framework.html)

<details>
<summary>If you see messages about `Received time doesn't correspond to the
time we think it is` or `Jump in frame counter`, the S-plane is not working.</summary>

```
[PHY]   Received Time doesn't correspond to the time we think it is (slot mismatch, received 480.5, expected 475.8)
[PHY]   Received Time doesn't correspond to the time we think it is (frame mismatch, 480.5 , expected 475.5)
[PHY]   Jump in frame counter last_frame 480 => 519, slot 19
[PHY]   Received Time doesn't correspond to the time we think it is (slot mismatch, received 519.19, expected 480.12)
[PHY]   Received Time doesn't correspond to the time we think it is (frame mismatch, 519.19 , expected 480.19)
[PHY]   Received Time doesn't correspond to the time we think it is (slot mismatch, received 520.1, expected 520.0)
```

You can see that the frame numbers jump around, by 5-40 frames (corresponding
to 50-400ms!). This indicates the gNB receives packets on the fronthaul that
don't match its internal time, and the synchronization between gNB and RU is
not working!

</details>

In this case, you should reverify that `ptp4l` and `phc2sys` are working, e.g.,
do not do any jumps (during the last hour). While an occasional jump is not
necessarily problematic for the gNB, many such messages mean that the system is
not working, and UEs might not be able to attach or reach good performance.
Also, you can try to compile with polling (see [the build
section](.#build-oai-gnb)) to see if it resolves the problem.

## Operation with multiple RUs

It is possible to connect up to 4 RUs to one DU at the same time and operate
them either with a single antenna array or a distributed antenna array.  This
works since all RUs and the DU are synchronized onto a common clock using PTP.
The assumed configuration is that with N RUs each having an M×M configuration,
we effectively reach an (N×M)×(N×M) configuration.

Some caveats:
- Even in case of a distributed antenna, this setup will deploy a
  single cell only -- multiple cells on different RUs are not supported.
- All RUs should use the same MTU, so either "normal" (1500 byte) MTU or jumbo
  frames, but not a mix of both.
- We tested only two RUs as of now, i.e., an 8×8 configuration.
- In case of a single array is currently limited to 4 logical antenna ports in DL;
  in UL, up to 8 can be used.

For two RUs using a 8x8 configuration, i.e. a single antenna system, the reference DU configuration file is
[`gnb-du.sa.band77.273prb.fhi72.8x8-benetel650_650.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band77.273prb.fhi72.8x8-benetel650_650.conf).

For two RUs each using a 4x4 configuration, i.e. a distributed antenna system (DAS),
we use the analog beamforming implementation. More details can be found in
[this document](./beamforming.md). It is important to note the following parameters in the configuration file:
- In RU section `nb_tx` and `nb_rx` indicates the total number of physical antenna ports across all distributed RUs.
- `pusch_AntennaPorts`, `pdsch_AntennaPorts_XP * pdsch_AntennaPorts_N1 * pdsch_AntennaPorts_N2` indicate the number of logical antenna ports for one analog beam.
The reference DU configuration file is [`gnb-du.sa.band77.273prb.fhi72.4x4-das-benetel650_650.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band77.273prb.fhi72.4x4-das-benetel650_650.conf).

Next, configure the `fhi_72` section as indicated below:

```
fhi_72 = {
   dpdk_devices = ("ru1_up_vf_pci", "ru1_cp_vf_pci", "ru2_up_vf_pci", "ru2_cp_vf_pci"); # two VFs can be used as well
   // core config as always
   ru_addr = ("ru1_up_mac_addr", "ru1_cp_mac_addr", "ru2_up_mac_addr", "ru2_cp_mac_addr"); # if two VFs, set two RU MAC addresses (one per RU)
   // mtu
   fh_config = (
     {
       // DU delay profile, ru_config, prach_config of RU1
     },
     {
       // DU delay profile, ru_config, prach_config of RU2
     }
  );
};
```

i.e., for `dpdk_devices`, and `ru_addr` is configured for
both RUs in a (flat) array, and the individual radio configuration is given for
each RU individually inside the `fh_config`.

<details>
<summary>Sample FHI 7.2 configuration for two RUs (2 x Benetel 650)</summary>

```
fhi_72 = {
  dpdk_devices = ("0000:01:01.0", "0000:01:01.1", "0000:01:01.2", "0000:01:01.3");
  system_core = 0;
  io_core = 1;
  worker_cores = (2);
  ru_addr = ("8c:1f:64:d1:10:46","8c:1f:64:d1:10:46","8c:1f:64:d1:10:43","8c:1f:64:d1:10:43")
  mtu = 9216;
  fh_config = (
# RAN650 #1
   {
    T1a_cp_dl = (419, 470);
    T1a_cp_ul = (285, 336);
    T1a_up = (294, 345);
    Ta4 = (0, 200);
    ru_config = {
      iq_width = 9;
      iq_width_prach = 9;
    };
  },
# RAN650 #2
  {
    T1a_cp_dl = (419, 470);
    T1a_cp_ul = (285, 336);
    T1a_up = (294, 345);
    Ta4 = (0, 200);
    ru_config = {
      iq_width = 9;
      iq_width_prach = 9;
    };
  });
```
</details>

Afterwards, start the gNB with the modified configuration file. If everything
went well, you should see the RU counters for both RUs go up:

```
[NR_PHY]   [o-du 0][rx   63488 pps   63264 kbps 2759808][tx  127684 pps  127116 kbps 4717971][Total Msgs_Rcvd 63488]
[NR_PHY]   [o_du0][pusch0   14336 prach0    1536]
[NR_PHY]   [o_du0][pusch1   14336 prach1    1536]
[NR_PHY]   [o_du0][pusch2   14336 prach2    1536]
[NR_PHY]   [o_du0][pusch3   14336 prach3    1536]
[NR_PHY]   [o-du 1][rx   63544 pps   63320 kbps 2763240][tx  127684 pps  127116 kbps 4717971][Total Msgs_Rcvd 63544]
[NR_PHY]   [o_du1][pusch0   14350 prach0    1536]
[NR_PHY]   [o_du1][pusch1   14350 prach1    1536]
[NR_PHY]   [o_du1][pusch2   14350 prach2    1536]
[NR_PHY]   [o_du1][pusch3   14350 prach3    1536]
```

You can also verify that there is signal on all RX antennas like so:
```bash
$ cat nrL1_stats.log
[...]
max_IO = 55 (85), min_I0 = 0 (136), avg_I0 = 44 dB(43.44.43.45.44.43.43.45.)
PRACH I0 = 30.6 dB
```

Note the eight entries after `avg_IO`.

You should be able to connect a UE now.


## OAI Management Plane
In OAI gNB, we support:
* Configuration Management: interface(s) creation, configuration of RU CU-planes, Tx/Rx antennas, and Tx/Rx carriers.
* Performance Management: activation/deactivation of available RU performance measurements and its notification reception with 10s periodicity:
```bash
[HW]   [MPLANE] [PM: "192.168.80.9"][RX_ON_TIME  773428][RX_EARLY       0][RX_LATE       0][RX_TOTAL  878881][RX_ON_TIME_C  104744][RX_EARLY_C       0][RX_LATE_C       0][TX_TOTAL  435922]
```

The reference specifications:
* `O-RAN.WG4.MP.0-R004-v16.01`
* `O-RAN.WG4.MP-YANGs-R004-v16.01`

### M-plane prerequisites
Before proceeding, please make sure you have a support for 7.2 interface, as described in [Prerequisites](#prerequisites).

#### DHCP server
The M-plane requires a DHCP server, where the M-plane connection can be established over untagged or tagged VLAN. We tested with untagged (the default VLAN is 1).
Please modify `/etc/dhcp/dhcpd.conf` configuration based on your testbed.

<details>
<summary>Example DHCP server configuration</summary>

```
class "vendor-class" {
  match option vendor-class-identifier;
}
subclass "vendor-class" "o-ran-ru2/Benetel" {
  vendor-option-space VC;
}
option space VC;
option VC.server-address code 129 = array of ip-address;
option VC.server-fqdn code 130 = string;
# Netconf client IP address - DHCP option 43
option VC.server-address 192.168.80.1;
option VC.server-fqdn "o_du_1.operator.com";
set vendor-string = option vendor-class-identifier;
# option 143 - DHCPv4 SZTP Redirect Option (RFC8572)
# 2 bytes of URI's length + URI
option sztp code 143 = { unsigned integer 16, string };
option sztp 15 "https://192.168.80.1";
# port is optional in URI, e.g. "https://192.168.80.1:222"
#option sztp 20 "https://192.168.80.1:2222";
subnet 192.168.80.0 netmask 255.255.255.0 {
  option routers 192.168.80.1;
  option subnet-mask 255.255.255.0;
  option domain-name "oai.com";
  option domain-name-servers 172.21.3.100;
  host benetel_ru {
    # RU MAC address
    hardware ethernet <ru-mac-address>;
    # RU IP address
    fixed-address <desired-ru-ip-address>;
  }
}
```
</details>

Please, configure the interface as:
```bash
sudo ip address add 192.168.80.1/24 dev <interface>
```

#### Mandatory packages
* On Fedora (we haven't yet tested RHEL):
```bash
sudo dnf install pcre-devel libssh-devel libxml2-devel libyang2-devel libnetconf2-devel
```

* On Ubuntu:
```bash
sudo apt-get install libpcre3-dev libssh-dev libxml2-dev
```

On Ubuntu, please note: `sudo apt-get install libyang2-dev libnetconf2-dev` will install unsupported versions (i.e. v2.0.112/v2.0.24 for `libyang2-dev`/`libnetconf2-dev`, but minimum required are v2.1.4/v2.1.25).
Therefore, please compile these libraries from source, as following:

<details>
<summary>Installing latest v2 libyang2 and libnetconf2 libraries</summary>

```
rm -rf /tmp/build_mplane_v2
mkdir /tmp/build_mplane_v2

# libyang
cd /tmp/build_mplane_v2
git clone https://github.com/CESNET/libyang.git
cd libyang
git checkout v2.1.111
mkdir build && cd build
cmake -DENABLE_TESTS=OFF \
      -DENABLE_VALGRIND_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_INSTALL_RPATH=/usr/local/lib \
      -DPLUGINS_DIR=/usr/local/lib/libyang \
      -DPLUGINS_DIR_EXTENSIONS=/usr/local/lib/libyang/extensions \
      -DPLUGINS_DIR_TYPES=/usr/local/lib/libyang/types \
      -DYANG_MODULE_DIR=/usr/local/share/yang/modules/libyang ..
make -j8
sudo make install
sudo ldconfig

#libnetconf
cd /tmp/build_mplane_v2
git clone https://github.com/CESNET/libnetconf2.git
cd libnetconf2
git checkout v2.1.37
mkdir build && cd build
cmake -DENABLE_TESTS=OFF \
      -DENABLE_EXAMPLES=OFF \
      -DENABLE_VALGRIND_TESTS=OFF \
      -DCLIENT_SEARCH_DIR=/usr/local/share/yang/modules \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_INSTALL_RPATH=/usr/local/lib \
      -DLIBYANG_INCLUDE_DIR=/usr/local/include \
      -DLIBYANG_LIBRARY=/usr/local/lib/libyang.so \
      -DLY_VERSION_PATH=/usr/local/include \
      -DYANG_MODULE_DIR=/usr/local/share/yang/modules/libnetconf2 ..
make -j8
sudo make install
sudo ldconfig

# to uninstall libraries
# cd /tmp/build_mplane_v2/libyang/build && sudo make uninstall
# cd /tmp/build_mplane_v2/libnetconf2/build && sudo make uninstall
# cd
# rm -rf /tmp/build_mplane_v2
```
</details>

If you would like to install these libraries in the custom path, please replace `/usr/local` default path to e.g. `/opt/mplane-v2`.

## Benetel O-RU
Note: RAN550-1v2.1.0-M-0820797 has been successfully tested.

We added a [CI M-plane pipeline](https://jenkins-oai.eurecom.fr/job/RAN-SA-FHI72-MPLANE-CN5G/) showcasing the M-plane integration.

#### One time steps
Connect to the RU as user `root`, enable the mplane service, and reboot:
```bash
ssh root@<ru-ip-address>
systemctl enable mplane
reboot
```
Once the mplane service is successfully enabled on the RU, two new users are being added in `/etc/passwd`:
```bash
...
oranbenetel:x:1000:1000::/home/oranbenetel:/bin/sh
oranext:x:1001:1001::/home/oranext:/bin/sh
```
OAI gNB requires the `sudo` access group for NETCONF session. In the case of Benetel O-RUs, the corresponding user is `oranbenetel`. Therefore, please create its home directory:
```bash
mkdir /home/oranbenetel && chown oranbenetel:oranbenetel /home/oranbenetel
```
Connect to the RU as user `oranbenetel`, generate ssh keys, and copy DU public key into RU for NETCONF authentication:
```bash
ssh oranbenetel@<ru-ip-address>
ssh-keygen
echo "<DU-pub-key>" >>  ~/.ssh/authorized_keys
```


### gNB configuration
The reference gNB configuration file for one Benetel RAN550:
[`gnb.sa.band78.273prb.fhi72.4x4-benetel550-mplane.conf`](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.273prb.fhi72.4x4-benetel550-mplane.conf)
The reference DU configuration file for two Benetel RAN650:
[gnb-du.sa.band77.273prb.fhi72.8x8-benetel650_650-mplane.conf](../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band77.273prb.fhi72.8x8-benetel650_650-mplane.conf)

In order to run gNB/DU with M-plane, we need to modify Tx gain `att_tx` in RU section, as well as the `fhi_72` section in the configuration file.

**Important note: Benetel v1.4.1 requires only one VF if the same VLAN tag is used for both C-/U-planes. Otherwise, C-plane will not be properly configured. Benetel v1.2.2 will receive the C-plane packets no matter the DU MAC address value.**
Example for one RU:
```bash
fhi_72 = {
  dpdk_devices = ("0000:c3:11.0", "0000:c3:11.1"); # one VF can be used as well
  system_core = 0;
  io_core = 1;
  worker_cores = (2);
  du_key_pair = ("<path-to>/.ssh/id_rsa.pub", "<path-to>/.ssh/id_rsa");
  du_addr = ("00:11:22:33:44:66", "00:11:22:33:44:67"); # only one needed if one VF configured
  vlan_tag = (9, 9); # only one needed if one VF configured
  ru_username = ("oranbenetel");
  ru_ip_addr = ("192.168.80.9");
  fh_config = ({
    T1a_cp_dl = (419, 470);
    T1a_cp_ul = (285, 336);
    T1a_up = (294, 345);
    Ta4 = (0, 200);
  });
};
```
Example for two RUs:
```bash
fhi_72 = {
  dpdk_devices = ("0000:c3:11.0", "0000:c3:11.1", "0000:c3:11.2", "0000:c3:11.3"); # two VFs can be used as well
  system_core = 0;
  io_core = 1;
  worker_cores = (2);
  du_key_pair = ("/home/oaicicd/.ssh/id_rsa.pub", "/home/oaicicd/.ssh/id_rsa");
  du_addr = ("00:11:22:33:44:66", "00:11:22:33:44:67", "00:11:22:33:44:68", "00:11:22:33:44:69"); # only two needed if two VFs configured
  vlan_tag = (9, 9, 11, 11); # only two needed if two VFs configured
  ru_username = ("oranbenetel", "oranbenetel");
  ru_ip_addr = ("192.168.80.9", "192.168.80.10");
  fh_config = (
# RAN550 #1
  {
    T1a_cp_dl = (419, 470);
    T1a_cp_ul = (285, 336);
    T1a_up = (294, 345);
    Ta4 = (0, 200);
  },
# RAN550 #2
  {
    T1a_cp_dl = (419, 470);
    T1a_cp_ul = (285, 336);
    T1a_up = (294, 345);
    Ta4 = (0, 200);
  });
};
```

* `fhi_72` :
  * `dpdk_devices`: [*]
  * `system_core`: [*]
  * `io_core`: [*]
  * `worker_cores`: [*]
  * `file_prefix`: [*]
  * `du_key_pair`: ssh public and private keys to authenticate RU with NETCONF
  * `du_addr`: DU MAC address(es) to create CU-plane interface(s) in the RU
  * `vlan_tag`: VLAN U and C plane tags to create CU-plane interface(s) in the RU
  * `ru_username`: Username with `sudo` access to connect to the RU via M-plane
  * `ru_ip_addr`: RU IP address to connect to the RU via M-plane
  * `dpdk_mem_size`: [*]
  * `dpdk_iova_mode`: [*]
  * `owdm_enable`: [*]
  * `fh_config`: DU delay profile (`T1a` and `Ta4`), and optionally `ru_config` for IQ bitwidth and compression type (assumed the same for PxSCH/PRACH)
  * `app_id`: [*]

[*] see [Configure OAI gNB](#configure-oai-gnb) for more details

The following parameters are retrieved from the RU and forwarded to the xran:
* `MTU`
* `RU MAC address`
* `IQ compression`: unless explicitely specified via gNB config file, the first <iq-bitwidth> value from <compression-method-supported> node is taken and static compression is set (assumed the same for PxSCH/PRACH)
* `PRACH offset`: hardcoded based on the RU vendor (i.e. for Benetel `max(Nrx,Ntx)`)

### Build and compile gNB
The following cmake options are available:
* `OAI_FHI72` = CUS support
* `OAI_FHI72_MPLANE` = M support

Compiled libraries:
* `OAI_FHI72` <=> `oran_fhlib_5g`
* `OAI_FHI72` && `OAI_FHI72_MPLANE` <=> `oran_fhlib_5g` (CUS) && `oran_fhlib_5g_mplane` (CUSM)

#### Using build_oai script
```bash
git clone https://github.com/duranta-project/openairinterface5g.git ~/openairinterface5g
cd ~/openairinterface5g/cmake_targets/
./build_oai -I  # if you never installed OAI, use this command once before the next line
./build_oai --install-optional-packages  # for pcre/libpcre3, libssh, and libxml2 library installation
./build_oai --gNB --ninja -t oran_fhlib_5g_mplane --cmake-opt -Dxran_LOCATION=$HOME/phy/fhi_lib/lib
# if libyang2 and libnetconf2 are installed in `/opt/mplane-v2`, please use the following command:
PKG_CONFIG_PATH=/opt/mplane-v2/lib/pkgconfig ./build_oai --gNB --ninja -t oran_fhlib_5g_mplane --cmake-opt -Dxran_LOCATION=$HOME/phy/fhi_lib/lib
```

#### Using cmake directly
```bash
git clone https://github.com/duranta-project/openairinterface5g.git ~/openairinterface5g
cd ~/openairinterface5g/
mkdir build && cd build
cmake .. -GNinja -DOAI_FHI72=ON -DOAI_FHI72_MPLANE=ON -Dxran_LOCATION=$HOME/phy/fhi_lib/lib
# if libyang2 and libnetconf2 are installed in `/opt/mplane-v2`, please use the following command:
PKG_CONFIG_PATH=/opt/mplane-v2/lib/pkgconfig cmake .. -GNinja -DOAI_FHI72=ON -DOAI_FHI72_MPLANE=ON -Dxran_LOCATION=$HOME/phy/fhi_lib/lib
ninja nr-softmodem oran_fhlib_5g_mplane params_libconfig
```

Note: instead of setting `xran_LOCATION`, feel free to use `xran_DOWNLOAD` option for automatic xran K release build.

### Start the gNB
Run the `nr-softmodem` from the build directory:
```bash
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ./nr-softmodem -O <mplane-configuration file> --thread-pool <list of non isolated cpus>
```

**Warning**: Make sure that the configuration file you add after the `-O` option is adapted to your machine, especially to its isolated cores.

M-plane sequence diagram:

```mermaid
sequenceDiagram
  participant dhcp as DHCP server
  participant du as O-DU
  participant ru as O-RU

  dhcp->ru: 1. Transport Layer Initialization
  note over dhcp,ru: (a) perform VLAN scan (untagged or tagged VLAN)<br/>(b) DHCP assigns IP address to RU<br/>(c) DHCP sends DU IP address to RU
  
  du->ru: 2. NETCONF/SSH connect

  du->>ru: 3. DU retrieves RU info 
  note left of du: <get>

  note over du: Check if RU is PTP synced
  
  du->>ru: 4. DU subscribes to all RU notifications 
  note left of du: <subscribe>
  
  du->>ru: 5. DU updates the supervision timer
  note left of du: <supervision-watchdog-reset>
  
  note over du: Store RU MAC, MTU, IQ bitwidth, and PRACH offset info for xran
  
  note over du: Store all the RU U-plane info - interface name, TX/RX carrier, and TX/RX endpoint names
  
  du->>ru: 6. DU loads yang models
  note left of du: <get-schema>
  note right of ru: ietf-netconf-monitoring.yang
  
  du->>ru: 7. DU performs CU-plane configuration
  note left of du: <edit-config>
  
  note over ru: (1) o-ran-interface.yang - create new interface with VLAN tag, DU and RU MAC addresses<br/>(2)(opt.) o-ran-transceiver.yang - for file upload<br/>(3) o-ran-processing-elements.yang - element with which RU ports should be assigned<br/>(4) o-ran-uplane-conf.yang<br/>#8193;(4a) low-level-tx/rx-endpoints - PxSCH, PRACH<br/>#8193;(4b) tx/rx-array-carriers - center frequency, BW, gain, ACTIVE,...<br/>#8193;(4c) low-level-tx/rx-links - mapping endpoints, carriers and processing element<br/>#8193;(4d)(opt.) if CAT B, SRS configuration<br/>#8193;(4e)(opt.) TDD configuration
  
  du->>ru: 8. DU checks if CU-plane configuration is valid
  note left of du: <validate>
  
  du->>ru: 9. If valid, DU commits the changes
  note left of du: <commit>
  
  du->>ru: 10. DU retrieves RU states
  note right of ru: ietf-hardware.yang
  note left of du: <get>
  note over ru: admin-state, power-state, oper-state,<br/>availability-state, usage-state
  
  ru->>du: 11. RU notifies DU about the configuration change
  
  note over du: DU configures xran
  
  du->ru: 12. DU activates Performance Measurements

  du->ru: 13. DU and RU exchange packets
```


<details>
<summary>4x4 MIMO and 100MHz BW with Benetel 550 RU v1.2.2 example run</summary>

```
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <connect> with username "oranbenetel" and port ID "830".
[HW]   [MPLANE] Successfuly connected to RU "192.168.80.9" with username "oranbenetel" and port ID "830".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get> operational datastore.
[HW]   [MPLANE] Successfully retrieved operational datastore from RU "192.168.80.9".
[HW]   [MPLANE] RU is in "LOCKED" sync state.
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <subscribe> with stream "NETCONF" and filter "(null)".
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully subscribed to all notifications from RU "192.168.80.9".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = "<supervision-watchdog-reset xmlns="urn:o-ran:supervision:1.0">
<supervision-notification-interval>65535</supervision-notification-interval>
<guard-timer-overhead>65535</guard-timer-overhead>
</supervision-watchdog-reset>".
[HW]   [MPLANE] Successfully updated supervision timer to (65535+65535)[s] for RU "192.168.80.9".
[HW]   [MPLANE] Watchdog timer answer: 
	<next-update-at xmlns="urn:o-ran:supervision:1.0">2025-03-30T08:52:31+02:00</next-update-at>

[HW]   [MPLANE] Interface MTU 1500 unreliable/not correctly reported by Benetel O-RU, hardcoding to 9216.
[HW]   [MPLANE] IQ bitwidth 16 unreliable/not correctly reported by Benetel O-RU, hardcoding to 9.
[HW]   [MPLANE] Storing the following information to forward to xran:
    RU MAC address 8c:1f:64:d1:11:c0
    MTU 9216
    Compression header type static
    IQ bitwidth 9
    PRACH offset 4
    DU port bitmask 61440
    Band sector bitmask 3840
    CC ID bitmask 240
    RU port ID bitmask 15
    DU port ID 0
    Band sector ID 0
    CC ID 0
    RU port ID 0
    max Tx gain 24.0
[HW]   [MPLANE] Successfully retrieved all the U-plane info - interface name, TX/RX carrier names, and TX/RX endpoint names.
[HW]   [MPLANE] Successfully retreived all performance measurement names.
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-yang-metadata".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "yang".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-inet-types".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-yang-types".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-yang-schema-mount".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-yang-structure-ext".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-datastores".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "sysrepo".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-netconf-acm".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-factory-default".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "sysrepo-factory-default".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-yang-library".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "sysrepo-monitoring".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "sysrepo-plugind".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-netconf".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-netconf-with-defaults".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-netconf-notifications".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-origin".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-netconf-monitoring".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "ietf-netconf-nmda".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <get-schema> for module "nc-notifications".
[HW]   [MPLANE] [LIBYANG] ERROR: Data model "notifications" not found in local searchdirs. (path: (null)).
[HW]   [MPLANE] [LIBYANG] ERROR: Loading "notifications" module failed. (path: (null)).
[HW]   [MPLANE] [LIBYANG] ERROR: Parsing module "nc-notifications" failed. (path: (null)).
[HW]   [MPLANE] Unable to load module "nc-notifications" from RU "192.168.80.9".
[HW]   [MPLANE] Unable to load all yang modules from operational datastore for RU "192.168.80.9". Using yang models present in "models" subfolder.
[HW]   [MPLANE] Successfully loaded all yang modules for RU "192.168.80.9".
[HW]   [MPLANE] The VLAN tags for C and U plane for the RU "192.168.80.9" are the same. Therefore, configuring one common interface and one processing element.
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <edit-config>:
<interfaces xmlns="urn:ietf:params:xml:ns:yang:ietf-interfaces">
  <interface>
    <name>INTERFACE_0</name>
    <type xmlns:ianaift="urn:ietf:params:xml:ns:yang:iana-if-type">ianaift:l2vlan</type>
    <enabled>true</enabled>
    <mac-address xmlns="urn:o-ran:interfaces:1.0">8c:1f:64:d1:11:c0</mac-address>
    <base-interface xmlns="urn:o-ran:interfaces:1.0">eth0</base-interface>
    <vlan-id xmlns="urn:o-ran:interfaces:1.0">9</vlan-id>
  </interface>
</interfaces>
<performance-measurement-objects xmlns="urn:o-ran:performance-management:1.0">
  <rx-window-measurement-interval>10</rx-window-measurement-interval>
  <tx-measurement-interval>10</tx-measurement-interval>
  <notification-interval>10</notification-interval>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_TOTAL</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME_C</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY_C</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE_C</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <tx-measurement-objects>
    <measurement-object>TX_TOTAL</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </tx-measurement-objects>
</performance-measurement-objects>
<processing-elements xmlns="urn:o-ran:processing-element:1.0">
  <transport-session-type>ETH-INTERFACE</transport-session-type>
  <ru-elements>
    <name>PLANE_0</name>
    <transport-flow>
      <interface-name>INTERFACE_0</interface-name>
      <eth-flow>
        <ru-mac-address>8c:1f:64:d1:11:c0</ru-mac-address>
        <vlan-id>9</vlan-id>
        <o-du-mac-address>00:11:22:33:44:66</o-du-mac-address>
      </eth-flow>
    </transport-flow>
  </ru-elements>
</processing-elements>
<user-plane-configuration xmlns="urn:o-ran:uplane-conf:1.0">
  <low-level-tx-links>
    <name>PdschLink0</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray0</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint0</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-tx-links>
    <name>PdschLink1</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray0</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint1</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-tx-links>
    <name>PdschLink2</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray0</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint2</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-tx-links>
    <name>PdschLink3</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray0</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint3</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-rx-links>
    <name>PuschLink0</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint0</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink0</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint0</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PuschLink1</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint1</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink1</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint1</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PuschLink2</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint2</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink2</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint2</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PuschLink3</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint3</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink3</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint3</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint0</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>0</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint1</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>1</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint2</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>2</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint3</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>3</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint0</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>0</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint0</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>4</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint1</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>1</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint1</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>5</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint2</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>2</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint2</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>6</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint3</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>3</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint3</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
      <compression-method>BLOCK_FLOATING_POINT</compression-method>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>7</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <tx-array-carriers>
    <name>TxArray0</name>
    <absolute-frequency-center>663360</absolute-frequency-center>
    <center-of-channel-bandwidth>3950400000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <gain>24.0</gain>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
  </tx-array-carriers>
  <rx-array-carriers>
    <name>RxArray0</name>
    <absolute-frequency-center>663360</absolute-frequency-center>
    <center-of-channel-bandwidth>3950400000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
    <gain-correction>0.0</gain-correction>
    <n-ta-offset>0</n-ta-offset>
  </rx-array-carriers>
</user-plane-configuration>

[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully edited the candidate datastore for RU "192.168.80.9".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <validate> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully validated candidate datastore for RU "192.168.80.9".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <commit> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully commited configuration into running datastore for RU "192.168.80.9".
[HW]   [MPLANE] Usage state = "idle" for RU "192.168.80.9".
[HW]   [MPLANE] Received notification from RU "192.168.80.9" at (2025-03-29T12:40:23.049085102+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray0",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.80.9" at (2025-03-29T12:40:23.058136880+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray0",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.80.9" at (2025-03-29T12:40:23.078776163+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray0",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.80.9" at (2025-03-29T12:40:23.093039138+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray0",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.80.9" at (2025-03-29T12:40:23.452751936+00:00)
{
  "ietf-netconf-notifications:netconf-config-change": {
    "changed-by": {
      "username": "root",
      "session-id": 0
    },
    "datastore": "running",
    "edit": [
      {
        "target": "/ietf-interfaces:interfaces/interface[name='INTERFACE_0']",
        "operation": "create"
      },
      {
        "target": "/ietf-interfaces:interfaces/interface[name='INTERFACE_0']/name",
        "operation": "create"
      },
...
[HW]   [MPLANE] RU "192.168.80.9" is now ready.
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <edit-config>:
<performance-measurement-objects xmlns="urn:o-ran:performance-management:1.0">
  <rx-window-measurement-interval>10</rx-window-measurement-interval>
  <tx-measurement-interval>10</tx-measurement-interval>
  <notification-interval>10</notification-interval>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_TOTAL</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME_C</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY_C</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE_C</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <tx-measurement-objects>
    <measurement-object>TX_TOTAL</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </tx-measurement-objects>
</performance-measurement-objects>

[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully edited the candidate datastore for RU "192.168.80.9".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <validate> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully validated candidate datastore for RU "192.168.80.9".
[HW]   [MPLANE] RPC request to RU "192.168.80.9" = <commit> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully commited configuration into running datastore for RU "192.168.80.9".
[HW]   [MPLANE] Sucessfully activated PM after start-up procedure for RU "192.168.80.9".
```
</details>

<details>
<summary>4x4 MIMO and 100MHz BW with Benetel 550 RU v1.4.1 example run</summary>

```
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <connect> with username "oranbenetel" and port ID "830".
[HW]   [MPLANE] Successfuly connected to RU "192.168.81.4" with username "oranbenetel" and port ID "830".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get> operational datastore.
[HW]   [MPLANE] Successfully retrieved operational datastore from RU "192.168.81.4".
[HW]   [MPLANE] RU is already PTP synchronized.
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <subscribe> with stream "NETCONF" and filter "(null)".
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully subscribed to all notifications from RU "192.168.81.4".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = "<supervision-watchdog-reset xmlns="urn:o-ran:supervision:1.0">
<supervision-notification-interval>65535</supervision-notification-interval>
<guard-timer-overhead>65535</guard-timer-overhead>
</supervision-watchdog-reset>".
[HW]   [MPLANE] Successfully updated supervision timer to (65535+65535)[s] for RU "192.168.81.4".
[HW]   [MPLANE] Watchdog timer answer: 
	<next-update-at xmlns="urn:o-ran:supervision:1.0">2025-08-29T06:49:32+02:00</next-update-at>

[HW]   [MPLANE] Interface MTU 1500 unreliable/not correctly reported by Benetel O-RU, hardcoding to 9216.
[HW]   [MPLANE] IQ bitwidth 16 unreliable/not correctly reported by Benetel O-RU, hardcoding to 9.
[HW]   [MPLANE] Storing the following information to forward to xran:
    RU MAC address 70:b3:d5:e1:5b:81
    MTU 9216
    IQ bitwidth 9
    PRACH offset 4
    DU port bitmask 61440
    Band sector bitmask 3840
    CC ID bitmask 240
    RU port ID bitmask 15
    DU port ID 0
    Band sector ID 0
    CC ID 0
    RU port ID 0
    max Tx gain 24.0
[HW]   [MPLANE] Successfully retrieved all the U-plane info - interface name, TX/RX carrier names, and TX/RX endpoint names.
[HW]   [MPLANE] Successfully retreived all performance measurement names.
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-yang-metadata".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "yang".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-inet-types".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-yang-types".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-yang-schema-mount".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-yang-structure-ext".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-datastores".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "sysrepo".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-netconf-acm".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-factory-default".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "sysrepo-factory-default".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-yang-library".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "sysrepo-monitoring".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "sysrepo-plugind".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-netconf".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-netconf-with-defaults".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-netconf-notifications".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-origin".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-netconf-monitoring".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "ietf-netconf-nmda".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <get-schema> for module "nc-notifications".
[HW]   [MPLANE] [LIBYANG] ERROR: Data model "notifications" not found in local searchdirs. (path: (null)).
[HW]   [MPLANE] [LIBYANG] ERROR: Loading "notifications" module failed. (path: (null)).
[HW]   [MPLANE] [LIBYANG] ERROR: Parsing module "nc-notifications" failed. (path: (null)).
[HW]   [MPLANE] Unable to load module "nc-notifications" from RU "192.168.81.4".
[HW]   [MPLANE] Unable to load all yang modules from operational datastore for RU "192.168.81.4". Using yang models present in "models" subfolder.
[HW]   [MPLANE] Successfully loaded all yang modules for RU "192.168.81.4".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <edit-config>:
<interfaces xmlns="urn:ietf:params:xml:ns:yang:ietf-interfaces">
  <interface>
    <name>INTERFACE_0</name>
    <type xmlns:ianaift="urn:ietf:params:xml:ns:yang:iana-if-type">ianaift:l2vlan</type>
    <enabled>true</enabled>
    <mac-address xmlns="urn:o-ran:interfaces:1.0">70:b3:d5:e1:5b:81</mac-address>
    <base-interface xmlns="urn:o-ran:interfaces:1.0">eth0</base-interface>
    <vlan-id xmlns="urn:o-ran:interfaces:1.0">3</vlan-id>
  </interface>
</interfaces>
<performance-measurement-objects xmlns="urn:o-ran:performance-management:1.0">
  <rx-window-measurement-interval>10</rx-window-measurement-interval>
  <tx-measurement-interval>10</tx-measurement-interval>
  <notification-interval>10</notification-interval>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_TOTAL</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME_C</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY_C</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE_C</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <tx-measurement-objects>
    <measurement-object>TX_TOTAL</measurement-object>
    <active>false</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </tx-measurement-objects>
</performance-measurement-objects>
<processing-elements xmlns="urn:o-ran:processing-element:1.0">
  <transport-session-type>ETH-INTERFACE</transport-session-type>
  <ru-elements>
    <name>PLANE_0</name>
    <transport-flow>
      <interface-name>INTERFACE_0</interface-name>
      <eth-flow>
        <ru-mac-address>70:b3:d5:e1:5b:81</ru-mac-address>
        <vlan-id>3</vlan-id>
        <o-du-mac-address>00:11:22:33:44:66</o-du-mac-address>
      </eth-flow>
    </transport-flow>
  </ru-elements>
</processing-elements>
<user-plane-configuration xmlns="urn:o-ran:uplane-conf:1.0">
  <low-level-tx-links>
    <name>PdschLink0</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray0</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint0</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-tx-links>
    <name>PdschLink1</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray1</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint1</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-tx-links>
    <name>PdschLink2</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray2</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint2</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-tx-links>
    <name>PdschLink3</name>
    <processing-element>PLANE_0</processing-element>
    <tx-array-carrier>TxArray3</tx-array-carrier>
    <low-level-tx-endpoint>LowLevelTxEndpoint3</low-level-tx-endpoint>
  </low-level-tx-links>
  <low-level-rx-links>
    <name>PuschLink0</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint0</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink0</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray0</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint0</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PuschLink1</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray1</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint1</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink1</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray1</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint1</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PuschLink2</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray2</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint2</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink2</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray2</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint2</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PuschLink3</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray3</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxEndpoint3</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-rx-links>
    <name>PrachLink3</name>
    <processing-element>PLANE_0</processing-element>
    <rx-array-carrier>RxArray3</rx-array-carrier>
    <low-level-rx-endpoint>LowLevelRxPrachEndpoint3</low-level-rx-endpoint>
  </low-level-rx-links>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint0</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>0</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint1</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>1</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint2</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>2</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-tx-endpoints>
    <name>LowLevelTxEndpoint3</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>3</eaxc-id>
    </e-axcid>
  </low-level-tx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint0</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>0</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint0</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>4</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint1</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>1</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint1</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>5</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint2</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>2</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint2</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>6</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxEndpoint3</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>3</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <low-level-rx-endpoints>
    <name>LowLevelRxPrachEndpoint3</name>
    <compression>
      <iq-bitwidth>9</iq-bitwidth>
      <compression-type>STATIC</compression-type>
    </compression>
    <frame-structure>193</frame-structure>
    <cp-type>NORMAL</cp-type>
    <cp-length>352</cp-length>
    <cp-length-other>288</cp-length-other>
    <offset-to-absolute-frequency-center>0</offset-to-absolute-frequency-center>
    <ul-fft-sampling-offsets>
      <scs>KHZ_30</scs>
      <ul-fft-sampling-offset>0</ul-fft-sampling-offset>
    </ul-fft-sampling-offsets>
    <e-axcid>
      <o-du-port-bitmask>61440</o-du-port-bitmask>
      <band-sector-bitmask>3840</band-sector-bitmask>
      <ccid-bitmask>240</ccid-bitmask>
      <ru-port-bitmask>15</ru-port-bitmask>
      <eaxc-id>7</eaxc-id>
    </e-axcid>
    <non-time-managed-delay-enabled>true</non-time-managed-delay-enabled>
  </low-level-rx-endpoints>
  <tx-array-carriers>
    <name>TxArray0</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <gain>24.0</gain>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
  </tx-array-carriers>
  <tx-array-carriers>
    <name>TxArray1</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <gain>24.0</gain>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
  </tx-array-carriers>
  <tx-array-carriers>
    <name>TxArray2</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <gain>24.0</gain>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
  </tx-array-carriers>
  <tx-array-carriers>
    <name>TxArray3</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <gain>24.0</gain>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
  </tx-array-carriers>
  <rx-array-carriers>
    <name>RxArray0</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
    <gain-correction>0.0</gain-correction>
    <n-ta-offset>0</n-ta-offset>
  </rx-array-carriers>
  <rx-array-carriers>
    <name>RxArray1</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
    <gain-correction>0.0</gain-correction>
    <n-ta-offset>0</n-ta-offset>
  </rx-array-carriers>
  <rx-array-carriers>
    <name>RxArray2</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
    <gain-correction>0.0</gain-correction>
    <n-ta-offset>0</n-ta-offset>
  </rx-array-carriers>
  <rx-array-carriers>
    <name>RxArray3</name>
    <absolute-frequency-center>623334</absolute-frequency-center>
    <center-of-channel-bandwidth>3350010000</center-of-channel-bandwidth>
    <channel-bandwidth>100000000</channel-bandwidth>
    <active>ACTIVE</active>
    <downlink-radio-frame-offset>0</downlink-radio-frame-offset>
    <downlink-sfn-offset>0</downlink-sfn-offset>
    <gain-correction>0.0</gain-correction>
    <n-ta-offset>0</n-ta-offset>
  </rx-array-carriers>
</user-plane-configuration>

[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully edited the candidate datastore for RU "192.168.81.4".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <validate> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully validated candidate datastore for RU "192.168.81.4".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <commit> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully commited configuration into running datastore for RU "192.168.81.4".
[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.335228846+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray0",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.368360865+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray1",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.414703766+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray2",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.452921618+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray3",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.471419021+00:00)
{
  "ietf-netconf-notifications:netconf-config-change": {
    "changed-by": {
      "username": "root",
      "session-id": 0
    },
    "datastore": "running",
    "edit": [
      {
        "target": "/ietf-interfaces:interfaces/interface[name='INTERFACE_0']",
        "operation": "create"
      },
      {
        "target": "/ietf-interfaces:interfaces/interface[name='INTERFACE_0']/name",
        "operation": "create"
      },
...
[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.678251110+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray0",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.715498360+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray1",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.830268973+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray2",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:19.853387657+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray3",
        "state": "BUSY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.515262267+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray0",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.535692962+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray1",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.557423764+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray2",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.579066867+00:00)
{
  "o-ran-uplane-conf:rx-array-carriers-state-change": {
    "rx-array-carriers": [
      {
        "name": "RxArray3",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.604192995+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray0",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.629830498+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray1",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.651062685+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray2",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] Received notification from RU "192.168.81.4" at (2025-08-28T10:37:24.672865767+00:00)
{
  "o-ran-uplane-conf:tx-array-carriers-state-change": {
    "tx-array-carriers": [
      {
        "name": "TxArray3",
        "state": "READY"
      }
    ]
  }
}

[HW]   [MPLANE] RU "192.168.81.4" is now ready.
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <edit-config>:
<performance-measurement-objects xmlns="urn:o-ran:performance-management:1.0">
  <rx-window-measurement-interval>10</rx-window-measurement-interval>
  <tx-measurement-interval>10</tx-measurement-interval>
  <notification-interval>10</notification-interval>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_TOTAL</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_ON_TIME_C</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_EARLY_C</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <rx-window-measurement-objects>
    <measurement-object>RX_LATE_C</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </rx-window-measurement-objects>
  <tx-measurement-objects>
    <measurement-object>TX_TOTAL</measurement-object>
    <active>true</active>
    <object-unit>RU</object-unit>
    <report-info>COUNT</report-info>
  </tx-measurement-objects>
</performance-measurement-objects>

[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully edited the candidate datastore for RU "192.168.81.4".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <validate> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully validated candidate datastore for RU "192.168.81.4".
[HW]   [MPLANE] RPC request to RU "192.168.81.4" = <commit> candidate datastore.
[HW]   [MPLANE] RPC reply = OK.
[HW]   [MPLANE] Successfully commited configuration into running datastore for RU "192.168.81.4".
[HW]   [MPLANE] Sucessfully activated PM after start-up procedure for RU "192.168.81.4".
```
</details>

Note: If you wish to run the fronthaul without M-plane, no need for recompilation, as the library `oran_fhlib_5g` already exists.
The only mandatory step is to link `oran_fhlib_5g` to `oai_transpro` library.
```bash
cd ~/openairinterface5g/cmake_targets/ran_build/build
rm liboai_transpro.so
ln -s liboran_fhlib_5g.so liboai_transpro.so
sudo ./nr-softmodem -O <without-mplane-configuration file> --thread-pool <list of non isolated cpus>
```


## Contact in case of questions

You can ask your question on the [mailing lists](https://github.com/duranta-project/openairinterface5g/wiki/MailingList).

Your email should contain below information:

- A clear subject in your email.
- For all the queries there should be [Query\] in the subject of the email and for problems there should be [Problem\].
- In case of a problem, add a small description.
- Do not share any screenshots/photos unless you want to share a diagram.
- OAI gNB/DU/CU/CU-CP/CU-UP configuration file in `.conf` format only.
- Logs of OAI gNB/DU/CU/CU-CP/CU-UP in `.log` or `.txt` format only.
- RU Vendor and Version.
- In case your question is related to performance, include a small description of the machine (CPU, RAM and networking card) and diagram of your testing environment. 
- If you have any issues related to PTP or synchronization, then first check the section "Debugging PTP issues". Then share your problem with PTP version you are using, switch details and master clock.
- Known/open issues are present on [Github](https://github.com/duranta-project/openairinterface5g/issues), so keep checking.
