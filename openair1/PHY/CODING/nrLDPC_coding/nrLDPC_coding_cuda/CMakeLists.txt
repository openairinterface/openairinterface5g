# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# The same sources build the plugin for either GPU backend: PHY/gpu_compat.h
# and PHY/gpu_simd_intrin_compat.h translate the CUDA runtime API and the
# DP4A-style intrinsics at compile time. LDPC_GPU_BACKEND (top-level
# CMakeLists.txt) says which compiler the .cu kernels go through; the library
# is called libldpc_cuda.so either way, so --loader.ldpc.shlibversion _cuda and
# the physim tests below are unchanged.
add_library(ldpc_cuda MODULE
            # main entry
            nrLDPC_coding_cuda.c
            nrLDPC_coding_cuda_config.c

            nrLDPC_decoder_cuda.c
            ldpc_encoder_cuda32.c

            nrLDPC_coding_segment_encoder_cuda.c
            nrLDPC_coding_segment_decoder_cuda.c
)

set(LDPC_GPU_KERNELS
    nrLDPC_decoder_BG1_cuda.cu
    ldpc_BG1_Zc384_32bit.cu
    ldpc_BG1_Zc352_32bit.cu
    ldpc_BG1_Zc320_32bit.cu
    ldpc_BG1_Zc288_32bit.cu
    ldpc_BG1_Zc256_32bit.cu
    ldpc_BG1_Zc240_32bit.cu
    ldpc_BG1_Zc224_32bit.cu
    ldpc_BG1_Zc208_32bit.cu
    ldpc_BG1_Zc192_32bit.cu
    ldpc_BG1_Zc176_32bit.cu
    ldpc_input.cu
    ldpc_deinterleave.cu
    ldpc_rate_matching.cu
)
target_sources(ldpc_cuda PRIVATE ${LDPC_GPU_KERNELS})

if (LDPC_GPU_BACKEND STREQUAL "HIP")

  enable_language(HIP)
  # the kernels keep their .cu extension, so say what to compile them as
  set_source_files_properties(${LDPC_GPU_KERNELS} PROPERTIES LANGUAGE HIP)
  # MI210 = gfx90a, Ryzen AI MAX+ 395 = gfx1151
  set_target_properties(ldpc_cuda PROPERTIES HIP_ARCHITECTURES "gfx90a;gfx1151")
  # hip::device adds the include directories, libamdhip64 and the platform
  # defines; the .c sources of this module include gpu_compat.h as well
  target_link_libraries(ldpc_cuda PRIVATE hip::device)

