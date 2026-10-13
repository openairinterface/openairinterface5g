/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef PHY_GPU_COMPAT_H
#define PHY_GPU_COMPAT_H

/*
  gpu_compat.h  --  C-compatible CUDA/HIP runtime abstraction

  Compile with:
    NVCC  -> CUDA
    HIPCC -> HIP

  No C++ features used.
*/

#if defined(__HIPCC__) || defined(__HIP_PLATFORM_AMD__) || defined(__HIP_PLATFORM_HCC__)
  #define GPU_USE_HIP 1
#else //if defined(__CUDACC__)
  #define GPU_USE_CUDA 1
//#else
//  #error "Compile with NVCC or HIPCC."
#endif


/* ================= Backend Includes ================= */

#if defined(GPU_USE_HIP)
  #include <hip/hip_runtime.h>
  #include <hip/hip_runtime_api.h>
#elif defined(GPU_USE_CUDA)
  #include <cuda_runtime.h>
  #include <cuda_runtime_api.h>
#endif


/* ================= Type Abstraction ================= */

#if defined(GPU_USE_HIP)

typedef hipError_t    gpuError_t;
typedef hipStream_t   gpuStream_t;
typedef hipEvent_t    gpuEvent_t;

#define gpuSuccess hipSuccess

#define gpuMemcpyHostToDevice   hipMemcpyHostToDevice
#define gpuMemcpyDeviceToHost   hipMemcpyDeviceToHost

#elif defined(GPU_USE_CUDA)

typedef cudaError_t    gpuError_t;
typedef cudaStream_t   gpuStream_t;
typedef cudaEvent_t    gpuEvent_t;

#define gpuSuccess cudaSuccess

#define gpuMemcpyHostToDevice   cudaMemcpyHostToDevice
#define gpuMemcpyDeviceToHost   cudaMemcpyDeviceToHost

#endif


/* ================= Error Handling ================= */

#include <stdio.h>
#include <stdlib.h>

#if defined(GPU_USE_HIP)
  #define gpuGetErrorName     hipGetErrorName
  #define gpuGetErrorString   hipGetErrorString
  #define gpuGetLastError     hipGetLastError
  #define gpuDeviceSynchronize hipDeviceSynchronize
  #define gpuPeekAtLastError  hipPeekAtLastError
#elif defined(GPU_USE_CUDA)
  #define gpuGetErrorName     cudaGetErrorName
  #define gpuGetErrorString   cudaGetErrorString
  #define gpuGetLastError     cudaGetLastError
  #define gpuDeviceSynchronize cudaDeviceSynchronize
  #define gpuPeekAtLastError  cudaPeekAtLastError
#endif

/* ================= Runtime API Mapping ================= */

#if defined(GPU_USE_HIP)

#define gpuGetDeviceProperties hipGetDeviceProperties

#define gpuMalloc              hipMalloc
#define gpuFree                hipFree
#define gpuMemset              hipMemset
#define gpuMemsetAsync         hipMemsetAsync
#define gpuHostAlloc           hipHostAlloc
#define gpuHostAllocMapped     hipHostAllocMapped
#define gpuHostAllocDefault    hipHostAllocDefault
#define gpuHostAllocPortable   hipHostAllocPortable
#define gpuFreeHost            hipFreeHost
#define gpuHostGetDevicePointer hipHostGetDevicePointer

#define gpuMemcpy              hipMemcpy
#define gpuMemcpyAsync         hipMemcpyAsync

#define gpuDeviceProp_t        hipDeviceProp_t
#define gpuDeviceGetAttribute  hipDeviceGetAttribute
#define gpuDevAttrManagedMemory hipDeviceAttributeManagedMemory
#define gpuDevAttrConcurrentManagedAccess hipDeviceAttributeConcurrentManagedAccess
#define gpuDevAttrUnifiedAddressing hipDeviceAttributeUnifiedAddressing
#define gpuDevAttrPageableMemoryAccess hipDeviceAttributePageableMemoryAccess
#define gpuDevAttrPageableMemoryAccessUsesHostPageTables hipDeviceAttributePageableMemoryAccessUsesHostPageTables
#define gpuDevAttrHostRegisterSupported hipDeviceAttributeHostRegisterSupported
#define gpuDevAttrIntegrated hipDeviceAttributeIntegrated


#define gpuStreamDestroy       hipStreamDestroy
#define gpuStreamSynchronize   hipStreamSynchronize
#define gpuStreamNonBlocking   hipStreamNonBlocking
#define gpuStreamCreateWithFlags        hipStreamCreateWithFlags
#define gpuStreamCreate        hipStreamCreate
#define gpuStreamWaitEvent     hipStreamWaitEvent
#define gpuStreamBeginCapture  hipStreamBeginCapture
#define gpuStreamEndCapture    hipStreamEndCapture
#define gpuStreamCaptureModeThreadLocal hipStreamCaptureModeThreadLocal

