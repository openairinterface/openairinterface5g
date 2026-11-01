# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(e1ap_lib_test e1ap_lib_test.c)
add_dependencies(tests e1ap_lib_test)
add_test(NAME e1ap_lib_test COMMAND e1ap_lib_test)
target_link_libraries(e1ap_lib_test PRIVATE e1ap_lib SIMU m)
target_link_libraries(e1ap_lib_test PRIVATE asn1_e1ap)
target_include_directories(e1ap_lib_test PRIVATE
                           ${CMAKE_SOURCE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}
)
