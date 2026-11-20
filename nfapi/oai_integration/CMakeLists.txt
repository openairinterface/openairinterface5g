# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_subdirectory(aerial)
if(OAI_WLS)
    add_subdirectory(wls_integration)
else()
    message(STATUS "No Support for WLS") # Not WLS, means its SOCKET-based comms, maybe add a flag specific for UDP
    add_subdirectory(socket)
endif()

# Shared: nFAPI mode management + PNF (Temporary until PNF separation)
add_library(nfapi_user_lib
        nfapi.c
        nfapi_pnf.c)
if(OAI_WLS)
    target_compile_definitions(nfapi_user_lib PRIVATE ENABLE_WLS)
elseif(OAI_AERIAL)
    target_compile_definitions(nfapi_user_lib PRIVATE ENABLE_AERIAL)
else()
    target_compile_definitions(nfapi_user_lib PRIVATE ENABLE_SOCKET)
endif()
target_link_libraries(nfapi_user_lib PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs UTIL)
target_link_libraries(nfapi_user_lib PRIVATE nr_fapi_p7)
target_link_libraries(nfapi_user_lib PRIVATE nfapi_pnf_lib)
if(OAI_WLS)
    target_link_libraries(nfapi_user_lib PRIVATE wls_integration_lib)
elseif(OAI_AERIAL)
    target_link_libraries(nfapi_user_lib PRIVATE aerial_lib)
endif()
target_include_directories(nfapi_user_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# LTE VNF OAI integration
add_library(nfapi_lte_user_lib
        vnf/nfapi_lte_vnf.c)
target_link_libraries(nfapi_lte_user_lib PRIVATE asn1_lte_rrc_hdrs UTIL)
target_link_libraries(nfapi_lte_user_lib PUBLIC nfapi_user_lib nfapi_lte_vnf_lib)

# NR VNF OAI integration
add_library(nfapi_nr_user_lib
        vnf/nfapi_nr_vnf.c)
if(OAI_WLS)
    target_compile_definitions(nfapi_nr_user_lib PRIVATE ENABLE_WLS)
elseif(OAI_AERIAL)
    target_compile_definitions(nfapi_nr_user_lib PRIVATE ENABLE_AERIAL)
else()
    target_compile_definitions(nfapi_nr_user_lib PRIVATE ENABLE_SOCKET)
endif()
target_link_libraries(nfapi_nr_user_lib PRIVATE asn1_nr_rrc_hdrs UTIL nr_fapi_p7)
target_link_libraries(nfapi_nr_user_lib PUBLIC nfapi_user_lib nfapi_nr_vnf_lib)
if(OAI_WLS)
    target_link_libraries(nfapi_nr_user_lib PRIVATE wls_integration_lib)
endif()
