# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_nr_layer_demapping test_layer_demapping.cpp)
target_link_libraries(test_nr_layer_demapping PRIVATE nr_phy_common GTest::gtest minimal_lib)
add_dependencies(tests test_nr_layer_demapping)
add_test(NAME test_nr_layer_demapping COMMAND ./test_nr_layer_demapping)

add_executable(test_llr test_llr.cpp)
target_link_libraries(test_llr PRIVATE PHY_NR SIMU GTest::gtest)
add_dependencies(tests test_llr)
add_test(NAME test_llr
  COMMAND ./test_llr)

add_executable(test_llr_no_avx_256 test_llr.cpp)
target_link_libraries(test_llr_no_avx_256 PRIVATE PHY_NR_NO_AVX_256 SIMU GTest::gtest)
add_dependencies(tests test_llr_no_avx_256)
add_test(NAME test_llr_no_avx_256
  COMMAND ./test_llr_no_avx_256)

add_executable(test_nr_est_delay test_nr_est_delay.c ../../TOOLS/dfts_load.c)
target_link_libraries(test_nr_est_delay PRIVATE PHY_NR SIMU minimal_lib shlib_loader)
add_dependencies(tests test_nr_est_delay)
add_dependencies(test_nr_est_delay dfts)
add_test(NAME test_nr_est_delay COMMAND ./test_nr_est_delay)
set_tests_properties(test_nr_est_delay PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}")
