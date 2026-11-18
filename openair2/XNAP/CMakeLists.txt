# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_subdirectory(lib)
add_subdirectory(MESSAGES)
add_subdirectory(tests/xn-simulator)

add_library(xnap xnap_common.c xnap_gNB.c xnap_gNB_itti_messaging.c xnap_gNB_handlers.c xnap_gNB_encoder.c xnap_ids.c)
target_link_libraries(xnap PUBLIC asn1_xnap xnap_lib)
target_link_libraries(xnap PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs log_headers)

# Separate build of the XNAP sources for xn-simulator-test only: overrides
# NUMBER_OF_gNB_MAX so the simulator can hold multiple XnAP instances in one
# process, without affecting the real gNB build (which links "xnap" above).
add_library(xnap_sim xnap_common.c xnap_gNB.c xnap_gNB_itti_messaging.c xnap_gNB_handlers.c xnap_gNB_encoder.c xnap_ids.c)
target_link_libraries(xnap_sim PUBLIC asn1_xnap xnap_lib)
target_link_libraries(xnap_sim PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs log_headers)
target_compile_definitions(xnap_sim PRIVATE NUMBER_OF_gNB_MAX=8)

if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()



