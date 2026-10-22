# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_library(xran_pkt STATIC xran_pkt_api.c xran_pkt_ext.c xran_pkt_bfw.c)
target_include_directories(xran_pkt PUBLIC ./)
target_compile_options(xran_pkt PRIVATE ${dpdk_CFLAGS})
target_link_libraries(xran_pkt PRIVATE PkgConfig::dpdk fh_compression)

if (ENABLE_TESTS)
    add_subdirectory(tests)
endif()