#define gpuEventRecord         hipEventRecord
#define gpuEventCreate         hipEventCreate
#define gpuEventCreateWithFlags hipEventCreateWithFlags
#define gpuEventDisableTiming  hipEventDisableTiming
#define gpuEventDestroy        hipEventDestroy
#define gpuEventSynchronize    hipEventSynchronize

#define gpuSetDeviceFlags      hipSetDeviceFlags
#define gpuDeviceScheduleSpin  hipDeviceScheduleSpin
#define gpuDeviceScheduleYield hipDeviceScheduleYield
#define gpuDeviceScheduleBlockingSync hipDeviceScheduleBlockingSync

#define gpuGraph_t             hipGraph_t
#define gpuGraphExec_t         hipGraphExec_t
#define gpuGraphInstantiate    hipGraphInstantiate
#define gpuGraphInstantiateWithFlags hipGraphInstantiateWithFlags
#define gpuGraphUpload         hipGraphUpload
#define gpuGraphLaunch         hipGraphLaunch
#define gpuGraphDestroy        hipGraphDestroy
#define gpuGraphExecDestroy    hipGraphExecDestroy

#elif defined(GPU_USE_CUDA)

#define gpuGetDeviceProperties cudaGetDeviceProperties

#define gpuDeviceProp_t        struct cudaDeviceProp
#define gpuDeviceGetAttribute  cudaDeviceGetAttribute
#define gpuDevAttrManagedMemory cudaDevAttrManagedMemory
#define gpuDevAttrConcurrentManagedAccess cudaDevAttrConcurrentManagedAccess
#define gpuDevAttrUnifiedAddressing cudaDevAttrUnifiedAddressing
#define gpuDevAttrPageableMemoryAccess cudaDevAttrPageableMemoryAccess
#define gpuDevAttrPageableMemoryAccessUsesHostPageTables cudaDevAttrPageableMemoryAccessUsesHostPageTables
#define gpuDevAttrHostRegisterSupported cudaDevAttrHostRegisterSupported
#define gpuDevAttrIntegrated   cudaDevAttrIntegrated

#define gpuMalloc              cudaMalloc
#define gpuFree                cudaFree
#define gpuMemset              cudaMemset
#define gpuMemsetAsync         cudaMemsetAsync
#define gpuHostAlloc           cudaHostAlloc
#define gpuHostAllocMapped     cudaHostAllocMapped
#define gpuHostAllocDefault    cudaHostAllocDefault
#define gpuHostAllocPortable   cudaHostAllocPortable
#define gpuFreeHost            cudaFreeHost
#define gpuHostGetDevicePointer cudaHostGetDevicePointer

#define gpuMemcpy              cudaMemcpy
#define gpuMemcpyAsync         cudaMemcpyAsync

#define gpuStreamCreateWithFlags        cudaStreamCreateWithFlags
#define gpuStreamCreate        cudaStreamCreate
#define gpuStreamWaitEvent     cudaStreamWaitEvent
#define gpuStreamDestroy       cudaStreamDestroy
#define gpuStreamSynchronize   cudaStreamSynchronize
#define gpuStreamNonBlocking   cudaStreamNonBlocking
#define gpuStreamBeginCapture  cudaStreamBeginCapture
#define gpuStreamEndCapture    cudaStreamEndCapture
#define gpuStreamCaptureModeThreadLocal cudaStreamCaptureModeThreadLocal

#define gpuEventRecord         cudaEventRecord
#define gpuEventCreate         cudaEventCreate
#define gpuEventCreateWithFlags cudaEventCreateWithFlags
#define gpuEventDisableTiming  cudaEventDisableTiming
#define gpuEventDestroy        cudaEventDestroy
#define gpuEventSynchronize    cudaEventSynchronize

#define gpuSetDeviceFlags      cudaSetDeviceFlags
#define gpuDeviceScheduleSpin  cudaDeviceScheduleSpin
#define gpuDeviceScheduleYield cudaDeviceScheduleYield
#define gpuDeviceScheduleBlockingSync cudaDeviceScheduleBlockingSync

#define gpuGraph_t             cudaGraph_t
#define gpuGraphExec_t         cudaGraphExec_t
#define gpuGraphInstantiate    cudaGraphInstantiate
#define gpuGraphInstantiateWithFlags cudaGraphInstantiateWithFlags
#define gpuGraphUpload         cudaGraphUpload
#define gpuGraphLaunch         cudaGraphLaunch
#define gpuGraphDestroy        cudaGraphDestroy
#define gpuGraphExecDestroy    cudaGraphExecDestroy
#endif

#endif /* PHY_GPU_COMPAT_H */
