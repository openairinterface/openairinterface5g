# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_write_reorder test_write_reorder.cpp)
target_link_libraries(test_write_reorder PRIVATE radio_common minimal_lib GTest::gtest)
add_dependencies(tests test_write_reorder)
add_test(NAME test_write_reorder COMMAND ./test_write_reorder)
