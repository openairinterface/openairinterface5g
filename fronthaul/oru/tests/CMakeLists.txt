# SPDX-License-Identifier: LicenseRef-CSSL-1.0

set(DPDK_TEST_ARGS "--no-huge" "--iova-mode=va" "--no-pci" "-m" "1024")

add_executable(test_oru_io test_oru_io.c)
target_link_libraries(test_oru_io PRIVATE oru_io PkgConfig::dpdk)
add_dependencies(tests test_oru_io)
add_test(NAME test_oru_io COMMAND test_oru_io ${DPDK_TEST_ARGS} "--file-prefix=test_oru_io" "--vdev=net_null0" "--vdev=net_null1")
set_property(TEST test_oru_io PROPERTY LABELS fronthaul)

add_executable(test_oru_packet_processor test_oru_packet_processor.c)
target_link_libraries(test_oru_packet_processor PRIVATE oru_packet_processor fh_compression log_headers PkgConfig::dpdk)
target_include_directories(test_oru_packet_processor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../xran_pkt)
add_dependencies(tests test_oru_packet_processor)
add_test(NAME test_oru_packet_processor COMMAND test_oru_packet_processor ${DPDK_TEST_ARGS} "--file-prefix=test_oru_packet_processor" "--vdev=net_null0" "--vdev=net_null1")
set_property(TEST test_oru_packet_processor PROPERTY LABELS fronthaul)

add_executable(test_fh_compression test_fh_compression.c)
target_link_libraries(test_fh_compression PRIVATE fh_compression)
add_dependencies(tests test_fh_compression)
add_test(NAME test_fh_compression COMMAND test_fh_compression)
set_property(TEST test_fh_compression PROPERTY LABELS fronthaul)

add_executable(test_oru_beamforming test_oru_beamforming.cpp)
target_link_libraries(test_oru_beamforming PRIVATE oru_beamforming log_headers GTest::gtest)
add_dependencies(tests test_oru_beamforming)
add_test(NAME test_oru_beamforming COMMAND test_oru_beamforming)
set_property(TEST test_oru_beamforming PROPERTY LABELS fronthaul)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)

add_executable(test_oru_pcap test_oru_pcap.c)
add_dependencies(tests test_oru_pcap)
target_link_libraries(test_oru_pcap PRIVATE oru_packet_processor log_headers PkgConfig::dpdk ${PCAP_LIBRARIES} xran_pkt)
target_include_directories(test_oru_pcap PRIVATE ${PCAP_INCLUDE_DIRS})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/run_oru_pcap_test.sh DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME test_oru_pcap_1 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_oru_pcap_test.sh https://github.com/bpodrygajlo/openairinterface5g/releases/download/fhi72-pcaps/capture-oai-odu-4x4-273prb-truncated.pcap.xz $<TARGET_FILE:test_oru_pcap> 1 1090 3 1 6 4 5 9600 4 -- ${DPDK_TEST_ARGS} "--file-prefix=test_oru_pcap_1")
set_property(TEST test_oru_pcap_1 PROPERTY LABELS fronthaul)

add_test(NAME test_oru_pcap_frag COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_oru_pcap_test.sh https://github.com/bpodrygajlo/openairinterface5g/releases/download/fhi72-pcaps/capture-oai-odu-4x4-273prb-truncated.pcap.xz $<TARGET_FILE:test_oru_pcap> 1 1090 3 1 6 4 5 1500 4 -- ${DPDK_TEST_ARGS} "--file-prefix=test_oru_pcap_frag")
set_property(TEST test_oru_pcap_frag PROPERTY LABELS fronthaul)

add_test(NAME test_oru_pcap_ext1 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_oru_pcap_test.sh https://github.com/ConnorBlasie/openairinterface5g/releases/download/ext1-pcaps/capture-xran-odu-catb-4x32-273prb-ext1.pcap.xz $<TARGET_FILE:test_oru_pcap> 1 262 3 1 6 4 5 9600 4 32 384 --xran-ext1-reference 384 3072 -- ${DPDK_TEST_ARGS} "--file-prefix=test_oru_pcap_ext1")
set_property(TEST test_oru_pcap_ext1 PROPERTY LABELS fronthaul)

add_test(NAME test_oru_pcap_ext11 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_oru_pcap_test.sh https://github.com/ConnorBlasie/openairinterface5g/releases/download/ext11-pcaps/capture-xran-odu-catb-4x32-273prb-ext11.pcap.xz $<TARGET_FILE:test_oru_pcap> 1 262 3 1 6 4 5 9600 4 32 0 --xran-ext11-reference 384 3072 -- ${DPDK_TEST_ARGS} "--file-prefix=test_oru_pcap_ext11")
set_property(TEST test_oru_pcap_ext11 PROPERTY LABELS fronthaul)

add_test(NAME test_oru_pcap_beamids COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_oru_pcap_test.sh https://github.com/bpodrygajlo/openairinterface5g/releases/download/fhi72-pcaps-gnb-sa-band257-132prb-2x2/fronthaul.pcap.xz $<TARGET_FILE:test_oru_pcap> 3 1146820 3 1 10 2 5 9216 0 -- ${DPDK_TEST_ARGS} "--file-prefix=test_oru_pcap_beamids")
set_property(TEST test_oru_pcap_beamids PROPERTY LABELS fronthaul)

add_dependencies(tests test_oru_pcap)

add_executable(test_oru_fh test_oru_fh.c)
target_link_libraries(test_oru_fh PRIVATE oru_fh xran_pkt PkgConfig::dpdk)
add_dependencies(tests test_oru_fh)
add_test(NAME test_oru_fh COMMAND test_oru_fh ${DPDK_TEST_ARGS} "--file-prefix=test_oru_fh" "--vdev=net_null0" "--vdev=net_null1")
set_property(TEST test_oru_fh PROPERTY LABELS fronthaul)
