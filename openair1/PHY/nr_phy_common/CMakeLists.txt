# SPDX-License-Identifier: LicenseRef-CSSL-1.0

set(NR_PHY_COMMON_SRC src/nr_phy_common.c
                      src/nr_channel_compensation.c
                      src/nr_compute_llr.c
                      src/nr_layer_demapping.c)

add_library(nr_phy_common ${NR_PHY_COMMON_SRC})
target_link_libraries(nr_phy_common PRIVATE UTIL PHY_COMMON m)
target_include_directories(nr_phy_common PUBLIC inc/)

# same as nr_phy_common but limited to 128-bit SIMD, to test the non-AVX2 paths on x86
add_library(nr_phy_common_no_avx_256 ${NR_PHY_COMMON_SRC})
target_link_libraries(nr_phy_common_no_avx_256 PRIVATE UTIL PHY_COMMON m)
target_include_directories(nr_phy_common_no_avx_256 PUBLIC inc/)
target_compile_definitions(nr_phy_common_no_avx_256 PRIVATE USE_128BIT)

add_library(nr_ue_phy_meas src/nr_ue_phy_meas.c)
target_include_directories(nr_ue_phy_meas PUBLIC inc/)
target_link_libraries(nr_ue_phy_meas PUBLIC utils)

if (ENABLE_TESTS)
  add_subdirectory(tests)
endif()
