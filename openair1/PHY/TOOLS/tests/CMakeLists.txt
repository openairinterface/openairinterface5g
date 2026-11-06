# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_signal_energy test_signal_energy.cpp ../signal_energy.c)
target_link_libraries(test_signal_energy PRIVATE GTest::gtest UTIL)
add_dependencies(tests test_signal_energy)
add_test(NAME test_signal_energy
  COMMAND ./test_signal_energy)

add_executable(benchmark_rotate_vector benchmark_rotate_vector.cpp)
target_link_libraries(benchmark_rotate_vector PRIVATE benchmark::benchmark UTIL PHY_NR)
add_dependencies(tests benchmark_rotate_vector)
add_test(NAME benchmark_rotate_vector
  COMMAND ./benchmark_rotate_vector)

add_executable(test_rotate_add_cpx_vector test_rotate_add_cpx_vector.cpp)
target_link_libraries(test_rotate_add_cpx_vector PRIVATE GTest::gtest benchmark::benchmark UTIL PHY_NR)
add_dependencies(tests test_rotate_add_cpx_vector)
add_test(NAME test_rotate_add_cpx_vector
  COMMAND ./test_rotate_add_cpx_vector)

add_executable(benchmark_dot_product benchmark_dot_product.cpp)
target_link_libraries(benchmark_dot_product PRIVATE benchmark::benchmark UTIL PHY_NR)
add_dependencies(tests benchmark_dot_product)
add_test(NAME benchmark_dot_product
  COMMAND ./benchmark_dot_product)

add_executable(test_log2_approx test_log2_approx.cpp ../log2_approx.c)
target_link_libraries(test_log2_approx PRIVATE GTest::gtest LOG minimal_lib)
add_dependencies(tests test_log2_approx)
add_test(NAME test_log2_approx
  COMMAND ./test_log2_approx --gtest_filter=-log2_approx.complete)

add_executable(dft_test test_dft.c ../dfts_load.c)
target_link_libraries(dft_test minimal_lib shlib_loader SIMU m)
add_dependencies(tests dft_test)
add_dependencies(dft_test dfts) # trigger build of dfts (shared lib for DFT)
add_test(NAME dft_test COMMAND ./dft_test)
set_tests_properties(dft_test PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}")

add_executable(test_dft_saturation test_dft_saturation.c ../dfts_load.c)
target_link_libraries(test_dft_saturation minimal_lib shlib_loader m)
add_dependencies(tests test_dft_saturation)
add_dependencies(test_dft_saturation dfts)
add_test(NAME test_dft_saturation COMMAND ./test_dft_saturation)
set_tests_properties(test_dft_saturation PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}")

add_executable(test_vector_op test_vector_op.cpp)
target_link_libraries(test_vector_op PRIVATE LOG minimal_lib)
add_dependencies(tests test_vector_op)
add_test(NAME test_vector_op
  COMMAND test_vector_op)

add_executable(test_oai_arith_operations test_oai_arith_operations.cpp ../oai_arith_operations.c)
target_link_libraries(test_oai_arith_operations PRIVATE GTest::gtest LOG minimal_lib)
add_dependencies(tests test_oai_arith_operations)
add_test(NAME test_oai_arith_operations
  COMMAND ./test_oai_arith_operations)

add_executable(test_sse_intrinsics test_sse_intrinsics.cpp)
target_link_libraries(test_sse_intrinsics PRIVATE GTest::gtest LOG minimal_lib)
add_dependencies(tests test_sse_intrinsics)
add_test(NAME test_sse_intrinsics
  COMMAND ./test_sse_intrinsics)

add_executable(benchmark_channel_pipeline benchmark_channel_pipeline.cpp test_channel_pipeline_tools.c)
target_link_libraries(benchmark_channel_pipeline PRIVATE UTIL SIMU PHY_COMMON LOG CONFIG_LIB shlib_loader m channel_pipeline benchmark::benchmark thread-pool)

