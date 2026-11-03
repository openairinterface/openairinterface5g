<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# OAI nrLDPC GPU Decoder Tutorial

This is a brief overview on the LDPC CUDA accelerator and how to run

[[_TOC_]]

## Supported Configurations

| Parameter | CPU Baseline  | GPU Implementation |
| --- | --- | --- |
| **Base Graph (BG)** | 1, 2 | **1** |
| **Lifting Size ($Z_c$)** | All  | **$\ge 128$** |
| **Code Rate (R)** | BG1: 1/3, 2/3, 8/9 <br><br> BG2: 1/5, 1/3, 2/3 | **BG1: 1/3, 2/3, 8/9** |

## Prerequisites

### Tested System Configuration

The GPU decoder has been successfully compiled and verified using the following baseline hardware and software environment:

* **NVIDIA Driver:** 575.57.08
* **CUDA Toolkit:** 12.9
* **CMake:** 3.22.1
* **GCC (C/C++ Compiler):** 12.3.0

**Other Validated Platforms**
Beyond the Grace Hopper architecture, the implementation has also been
successfully validated across a range of NVIDIA ecosystems, including:

* NVIDIA Jetson
* NVIDIA GH200
* NVIDIA L40S
* NVIDIA DGX Spark(GB10)
* NVIDIA A100

### Monitoring & Profiling

To examine runtime details, hardware utilization, and kernel execution
efficiency, we recommend using the following official NVIDIA developer tools:

* **`nvidia-smi`**: Use this for real-time monitoring of VRAM usage, overall
  GPU utilization, and power consumption during the end-to-end tests.
* **`nsys` (Nsight Systems)**: Use this for system-wide performance analysis.
  It is highly effective for observing the CUDA Graph execution timeline and
  CPU-GPU pipeline scheduling behaviors.
* **`ncu` (Nsight Compute)**: Use this for deep, kernel-level profiling. It
  helps verify SM occupancy, shared memory usage, and the execution efficiency
  of the SIMD4 vectorized PTX instructions.

## AMD GPU Support (HIP / ROCm)

The same decoder sources can be compiled for **AMD GPUs** with the ROCm/HIP toolchain (`hipcc`) instead of NVIDIA's `nvcc`. This is achieved with a thin, compile-time compatibility layer — no separate code path is maintained:

* `openair1/PHY/gpu_compat.h` — maps the CUDA runtime API onto HIP (`cudaMalloc` → `gpuMalloc` → `hipMalloc`, streams, events, graphs, error handling, …).
* `openair1/PHY/gpu_simd_intrin_compat.h` — provides the DP4A-style SIMD intrinsics (`__vminu4`, `__vabs4`, `__vcmpeq4`, …) as portable `gpu_v*` helpers with CUDA, HIP-device, and host fallbacks.
* `openair1/PHY/gpu_alloc.h` — the pinned, device-mapped host allocations used by the LDPC_CUDA-guarded host code.

The backend is selected at compile time from the compiler's own macros, and CMake picks the toolchain: with `ENABLE_LDPC_CUDA=ON`, `LDPC_GPU_BACKEND` defaults to HIP if a HIP compiler is installed and to CUDA otherwise. Set `-DLDPC_GPU_BACKEND=CUDA` or `=HIP` to force one when both are present. The build produces the **same plugin** `libldpc_cuda.so` with the **same `_cuda` shlibversion**, so every `ldpctest`, `nr_dlsim` and `nr_ulsim` command below is **identical** regardless of whether the plugin was built with CUDA or HIP.

### Tested AMD System Configuration

The HIP backend has been successfully compiled and verified on:

* **GPU:** AMD Radeon 8060S (Ryzen AI MAX+ 395, `gfx1151`)
* **ROCm:** 7.2.4 (`hipcc` / AMD clang 22)
* **CMake:** 3.28.3
* **GCC (host C/C++ compiler):** 12.3.0

Other AMD targets can be added via the GPU architecture list in `openair1/PHY/CODING/nrLDPC_coding/nrLDPC_coding_cuda/CMakeLists.txt` (`HIP_ARCHITECTURES`, e.g. `gfx90a` for MI210, `gfx1151` for Ryzen AI MAX+).

### Monitoring & Profiling (ROCm)

The AMD equivalents of the NVIDIA tools above are:

* **`rocm-smi`**: real-time monitoring of VRAM usage, GPU utilization and power (equivalent to `nvidia-smi`).
* **`rocprofv3` / `rocprof`**: system-wide and kernel-level profiling, including HIP Graph execution timelines (equivalent to `nsys` / `ncu`).

