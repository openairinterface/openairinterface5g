# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_nr_csi_im test_nr_csi_im.c)
target_link_libraries(test_nr_csi_im PRIVATE
  -Wl,--start-group PHY_NR_UE PHY_NR_COMMON -Wl,--end-group
  ds minimal_lib m)
add_dependencies(tests test_nr_csi_im)
add_test(NAME test_nr_csi_im COMMAND test_nr_csi_im)
