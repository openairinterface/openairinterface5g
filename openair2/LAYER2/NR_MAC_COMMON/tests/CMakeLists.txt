add_executable(test_lut_dmrs test_lut_dmrs.c)
target_link_libraries(test_lut_dmrs PRIVATE
    MAC_NR_COMMON
    nr_common
    asn1_nr_rrc_hdrs
    minimal_lib m)
add_dependencies(tests test_lut_dmrs)
add_test(NAME test_lut_dmrs COMMAND test_lut_dmrs)

add_executable(test_periodic_occasion test_periodic_occasion.c)
target_link_libraries(test_periodic_occasion PRIVATE
    MAC_NR_COMMON
    nr_common
    asn1_nr_rrc_hdrs
    minimal_lib m)
add_dependencies(tests test_periodic_occasion)
add_test(NAME test_periodic_occasion COMMAND test_periodic_occasion)

