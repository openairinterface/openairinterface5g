# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(xn-simulator-test xn-simulator.c xn-ho-sim.c
               ${OPENAIR2_DIR}/GNB_APP/gnb_config_common.c
               ${OPENAIR2_DIR}/GNB_APP/gnb_config_ng.c
               ${OPENAIR2_DIR}/GNB_APP/gnb_config_xn.c)
target_link_libraries(xn-simulator-test PRIVATE
		      CONFIG_LIB ITTI SCTP_CLIENT
		      f1ap xnap_sim ngap SECURITY
		      softmodem_common)
target_link_libraries(xn-simulator-test PRIVATE asn1_xnap asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)
# UE stub of the Xn handover test: NR UE NAS, as in nr-ue-nas-simulator
target_link_libraries(xn-simulator-test PRIVATE nr_nas nr_common lib_uicc usim_lib SECURITY GTPV1U UTIL)
target_compile_definitions(xn-simulator-test PRIVATE OAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_dependencies(xn-simulator-test params_libconfig)
target_include_directories(xn-simulator-test PRIVATE
                           ${CMAKE_SOURCE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR})