## Library Parameters

The library is selected with `--loader.ldpc.shlibversion _cuda`. The following optional parameters of the
`nrLDPC_coding_cuda` section can be set on the command line of the PHY simulators, nr-softmodem and
nr-uesoftmodem, or in a configuration file:

- `nrLDPC_coding_cuda.num_contexts` - number of decoder contexts, i.e. transport blocks decoded concurrently, each
  with its own buffers and CUDA stream (1 to 8, default *4*).

- `nrLDPC_coding_cuda.crc_check_interval` - early termination: number of decoder iterations between two checks of
  the code block CRCs (1 to 64, default *2*). A code block that passes its CRC is not processed any further, and the
  decoding stops when all code blocks passed or the maximum number of iterations is reached, so the error rate does
  not depend on this value, only the decoding time.

- `nrLDPC_coding_cuda.crc_check` - where the code block CRCs are checked during decoding: *gpu* (default) or *host*.
  The host check, with a round trip to the host every `crc_check_interval` iterations, is slower; it is meant for
  comparison and debugging.

- `nrLDPC_coding_cuda.wait_mode` - how a host thread waits for the GPU: *spin*, *yield* (default) or *block*.
  Blocking costs a wake-up latency at least once per transport block (about 100 us on a DGX Spark). This setting
  applies to the whole process, i.e. also to other CUDA code running in it.

`ldpctest` uses the code block decoder (`LDPCdecoder_cuda()`), to which only `wait_mode` applies.

Example:
```
nrLDPC_coding_cuda : {
  num_contexts : 8;
};

loader : {
  ldpc : {
    shlibversion : "_cuda";
  };
};
```

## Unit Test

### Build

Use `cmake` to compile with CUDA support from the root directory:

```bash
cmake -B build/ -G Ninja -DENABLE_LDPC_CUDA=ON -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc -DENABLE_PHYSIM_TESTS=ON
ninja -C build/
ctest --test-dir build/ -R ldpc_cuda
```

#### Building for AMD GPUs (HIP / ROCm)

Nothing changes on the command line: on a machine with ROCm installed and no
CUDA, the same invocation builds the plugin with `hipcc`.

```bash
./build_oai --ninja --phy_simulators --build-lib ldpc_cuda
```

Equivalently, invoking CMake directly:

```bash
cmake <src_dir> -GNinja -DENABLE_PHYSIM_TESTS=ON -DENABLE_LDPC_CUDA=ON
ninja ldpc_cuda ldpctest nr_dlsim nr_ulsim
```

`cmake` reports which backend it picked (`LDPC GPU offload: selected the HIP
backend`). The ROCm installation prefix is derived from the HIP compiler, so
`CMAKE_PREFIX_PATH` does not have to be set. Add `-DLDPC_GPU_BACKEND=CUDA` on a
machine that has both toolchains and should build for NVIDIA.

The resulting `libldpc_cuda.so` links against `libamdhip64.so`; verify with `ldd libldpc_cuda.so`. From here on, all test commands are the same as for the CUDA build (the plugin keeps the `_cuda` shlibversion).

### ldpctest

`ldpctest` is the foundational tool for evaluating the performance and correctness of the LDPC encoder/decoder.

**Basic Parameters:**
* `-r`: Nominator rate. Supported values: 1, 2, 22. (Default: 1)
* `-d`: Denominator rate. Supported values: 3, 5, 25. (Default: 3)
* `-n`: Number of trials (e.g., `-n300` runs 300 trials).
* `-S`: Number of segments (determines the workload size).
* `-s`: Starting SNR in dBm.
* `-i`: Maximum number of iterations. (Note: Due to internal index offset
  implementation, the parameter `-i4` executes 5 decoding iterations.)
* `-v _cuda `: Enable GPU decoder (loads `libldpc_cuda.so`, whether built with CUDA or HIP). Omit this parameter for CPU baseline.

#### Latency & Baseline Test

To compare the single-segment processing capability between CPU and GPU, set the segments parameter to 1 (`-S1`).

**BG1 Rate 1/3:**

* CPU: `./ldpctest -r1 -n300 -S1 -s4 -i4`
* GPU: `./ldpctest -r1 -n300 -S1 -s4 -i4 -v _cuda`

**BG1 Rate 2/3:**

* CPU: `./ldpctest -r2 -n300 -S1 -s4 -i4`
* GPU: `./ldpctest -r2 -n300 -S1 -s4 -i4 -v _cuda`

**BG1 Rate 8/9:**

