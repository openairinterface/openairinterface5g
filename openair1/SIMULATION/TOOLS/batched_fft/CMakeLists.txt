# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# Limited float32 FFT: one transform per SIMD lane (see batched_fft.h). The kernels are NEON on
# aarch64 and AVX2 (simde) elsewhere.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  add_library(batched_fft batched_fft.c batched_fft_neon.c)
else()
  add_library(batched_fft batched_fft.c batched_fft_avx2.c)
endif()
target_include_directories(batched_fft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(batched_fft PRIVATE m)

if (ENABLE_TESTS)
  add_subdirectory(tests)
endif()
