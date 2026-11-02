# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_executable(test_batched_fft test_batched_fft.cpp)
target_link_libraries(test_batched_fft PRIVATE batched_fft GTest::gtest)
add_dependencies(tests test_batched_fft)
add_test(NAME test_batched_fft COMMAND test_batched_fft)

add_executable(benchmark_batched_fft benchmark_batched_fft.cpp)
target_link_libraries(benchmark_batched_fft PRIVATE batched_fft benchmark::benchmark)