* CPU: `./ldpctest -r22 -d25 -n300 -S1 -s4 -i4`
* GPU: `./ldpctest -r22 -d25 -n300 -S1 -s4 -i4 -v _cuda`

#### Maximum Throughput Test

To evaluate the maximum throughput of the GPU, increase the `-S` parameter to
add parallel decoding workloads. The current GPU version in `ldpctest` supports
a maximum of **128** segments.

Run the following commands to benchmark the maximum GPU throughput:

```bash
# BG1 R13 Max Throughput
./ldpctest -r1 -n30 -S128 -s4 -i4 -v _cuda
# BG1 R23 Max Throughput
./ldpctest -r2 -n30 -S128 -s4 -i4 -v _cuda
# BG1 R89 Max Throughput
./ldpctest -r22 -d25 -n30 -S128 -s4 -i4 -v _cuda
```

### Downlink Simulator (dlsim)

`nr_dlsim` is used to simulate the physical downlink shared channel (PDSCH). To
offload LDPC decoding to the GPU, you must specify the GPU shared library.

* `--loader.ldpc.shlibversion _cuda`: Directs the dynamic loader to use the GPU-accelerated LDPC library (`libldpc_cuda.so`, CUDA or HIP build).

Unlike `ldpctest` where you can manually set the number of segments, in
`nr_dlsim`, the number of segments (Code Blocks) is determined dynamically by the
Transport Block Size (TBS). The TBS is defined by a combination of the MCS
index (`-e`), the number of allocated PRBs (`-R`, `-b`), and the number of MIMO
layers (`-x`). To specify the iterations, use `-I`.

Understanding the resulting segment count is crucial for evaluating GPU
performance, as higher segment counts better utilize the GPU's parallel
processing capabilities.

**1. Target: ~ 9 Segments (Rate 1/3)**
Using MCS 13 with 273 PRBs and a single layer generates a workload of
approximately 9 segments per slot.

```bash
./nr_dlsim -n100 -s20 -e13 -R273 -b273 -x1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda
```

**2. Target: ~ 18 Segments (Rate 2/3)**
Increasing the MCS to 22 while keeping 273 PRBs and 1 layer doubles the
throughput requirement, resulting in about 18 segments.

```bash
./nr_dlsim -n100 -s20 -e22 -R273 -b273 -x1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda
```

**3. Target: ~ 50 Segments (Rate 8/9, High Workload)**
Using MCS 27 combined with 2x2 MIMO (`-x2 -y2 -z2`) forces a massive Transport
Block Size, pushing the workload to approximately 50 segments. This scenario
highly benefits from the Node-Based GPU architecture.

```bash
./nr_dlsim -n100 -s40 -e27 -R273 -b273 -z2 -x2 -y2 -P --loader.ldpc.shlibversion _cuda
```

Note: The `-P` flag enables performance printing, allowing you to observe the
decoding time per slot.

### Uplink Simulator (ulsim)

`nr_ulsim` is used to simulate the physical uplink shared channel (PUSCH). The
GPU acceleration logic follows the same architectural pattern as `dlsim`.

> **Parameter Mapping Notice:**
> When transitioning from `nr_dlsim` to `nr_ulsim`, the command-line arguments
> for identical physical layer parameters are different:
> * MCS Index: Use `-m` (instead of `-e`).
> * Allocated PRBs: Use `-r` (instead of `-b`).
> * MIMO Layers: Use `-W` (instead of `-x`).

To properly observe the GPU performance, we use the `-P` flag to print the
execution statistics. After running a test, locate the `ULSCH segments decoding
time` field in the `gNB RX` section of the output log. This metric isolates the
time spent executing the LDPC decoding iterations.

**1. Target: ~ 9 Segments (Rate 1/3)**
Using MCS 13 with 273 PRBs and a single layer.

```bash
./nr_ulsim -n100 -s20 -m13 -R273 -r273 -W1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda
```

**2. Target: ~ 18 Segments (Rate 2/3)**
Increasing to MCS 22, maintaining 273 PRBs and a single layer.

```bash
./nr_ulsim -n100 -s20 -m22 -R273 -r273 -W1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda
```

**3. Target: ~ 50 Segments (Rate 8/9, Massive Workload)**
Using MCS 27 with 273 PRBs and 2x2 MIMO (`-W2 -y2 -z2`).
```bash
./nr_ulsim -n100 -s40 -m27 -R273 -r273 -W2 -y2 -z2 -P --loader.ldpc.shlibversion _cuda
```

## E2E Test

For instructions on running an end-to-end test, please refer to [the
corresponding tutorial](../../../../../doc/NR_SA_Tutorial_OAI_nrUE_GPU_offload.md).