add_executable(test_channel_pipeline test_channel_pipeline.cpp test_channel_pipeline_tools.c)
target_link_libraries(test_channel_pipeline PRIVATE UTIL SIMU PHY_COMMON LOG CONFIG_LIB shlib_loader m channel_pipeline GTest::gtest thread-pool)

add_executable(accuracy_test_gpu_optimized_pipeline accuracy_test_gpu_optimized_pipeline.cpp test_channel_pipeline_tools.c)
target_link_libraries(accuracy_test_gpu_optimized_pipeline PRIVATE UTIL SIMU PHY_COMMON LOG CONFIG_LIB shlib_loader m channel_pipeline GTest::gtest thread-pool)

add_dependencies(tests test_channel_pipeline)
add_test(NAME test_channel_pipeline COMMAND test_channel_pipeline)
if (ENABLE_CHANNEL_SIM_CUDA)
  target_compile_definitions(test_channel_pipeline PRIVATE CHANNEL_SIM_CUDA)
  set_tests_properties(test_channel_pipeline PROPERTIES LABELS "cuda")

  target_compile_definitions(benchmark_channel_pipeline PRIVATE CHANNEL_SIM_CUDA)
  add_dependencies(tests benchmark_channel_pipeline)
  add_test(NAME benchmark_channel_pipeline COMMAND benchmark_channel_pipeline)
  set_tests_properties(benchmark_channel_pipeline PROPERTIES LABELS "cuda")

  target_compile_definitions(accuracy_test_gpu_optimized_pipeline PRIVATE CHANNEL_SIM_CUDA)
  add_dependencies(tests accuracy_test_gpu_optimized_pipeline)
  add_test(NAME accuracy_test_gpu_optimized_pipeline COMMAND accuracy_test_gpu_optimized_pipeline)
  set_tests_properties(accuracy_test_gpu_optimized_pipeline PROPERTIES LABELS "cuda")
endif()

if (ENABLE_CHANNEL_SIM_CUDA)
add_executable(test_multipath test_multipath.c)
target_link_libraries(test_multipath PRIVATE UTIL SIMU LOG CONFIG_LIB shlib_loader m oai_cuda_lib)

add_dependencies(tests test_multipath)
add_test(NAME test_multipath COMMAND test_multipath)
set_tests_properties(test_multipath PROPERTIES LABELS "cuda")

add_executable(test_noise test_noise.c)
target_link_libraries(test_noise PRIVATE UTIL SIMU PHY_COMMON LOG CONFIG_LIB shlib_loader m oai_cuda_lib)

add_dependencies(tests test_noise)
add_test(NAME test_noise COMMAND test_noise)
set_tests_properties(test_noise PROPERTIES LABELS "cuda")

add_executable(test_channel_simulation test_channel_simulation.c)
target_link_libraries(test_channel_simulation PRIVATE UTIL SIMU PHY_COMMON LOG CONFIG_LIB shlib_loader m oai_cuda_lib)

add_dependencies(tests test_channel_simulation)
add_test(NAME test_channel_simulation COMMAND test_channel_simulation)

add_executable(test_channel_scalability test_channel_scalability.c)
target_link_libraries(test_channel_scalability PRIVATE UTIL SIMU PHY_COMMON LOG CONFIG_LIB shlib_loader m oai_cuda_lib)

add_dependencies(tests test_channel_scalability)
add_test(NAME test_channel_scalability COMMAND test_channel_scalability)

if (TARGET oai_cuda_lib)
    target_compile_definitions(test_multipath PRIVATE CHANNEL_SIM_CUDA)
    target_compile_definitions(test_noise PRIVATE CHANNEL_SIM_CUDA)
    target_compile_definitions(test_channel_simulation PRIVATE CHANNEL_SIM_CUDA)
    target_compile_definitions(test_channel_scalability PRIVATE CHANNEL_SIM_CUDA)
endif()
endif()