else()

  enable_language(CUDA)

  # it would be possible to auto-detect compute capailities: https://stackoverflow.com/a/68223399
  #include(FindCUDA/select_compute_arch)
  #CUDA_DETECT_INSTALLED_GPUS(INSTALLED_GPU_CCS_1)

  # Nvidia recommends to build against all targeted archs: https://forums.developer.nvidia.com/t/261162
  # this is for cmake 3.16 (OAI minimum), starting at cmake 3.18 there is another syntax, see below

  SET(CUDA_VERBOSE_BUILD ON)

  # version using NVLink-C2C in Grace Hopper
  # sm_121 (DGX Spark, GB10) needs a recent toolkit (CUDA 12.9): add it only if nvcc supports it, else GB10 runs the
  # compute_90 PTX, compiled by the driver at load time
  set(LDPC_CUDA_ARCHITECTURES 80 87 90)
  execute_process(COMMAND ${CMAKE_CUDA_COMPILER} --list-gpu-arch OUTPUT_VARIABLE NVCC_GPU_ARCHS ERROR_QUIET)
  if(NVCC_GPU_ARCHS MATCHES "compute_121")
    list(APPEND LDPC_CUDA_ARCHITECTURES 121)
  endif()
  message(STATUS "ldpc_cuda: CUDA architectures ${LDPC_CUDA_ARCHITECTURES}")
  set_target_properties(ldpc_cuda PROPERTIES CUDA_ARCHITECTURES "${LDPC_CUDA_ARCHITECTURES}")
  # we need to find the right include directory, but ti seems that
  # enable_language(CUDA) does not make this available(?), so also use the CUDA package
  find_package(CUDA REQUIRED)
  target_include_directories(ldpc_cuda PRIVATE ${CUDA_INCLUDE_DIRS})
  set_target_properties(ldpc_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

endif()

target_link_libraries(ldpc_cuda PRIVATE log_headers)
set_target_properties(ldpc_cuda PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_dependencies(ldpctest ldpc_cuda)
add_dependencies(nr-softmodem ldpc_cuda)
add_dependencies(nr-uesoftmodem ldpc_cuda)
add_dependencies(nr_ulsim ldpc_cuda)
add_dependencies(nr_ulschsim ldpc_cuda)
add_dependencies(nr_dlsim ldpc_cuda)
add_dependencies(nr_dlschsim ldpc_cuda)

if(ENABLE_PHYSIM_TESTS)

  # this is a list of physical simulators designed to test the functioning of
  # ldpc_cuda. Of course, one could also modify
  # openair1/SIMULATION/tests/CMakeLists.txt to run them with ldpc_cuda

  add_physim_test(ldpctest.ldpc_cuda.simple "ldpctest: simple CUDA load check" ldpctest -v _cuda -l8448 -s10 -n300)
  add_physim_test(ldpctest.ldpc_cuda.r13 "ldpctest: base graph 1, rate 1/3" ldpctest -v _cuda -r1 -n300 -S64 -s4 -i4)
  add_physim_test(ldpctest.ldpc_cuda.r23 "ldpctest: base graph 1, rate 2/3" ldpctest -v _cuda -r2 -n300 -S64 -s5 -i4)
  add_physim_test(ldpctest.ldpc_cuda.r89 "ldpctest: base graph 1, rate 8/9" ldpctest -v _cuda -r22 -d25 -n300 -S64 -s6 -i4)

  # one test per BG1 lifting size a parity check kernel is generated for. The
  # block lengths are those of physim.5g.ldpctest.test1..10, which cover Zc=176
  # to 384; without these the offload is only ever exercised at Zc=384, since
  # the tests above and all the nr_dlsim/nr_ulsim ones below land on 8448/384.
  add_physim_test(ldpctest.ldpc_cuda.zc176 "ldpctest: base graph 1, Zc = 176 (block length 3872)" ldpctest -v _cuda -l3872 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc192 "ldpctest: base graph 1, Zc = 192 (block length 4224)" ldpctest -v _cuda -l4224 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc208 "ldpctest: base graph 1, Zc = 208 (block length 4576)" ldpctest -v _cuda -l4576 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc224 "ldpctest: base graph 1, Zc = 224 (block length 4928)" ldpctest -v _cuda -l4928 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc240 "ldpctest: base graph 1, Zc = 240 (block length 5280)" ldpctest -v _cuda -l5280 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc256 "ldpctest: base graph 1, Zc = 256 (block length 5632)" ldpctest -v _cuda -l5632 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc288 "ldpctest: base graph 1, Zc = 288 (block length 6336)" ldpctest -v _cuda -l6336 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc320 "ldpctest: base graph 1, Zc = 320 (block length 7040)" ldpctest -v _cuda -l7040 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc352 "ldpctest: base graph 1, Zc = 352 (block length 7744)" ldpctest -v _cuda -l7744 -n300 -S64 -s10)
  add_physim_test(ldpctest.ldpc_cuda.zc384 "ldpctest: base graph 1, Zc = 384 (block length 8448)" ldpctest -v _cuda -l8448 -n300 -S64 -s10)

  add_physim_test(nr_dlsim.ldpc_cuda.9seg_r13 "nr_dlsim: 9 segments, rate 1/3" nr_dlsim -n100 -s20 -e13 -R273 -b273 -x1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_dlsim.ldpc_cuda.18seg_r23 "nr_dlsim: 18 segments, rate 2/3" nr_dlsim -n100 -s20 -e22 -R273 -b273 -x1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_dlsim.ldpc_cuda.50seg_r89 "nr_dlsim: 50 segments, rate 8/9" nr_dlsim -n100 -s40 -e27 -R273 -b273 -z2 -x2 -y2 -Q32 -P --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_dlsim.ldpc_cuda.2t2r_mcs25_64qam "nr_dlsim: 2T2R/2 layers, MCS 25, 64QAM, 100 MHz 30 kHz SCS)" nr_dlsim -n100 -R273 -b273 -P -e 25 -s30 -x2 -y2 -z2 --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_dlsim.ldpc_cuda.1t1r_mcs25_256qam "nr_dlsim: 1T1R/1 layer, MCS 25, 256QAM, 100 MHz 30 kHz SCS)" nr_dlsim -n100 -R273 -b273 -P -e 25 -s30 -q 1 --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_dlsim.ldpc_cuda.1t1r_mcs25_256qam_bw "nr_dlsim: 1T1R/1 layer, MCS 25 256QAM, 40 MHz 30 kHz SCS" nr_dlsim -n100 -R106 -b106 -P -e 25 -s30 -q 1 --loader.ldpc.shlibversion _cuda)
  # code blocks with Z < 384 (Z = 352, 9 code blocks)
  add_physim_test(nr_dlsim.ldpc_cuda.9seg_z352_256qam "nr_dlsim: 9 segments, Z 352, 256QAM MCS 24" nr_dlsim -n100 -s24 -S24.1 -e24 -q1 -R106 -b72 -I20 -P --loader.ldpc.shlibversion _cuda)
  # more than 128 code blocks (129)
  add_physim_test(nr_dlsim.ldpc_cuda.4t4r_mcs27_256qam "nr_dlsim: 4T4R/4 layers, MCS 27 256QAM, 273 PRBs (129 code blocks)" nr_dlsim -n10 -R273 -b273 -P -e 27 -s40 -S41 -q 1 -x4 -y4 -z4 --loader.ldpc.shlibversion _cuda)

  add_physim_test(nr_ulsim.ldpc_cuda.9seg_r13 "nr_ulsim: 9 segments, rate 1/3" nr_ulsim -n100 -s20 -m13 -R273 -r273 -W1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_ulsim.ldpc_cuda.18seg_r23 "nr_ulsim: 18 segments, rate 2/3" nr_ulsim -n100 -s20 -m22 -R273 -r273 -W1 -y1 -z1 -P --loader.ldpc.shlibversion _cuda)
  add_physim_test(nr_ulsim.ldpc_cuda.50seg_r89 "nr_ulsim: 50 segments, rate 8/9" nr_ulsim -n100 -s40 -m27 -R273 -r273 -W2 -I9 -y2 -z2 -P --loader.ldpc.shlibversion _cuda)

endif()
