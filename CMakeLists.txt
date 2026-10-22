# SPDX-License-Identifier: LicenseRef-CSSL-1.0

cmake_minimum_required (VERSION 3.19)
project (OpenAirInterface LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OAI_VERSION 3.0.0)

option(ENABLE_CHANNEL_SIM_CUDA "Enable CUDA accelerated channel simulation" OFF)
if(ENABLE_CHANNEL_SIM_CUDA)
    find_package(CUDA REQUIRED)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "CUDA explicitly enabled, building with GPU acceleration support.")

    enable_language(CUDA)

    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(CMAKE_CUDA_ARCHITECTURES 90)
    endif()

    option(USE_UNIFIED_MEMORY "Enable CUDA Unified Memory path" OFF)
    option(USE_ATS_MEMORY "Enable ATS Hybrid Memory path" ON)

    if(USE_UNIFIED_MEMORY AND USE_ATS_MEMORY)
        message(FATAL_ERROR "Cannot enable both USE_UNIFIED_MEMORY and USE_ATS_MEMORY at the same time.")
    endif()

    if(USE_UNIFIED_MEMORY)
        add_compile_definitions(USE_UNIFIED_MEMORY)
        message(STATUS "CUDA Unified Memory path enabled.")
    elseif(USE_ATS_MEMORY)
        add_compile_definitions(USE_ATS_MEMORY)
        message(STATUS "CUDA ATS Hybrid Memory path enabled.")
    else()
        message(STATUS "CUDA Explicit Copy path enabled (default ATS was overridden).")
    endif()

    add_compile_definitions(CHANNEL_SIM_CUDA)
endif()

#########################################################
# Base directories, compatible with legacy OAI building #
#########################################################
set (OPENAIR_DIR     ${CMAKE_SOURCE_DIR})

include("cmake_targets/macros.cmake")
if(NOT DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_SOURCE_CACHE ~/.cache/cpm/ CACHE FILEPATH "Directory to download CPM dependencies")
endif()
include("cmake_targets/CPM.cmake")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_targets/tools/MODULES" "${CMAKE_MODULE_PATH}")

##############################
### CCache: reduce compilation time
##############################
#use ccache if available
option(CCACHE_ACTIVE "CCache" ON)
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND AND CCACHE_ACTIVE)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_FOUND}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_FOUND}")
  message(STATUS "Found ccache in ${CCACHE_FOUND}")
else()
  message(STATUS "Ccache not found. Consider installing it for faster compilation. Command: sudo apt/dnf install ccache")
endif()

# System packages that are required
# We use either the cmake buildin, in ubuntu are in: /usr/share/cmake*/Modules/
# or cmake provide a generic interface to pkg-config that widely used
###################################
include(FindPkgConfig)


option(PACKAGING_RPM "By default will create debian packages, with this option willl create rpm packages" OFF)
option(PACKAGING_NR "Will produce a package containing nr target, configuration files and docs" OFF)
option(PACKAGING_LTE "Will produce a package containing lte target, configuration files and docs" OFF)
option(PACKAGING_COMMON "Will produce a package containing common target, configuration files and docs" OFF)
option(PACKAGING_USRP "Will produce a package containing usrp target, configuration files and docs" OFF)
option(PACKAGING_PHYSIM "Will produce a package containing physim target, configuration files and docs" OFF)

# Check if asn1c is installed, and check if it supports all options we need
# a user can provide ASN1C_EXEC to override an asn1c to use (e.g., parallel
# installation)
find_program(ASN1C_EXEC_PATH asn1c HINTS /opt/asn1c/bin)
set(ASN1C_EXEC ${ASN1C_EXEC_PATH} CACHE FILEPATH "path to asn1c executable")
option(AUTO_DOWNLOAD_ASN1C "Automatically download asn1c if not found" OFF)
if (ASN1C_EXEC)
  check_option(${ASN1C_EXEC} -gen-APER ASN1C_EXEC)
  check_option(${ASN1C_EXEC} -no-gen-UPER ASN1C_EXEC)
  check_option(${ASN1C_EXEC} -no-gen-JER ASN1C_EXEC)
  check_option(${ASN1C_EXEC} -no-gen-BER ASN1C_EXEC)
  check_option(${ASN1C_EXEC} -no-gen-OER ASN1C_EXEC)
  add_custom_target(asn1c DEPENDS ${ASN1C_EXEC})
else ()
  if (NOT AUTO_DOWNLOAD_ASN1C)
    message(FATAL_ERROR "asn1c not found. Install it globally or activate AUTO_DOWNLOAD_ASN1C")
  endif()
  message(STATUS "Downloading and compile asn1c local in this build tree")
  include(ExternalProject)
  ExternalProject_Add(
    asn1c_gen
    GIT_REPOSITORY https://github.com/mouse07410/asn1c
    GIT_TAG 940dd5fa9f3917913fd487b13dfddfacd0ded06e
    GIT_REMOTE_UPDATE_STRATEGY CHECKOUT
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/bin/asn1c
    BUILD_IN_SOURCE True
    CONFIGURE_COMMAND autoreconf -i
    BUILD_COMMAND ./configure --prefix ${CMAKE_BINARY_DIR}
    COMMAND make -j8 CFLAGS=-fno-strict-aliasing -Wmisleading-indentation
    STEP_TARGETS build
  )
  set(ASN1C_EXEC ${CMAKE_BINARY_DIR}/bin/asn1c)
  add_custom_target(asn1c DEPENDS asn1c_gen)
endif()
#########################################################
# Base directories, compatible with legacy OAI building #
#########################################################
set (NFAPI_DIR       ${OPENAIR_DIR}/nfapi/open-nFAPI)
set (NFAPI_USER_DIR  ${OPENAIR_DIR}/nfapi/oai_integration)
set (OPENAIR1_DIR    ${OPENAIR_DIR}/openair1)
set (OPENAIR2_DIR    ${OPENAIR_DIR}/openair2)
set (OPENAIR3_DIR    ${OPENAIR_DIR}/openair3)

project (OpenAirInterface)

####################################################
# compilation flags
#############################################

#set(CMAKE_BUILD_TYPE "Debug")
if (CMAKE_BUILD_TYPE STREQUAL "")
   set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()
message(STATUS "CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")
add_list_string_option(CMAKE_BUILD_TYPE "RelWithDebInfo" "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel." Debug Release RelWithDebInfo MinSizeRel)

# in case /proc/cpuinfo exists we want to inspect available Intrinsics
# -so not to go always through SIMDE emulation
# -so to avoid AVX512 instructions generation by gcc
if(EXISTS "/proc/cpuinfo" AND NOT CROSS_COMPILE)
  file(STRINGS "/proc/cpuinfo" CPUFLAGS REGEX flags LIMIT_COUNT 1)
  file(STRINGS "/proc/cpuinfo" CPUPART REGEX "CPU part" LIMIT_COUNT 1)
else()
  message(WARNING "did not find /proc/cpuinfo -- not setting any x86-specific compilation variables")
endif()

eval_boolean(AUTODETECT_AVX512 DEFINED CPUFLAGS AND CPUFLAGS MATCHES "avx512")
add_boolean_option(AVX512 ${AUTODETECT_AVX512} "Whether AVX512 intrinsics is available on the host processor" ON)

eval_boolean(AUTODETECT_AVX2 DEFINED CPUFLAGS AND CPUFLAGS MATCHES "avx2")
add_boolean_option(AVX2 ${AUTODETECT_AVX2} "Whether AVX2 intrinsics is available on the host processor" ON)

eval_boolean(AUTODETECT_GFNI DEFINED CPUFLAGS AND CPUFLAGS MATCHES "gfni")
add_boolean_option(GFNI ${AUTODETECT_GFNI} "Whether GFNI intrinsics is available on the host processor" ON)

message(STATUS "CPU architecture is ${CMAKE_SYSTEM_PROCESSOR}")
if (CROSS_COMPILE)
  message(STATUS "setting as aarch64")
  set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -lgcc -lrt")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  # The following intrinsics are assumed to be available on any x86 system
  # (avx, f16c, fma, mmx, pclmul, sse, sse2, sse3, xop)
  set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_AVX_NATIVE -DSIMDE_X86_AVX_NATIVE -DSIMDE_X86_F16C_NATIVE -DSIMDE_X86_FMA_NATIVE -DSIMDE_X86_MMX_NATIVE -DSIMDE_X86_PCLMUL_NATIVE -DSIMDE_X86_SSE2_NATIVE -DSIMDE_X86_SSE3_NATIVE -DSIMDE_X86_SSE_NATIVE -DSIMDE_X86_XOP_HAVE_COM_ -DSIMDE_X86_XOP_NATIVE")
  message(STATUS "AVX512 intrinsics are ${AVX512}")
  if(${AVX512})
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_AVX512BW_NATIVE -DSIMDE_X86_AVX512F_NATIVE -DSIMDE_X86_AVX512VL_NATIVE -mavx512bw -march=skylake-avx512 -mtune=skylake-avx512")
  else()
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -mno-avx512f -march=native")
  endif()
  message(STATUS "AVX2 intrinsics are ${AVX2}")
  if(${AVX2})
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_AVX2_NATIVE -DSIMDE_X86_VPCLMULQDQ_NATIVE")
  endif()
  message(STATUS "GFNI intrinsics are ${GFNI}")
  if(${GFNI})
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_GFNI_NATIVE -mgfni")
  endif()
  if (CPUFLAGS MATCHES "sse4_1")
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_SSE4_1_NATIVE")
  endif()
  if(CPUFLAGS MATCHES "sse4_2")
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_SSE4_2_NATIVE")
  endif()
  if(CPUFLAGS MATCHES "ssse3")
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -DSIMDE_X86_SSSE3_NATIVE")
  endif()
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mfloat-abi=hard -mfpu=neon -lgcc -lrt")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
# Neoverse CPU part codes available at https://github.com/util-linux/util-linux/blob/master/sys-utils/lscpu-arm.c
  if (CPUPART MATCHES "0xd40") # Neoverse-V1
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=neoverse-v1 -lgcc -lrt")
  elseif (CPUPART MATCHES "0xd4f") # Neoverse-V2
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=neoverse-v2 -ftree-vectorize -lgcc -lrt")
  elseif (CPUPART MATCHES "0xd0c") # Neoverse-N1
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=neoverse-n1  -lgcc -lrt")
  elseif (CPUPART MATCHES "0xd49") # Neoverse-N2
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=neoverse-n2 -ftree-vectorize -lgcc -lrt")
  elseif (CPUPART MATCHES "0xd08") # Cortex-A72 (e.g. NXP LS2160A)
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=cortex-a72 -lgcc -lrt")
  elseif (CPUPART MATCHES "0xd03") # Cortex-A53
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=cortex-a53 -lgcc -lrt")
  else ()
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -gdwarf-2 -mcpu=native -lgcc -lrt")
  endif()
else()
  message(FATAL_ERROR "compile for CPU architecture ${CPUARCH}, CMAKE_SYSTEM_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if (NOT CROSS_COMPILE)
  if (CPUPART MATCHES "0xd08")
    # Cortex-A72 is ARMv8.0-A only (no ARMv8.2-A); hardware includes crypto (aes, pmull, sha1, sha2)
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -march=armv8-a+simd+crypto")
  elseif (CPUPART MATCHES "0xd03")
    # Cortex-A53 is ARMv8.0-A only; crypto extension is optional and not assumed present
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -march=armv8-a+simd")
  else()
    set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -march=native")
  endif()
else ()
  set(C_FLAGS_PROCESSOR "${C_FLAGS_PROCESSOR} -march=armv8.2-a")
endif()

# we need -rdynamic to incorporate all symbols in shared objects, see man page
set(commonOpts "-pipe -fPIC -Wall -fno-strict-aliasing -rdynamic")
# GNU C/C++ Compiler might throw many warnings without packed-bitfield-compat, see man page
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(commonOpts "${commonOpts} -Wno-packed-bitfield-compat")
endif()
# clang: suppress complaints about unused command line argument (-rdynamic only
# used during linking) and "const member leaves the object uninitialized"
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(commonOpts "${commonOpts} -Wno-unused-command-line-argument -Wno-default-const-init-field-unsafe")
endif()

if (ENABLE_LDPC_CUDA)
  # The GPU offload is written against PHY/gpu_compat.h, which maps the CUDA
  # runtime API onto HIP as well, so the backend follows whichever toolchain is
  # installed rather than needing a flag of its own. Set LDPC_GPU_BACKEND to
  # CUDA or HIP to force one when both are present.
  set(LDPC_GPU_BACKEND "AUTO" CACHE STRING "GPU backend for the LDPC offload (AUTO, CUDA or HIP)")
  set_property(CACHE LDPC_GPU_BACKEND PROPERTY STRINGS AUTO CUDA HIP)

  # CheckLanguage reports CMAKE_HIP_COMPILER without committing to the
  # language. Both the AUTO decision and the ROCm prefix below need it, so
  # probe unconditionally: forcing LDPC_GPU_BACKEND=HIP used to skip this and
  # then fail in find_package(hip) for want of a prefix path.
  include(CheckLanguage)
  check_language(HIP)

  if (LDPC_GPU_BACKEND STREQUAL "AUTO")
    if (CMAKE_HIP_COMPILER)
      set(LDPC_GPU_BACKEND "HIP")
    else()
      set(LDPC_GPU_BACKEND "CUDA")
    endif()
    message(STATUS "LDPC GPU offload: selected the ${LDPC_GPU_BACKEND} backend")
  endif()

  if (LDPC_GPU_BACKEND STREQUAL "HIP")
    # ROCm installs hip-config.cmake under its own prefix, which is two levels
    # up from the HIP compiler (<rocm>/lib/llvm/bin/clang++); derive it rather
    # than making the caller pass CMAKE_PREFIX_PATH.
    if (CMAKE_HIP_COMPILER)
      get_filename_component(ROCM_ROOT "${CMAKE_HIP_COMPILER}" DIRECTORY)
      get_filename_component(ROCM_ROOT "${ROCM_ROOT}/../../.." ABSOLUTE)
      list(APPEND CMAKE_PREFIX_PATH "${ROCM_ROOT}")
    endif()
    # hip::host carries the ROCm include directories, libamdhip64 and
    # __HIP_PLATFORM_AMD__, which is what PHY/gpu_compat.h keys off, so nothing
    # has to hardcode a ROCm path.
    find_package(hip REQUIRED)
  elseif (NOT LDPC_GPU_BACKEND STREQUAL "CUDA")
    message(FATAL_ERROR "LDPC_GPU_BACKEND must be AUTO, CUDA or HIP, got \"${LDPC_GPU_BACKEND}\"")
  endif()

  # TODO only on specific targets!!
  set(commonOpts "${commonOpts} -DLDPC_CUDA")
endif()

set(CMAKE_C_FLAGS
  "${C_FLAGS_PROCESSOR} ${commonOpts} -std=gnu11 -funroll-loops ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${C_FLAGS_PROCESSOR} ${commonOpts} ${CMAKE_CXX_FLAGS}")

add_boolean_option(SANITIZE_ADDRESS OFF "enable the address sanitizer (ASan)" ON)
if (SANITIZE_ADDRESS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -fno-common")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -fno-common")
endif ()

add_boolean_option(SANITIZE_THREAD OFF "enable the address sanitizer (TSan)" ON)
if (SANITIZE_THREAD)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -fno-common")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -fno-common")
  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

add_boolean_option(SANITIZE_UNDEFINED OFF "enable the undefined behavior sanitizer (UBSan)" ON)
if (SANITIZE_UNDEFINED)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-sanitize-recover=all")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-sanitize-recover=all")
endif ()

add_boolean_option(SANITIZE_MEMORY OFF "enable the memory sanitizer (MSan, requires clang, incompatible with ASan/UBSan)" ON)
if(SANITIZE_MEMORY)
  if (SANITIZE_UNDEFINED OR SANITIZE_ADDRESS)
    message(FATAL_ERROR "memory sanitizer cannot coexist with address sanitizer or undefined behavior sanitizer, please disable either MSan or ASan and UBSan")
  endif()
  if (NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
    message(FATAL_ERROR "memory sanitizer requires clang, please set clang using CC=/usr/bin/clang CXX=/usr/bin/clang++ ./build_oai ...")
  endif()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer -fsanitize-recover=memory")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer -fsanitize-recover=memory")
endif()

add_definitions("-DASN_DISABLE_OER_SUPPORT -DHAVE_CONFIG_H -DHAVE_CONFIG_H_")

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_RPATH ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

if(PACKAGING_LTE OR PACKAGING_NR OR PACKAGING_COMMON OR PACKAGING_USRP OR PACKAGING_PHYSIM)
  set(CMAKE_SKIP_INSTALL_RPATH TRUE)
endif()

# set a flag for changes in the source code
# these changes are related to hardcoded path to include .h files
set(debugOpt "-ggdb2 -DMALLOC_CHECK_=3 -fno-delete-null-pointer-checks")
set(CMAKE_C_FLAGS_DEBUG "${debugOpt} -O0")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${debugOpt} -O2")
set(CMAKE_C_FLAGS_RELEASE "-O3")

# Enable assert() for RelWithDebInfo builds
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

set(GIT_BRANCH        "UNKNOWN")
set(GIT_COMMIT_HASH   "UNKNOWN")
set(GIT_COMMIT_DATE   "UNKNOWN")

find_package(Git)
if(GIT_FOUND)
  # Get the current working branch
  execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  # Get the latest abbreviated commit hash of the working branch
  execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  # Get the latest commit date of the working branch
  execute_process(
    COMMAND git log -1 --format=%cd
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

option(USE_UNIFIED_MEMORY "Enable CUDA Unified Memory path instead of explicit copies" OFF)

option(USE_ATS_MEMORY "Enable Hybrid ATS (CPU->GPU) and Explicit Copy (GPU->CPU) path" OFF)

if(USE_UNIFIED_MEMORY AND USE_ATS_MEMORY)
  message(FATAL_ERROR "Cannot enable both USE_UNIFIED_MEMORY and USE_ATS_MEMORY at the same time.")
endif()

if(USE_UNIFIED_MEMORY)
  add_compile_definitions(USE_UNIFIED_MEMORY)
  message(STATUS "CUDA Unified Memory path enabled.")
elseif(USE_ATS_MEMORY)
  add_compile_definitions(USE_ATS_MEMORY)
  message(STATUS "CUDA ATS Hybrid Memory path enabled.")
endif()

# Debug related options
#########################################
# asn1c skeletons have hardcoded this flag to make customized debug logs
# OAI uses this feature to re-use OAI LOG_I(ASN1, ...)
# see common/utils/config.h
add_boolean_option(TRACE_ASN1C_ENC_DEC OFF "Enable ASN1 encoder/decoder debug traces via OAI logging system" ON)
add_boolean_option(T_TRACER            ON  "Activate the T tracer, a debugging/monitoring framework" ON)
add_boolean_option(ENABLE_LTTNG        OFF "Activate the LTTNG tracer, a debugging/monitoring framework" ON)
add_boolean_option(UE_DEBUG_TRACE      OFF "Activate UE debug trace" ON)
add_boolean_option(UE_TIMING_TRACE     OFF "Activate UE timing trace" ON)

add_boolean_option(TRACY_ENABLE OFF "Enable tracy instrumentation" ON)
if (TRACY_ENABLE)
  # the tracy version here should match the tracy server version
  # below is latest release as of this commit
  CPMAddPackage("gh:wolfpld/tracy#v0.13.1")
endif()

set (OCP_ITTI ${OPENAIR_DIR}/common/utils/ocp_itti)
add_library(ITTI ${OCP_ITTI}/intertask_interface.cpp)
target_link_libraries(ITTI PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)

#########################
##### E2 AGENT
#########################
set(E2_AGENT "OFF" CACHE STRING "O-RAN-compliant E2 Agent")
set_property(CACHE E2_AGENT PROPERTY STRINGS "ON" "OFF")

if (E2_AGENT)
  set(E2AP_VERSION "E2AP_V2" CACHE STRING "E2AP version")
  set_property(CACHE E2AP_VERSION PROPERTY STRINGS "E2AP_V1" "E2AP_V2" "E2AP_V3")
  message(STATUS "Selected E2AP version: ${E2AP_VERSION}")

  set(KPM_VERSION "KPM_V2_03" CACHE STRING "The KPM SM version to use")
  set_property(CACHE KPM_VERSION PROPERTY STRINGS "KPM_V2_03" "KPM_V3_00")
  message(STATUS "Selected KPM version: ${KPM_VERSION}")
endif()

add_boolean_option(ENABLE_IMSCOPE OFF "Enable phy scope based on imgui" OFF)
add_boolean_option(ENABLE_IMSCOPE_RECORD OFF "Enable recording IQ data for imscope" OFF)

#########################
##### E3 AGENT
#########################
set(E3_AGENT "OFF" CACHE STRING "O-RAN nGRG E3 Agent for dApps")
set_property(CACHE E3_AGENT PROPERTY STRINGS "ON" "OFF")

##################################################
# ASN.1 grammar C code generation & dependencies #
##################################################

# RRC
######
add_library(lte_rrc
    ${OPENAIR2_DIR}/RRC/LTE/MESSAGES/asn1_msg.c
    ${OPENAIR2_DIR}/RRC/LTE/MESSAGES/asn1_msg_NB_IoT.c)
target_link_libraries(lte_rrc PUBLIC asn1_lte_rrc)
target_link_libraries(lte_rrc PRIVATE nr_rrc SECURITY)


#NR RRC
#######
add_library(nr_rrc
    ${OPENAIR2_DIR}/RRC/NR/MESSAGES/asn1_msg.c
    ${OPENAIR2_DIR}/RRC/NR/rrc_gNB_measurements.c)
target_link_libraries(nr_rrc PUBLIC asn1_nr_rrc asn1_lte_rrc rrc_cell_management)

# S1AP
##############
set(S1AP_DIR ${OPENAIR3_DIR}/S1AP)
add_library(s1ap
  ${S1AP_DIR}/s1ap_eNB.c
  ${S1AP_DIR}/s1ap_eNB_context_management_procedures.c
  ${S1AP_DIR}/s1ap_eNB_decoder.c
  ${S1AP_DIR}/s1ap_eNB_encoder.c
  ${S1AP_DIR}/s1ap_eNB_handlers.c
  ${S1AP_DIR}/s1ap_eNB_itti_messaging.c
  ${S1AP_DIR}/s1ap_eNB_management_procedures.c
  ${S1AP_DIR}/s1ap_eNB_nas_procedures.c
  ${S1AP_DIR}/s1ap_eNB_nnsf.c
  ${S1AP_DIR}/s1ap_eNB_overload.c
  ${S1AP_DIR}/s1ap_eNB_trace.c
  ${S1AP_DIR}/s1ap_eNB_ue_context.c
  )
target_include_directories(s1ap PUBLIC ${S1AP_DIR})
target_link_libraries(s1ap PUBLIC asn1_s1ap)
target_link_libraries(s1ap PRIVATE nr_rrc asn1_nr_rrc_hdrs lte_rrc SCTP_CLIENT)

# NGAP
##############
set(NGAP_DIR ${OPENAIR3_DIR}/NGAP)
add_library(ngap
  ${NGAP_DIR}/ngap_gNB.c
  ${NGAP_DIR}/ngap_common.c
  ${NGAP_DIR}/ngap_gNB_context_management_procedures.c
  ${NGAP_DIR}/ngap_gNB_mobility_management.c
  ${NGAP_DIR}/ngap_gNB_decoder.c
  ${NGAP_DIR}/ngap_gNB_encoder.c
  ${NGAP_DIR}/ngap_gNB_handlers.c
  ${NGAP_DIR}/ngap_gNB_itti_messaging.c
  ${NGAP_DIR}/ngap_gNB_management_procedures.c
  ${NGAP_DIR}/ngap_gNB_paging.c
  ${NGAP_DIR}/ngap_gNB_nas_procedures.c
  ${NGAP_DIR}/ngap_gNB_pdu_session_management.c
  ${NGAP_DIR}/ngap_gNB_nnsf.c
  ${NGAP_DIR}/ngap_gNB_overload.c
  ${NGAP_DIR}/ngap_gNB_ue_context.c
  ${NGAP_DIR}/ngap_gNB_NRPPa_transport_procedures.c
  )
target_include_directories(ngap PUBLIC ${NGAP_DIR})
target_link_libraries(ngap PUBLIC asn1_ngap)
target_link_libraries(ngap PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)
if(ENABLE_TESTS)
  target_compile_definitions(ngap PRIVATE ENABLE_TESTS)
endif()
if(E2_AGENT)
  target_compile_definitions(ngap PRIVATE E2_AGENT)
endif()

#M2AP
##############
set(M2AP_DIR ${OPENAIR2_DIR}/M2AP)
add_library(m2ap
  ${M2AP_DIR}/m2ap_common.c
  ${M2AP_DIR}/m2ap_eNB.c
  ${M2AP_DIR}/m2ap_MCE.c
  ${M2AP_DIR}/m2ap_decoder.c
  ${M2AP_DIR}/m2ap_encoder.c
  ${M2AP_DIR}/m2ap_MCE_handler.c
  ${M2AP_DIR}/m2ap_eNB_handler.c
  ${M2AP_DIR}/m2ap_itti_messaging.c
  ${M2AP_DIR}/m2ap_eNB_management_procedures.c
  ${M2AP_DIR}/m2ap_eNB_generate_messages.c
  ${M2AP_DIR}/m2ap_MCE_management_procedures.c
  ${M2AP_DIR}/m2ap_MCE_generate_messages.c
  ${M2AP_DIR}/m2ap_ids.c
  ${M2AP_DIR}/m2ap_timers.c
  ${M2AP_DIR}/m2ap_MCE_interface_management.c
  ${M2AP_DIR}/m2ap_eNB_interface_management.c
 )
target_link_libraries(m2ap PUBLIC asn1_m2ap)
target_link_libraries(m2ap PRIVATE lte_rrc nr_rrc SCTP_CLIENT)
target_include_directories(m2ap PUBLIC "${M2AP_DIR}")

#M3AP
##############
set(M3AP_DIR ${OPENAIR3_DIR}/M3AP)
add_library(m3ap
  ${M3AP_DIR}/m3ap_common.c
  ${M3AP_DIR}/m3ap_decoder.c
  ${M3AP_DIR}/m3ap_encoder.c
  ${M3AP_DIR}/m3ap_MCE_handler.c
  ${M3AP_DIR}/m3ap_MME_handler.c
  ${M3AP_DIR}/m3ap_MME.c
  ${M3AP_DIR}/m3ap_MME_management_procedures.c
  ${M3AP_DIR}/m3ap_MME_interface_management.c
  ${M3AP_DIR}/m3ap_MCE.c
  ${M3AP_DIR}/m3ap_MCE_management_procedures.c
  ${M3AP_DIR}/m3ap_MCE_interface_management.c
  ${M3AP_DIR}/m3ap_itti_messaging.c
  ${M3AP_DIR}/m3ap_ids.c
  ${M3AP_DIR}/m3ap_timers.c
)
target_link_libraries(m3ap PUBLIC asn1_m3ap)
target_link_libraries(m3ap PRIVATE m2ap SCTP_CLIENT)
target_link_libraries(m3ap PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)
target_include_directories(m3ap PUBLIC "${M3AP_DIR}")

#X2AP
##############
set(X2AP_DIR ${OPENAIR2_DIR}/X2AP)
add_library(x2ap
  ${X2AP_DIR}/x2ap_common.c
  ${X2AP_DIR}/x2ap_eNB.c
  ${X2AP_DIR}/x2ap_eNB_decoder.c
  ${X2AP_DIR}/x2ap_eNB_encoder.c
  ${X2AP_DIR}/x2ap_eNB_handler.c
  ${X2AP_DIR}/x2ap_eNB_itti_messaging.c
  ${X2AP_DIR}/x2ap_eNB_management_procedures.c
  ${X2AP_DIR}/x2ap_eNB_generate_messages.c
  ${X2AP_DIR}/x2ap_ids.c
  ${X2AP_DIR}/x2ap_timers.c
 )
target_link_libraries(x2ap PUBLIC asn1_x2ap)
target_link_libraries(x2ap PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs SCTP_CLIENT)

# F1AP
##############
set(F1AP_DIR ${OPENAIR2_DIR}/F1AP)
add_library(f1ap
            ${F1AP_DIR}/f1ap_common.c
            ${F1AP_DIR}/f1ap_ids.c
            ${F1AP_DIR}/f1ap_cu_interface_management.c
            ${F1AP_DIR}/f1ap_cu_paging.c
            ${F1AP_DIR}/f1ap_cu_rrc_message_transfer.c
            ${F1AP_DIR}/f1ap_cu_task.c
            ${F1AP_DIR}/f1ap_cu_ue_context_management.c
            ${F1AP_DIR}/f1ap_du_interface_management.c
            ${F1AP_DIR}/f1ap_du_paging.c
            ${F1AP_DIR}/f1ap_du_rrc_message_transfer.c
            ${F1AP_DIR}/f1ap_du_task.c
            ${F1AP_DIR}/f1ap_du_ue_context_management.c
            ${F1AP_DIR}/f1ap_encoder.c
            ${F1AP_DIR}/f1ap_handlers.c
            ${F1AP_DIR}/f1ap_cu_positioning.c
            ${F1AP_DIR}/f1ap_du_positioning.c
            ${F1AP_DIR}/f1ap_itti_messaging.c)
target_include_directories(f1ap PUBLIC ${F1AP_DIR})
target_link_libraries(f1ap PUBLIC asn1_f1ap GTPV1U)
target_link_libraries(f1ap PRIVATE ngap nr_rrc ds f1ap_lib)
target_include_directories(f1ap PRIVATE ${F1AP_DIR} ${F1AP_DIR}/lib)
if(E2_AGENT)
  target_compile_definitions(f1ap PRIVATE E2_AGENT)
endif()

# LPP
##############
# target asn1_lpp in openair3/LPP/MESSAGES/CMakeLists.txt used below

# Hardware dependant options
###################################
set(NB_ANTENNAS_RX "4" CACHE STRING "Number of antennas in reception")
set_property(CACHE NB_ANTENNAS_RX PROPERTY STRINGS "1" "2" "4" "8")
set(NB_ANTENNAS_TX "4" CACHE STRING "Number of antennas in transmission")
set_property(CACHE NB_ANTENNAS_TX PROPERTY STRINGS "1" "2" "4" "8")

set (CONFIG_ROOTDIR ${OPENAIR_DIR}/common/config)
add_library(config_internals OBJECT
  ${CONFIG_ROOTDIR}/config_common.c
)

add_library(CONFIG_LIB
  ${CONFIG_ROOTDIR}/config_load_configmodule.c
  ${CONFIG_ROOTDIR}/config_userapi.c
  ${CONFIG_ROOTDIR}/config_cmdline.c
)
target_link_libraries(CONFIG_LIB PRIVATE config_internals dl UTIL)

# CONFIG_LIB is a wrapper around a particular config implementation, default is
# libconfig in params_libconfig below
pkg_check_modules(libconfig REQUIRED IMPORTED_TARGET libconfig)
add_library(params_libconfig MODULE ${CONFIG_ROOTDIR}/libconfig/config_libconfig.c)
target_link_libraries(params_libconfig PRIVATE config_internals PkgConfig::libconfig)

# shared library loader
add_library(shlib_loader OBJECT common/utils/load_module_shlib.c)
target_link_libraries(shlib_loader PRIVATE CONFIG_LIB)

##############################################################
#    ???!!! TO BE DOCUMENTED OPTIONS !!!???
##############################################################

add_boolean_option(UE_EXPANSION             OFF         "enable UE_EXPANSION with max 256 UE" ON)
add_boolean_option(PHY_TX_THREAD            OFF         "enable UE_EXPANSION with max 256 UE" ON)
add_boolean_option(PRE_SCD_THREAD           OFF         "enable UE_EXPANSION with max 256 UE" ON)

##########################
# SCHEDULING/REAL-TIME/PERF options
##########################
add_boolean_option(ENABLE_USE_CPU_EXECUTION_TIME OFF "Add data in vcd traces: disable it if perf issues" ON)
add_boolean_option(ENABLE_VCD              OFF  "always true now, time measurements of proc calls and var displays" ON)
add_boolean_option(ENABLE_VCD_FIFO         OFF  "time measurements of proc calls and var displays sent to FIFO (one more thread)" ON)

##########################
# PHY options
##########################
add_integer_option(MAX_NUM_CCs             1     "Carrier component data arrays size (oai doesn't support carrier aggregation for now)" ON)
add_boolean_option(SMBV                    OFF "Rohde&Schwarz SMBV100A vector signal generator" ON)
add_boolean_option(DEBUG_PHY               OFF "Enable PHY layer debugging options" ON)
add_boolean_option(DEBUG_PHY_PROC          OFF "Enable debugging of PHY layer procedures" ON)

##########################
# NAS LAYER OPTIONS
##########################
add_boolean_option(NAS_BUILT_IN_UE         ON  "UE NAS layer present in this executable" ON)

##########################
# RRC LAYER OPTIONS
##########################
add_boolean_option(RRC_DEFAULT_RAB_IS_AM       ON   "set the LTE RLC mode to AM for the default bearer, otherwise it is UM." ON)

# add the binary tree to the search path for include files
# We will find common/oai_version.h after generation in target directory
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Legacy exact order

include_directories("${OPENAIR_DIR}/executables")
include_directories("${OPENAIR2_DIR}/COMMON")
include_directories("${OPENAIR2_DIR}/UTIL")
include_directories("${OPENAIR3_DIR}/UTILS")
include_directories("${OPENAIR1_DIR}")
include_directories("${OPENAIR2_DIR}")
include_directories("${OPENAIR3_DIR}/NAS/TOOLS")
include_directories("${OPENAIR2_DIR}/ENB_APP")
include_directories("${OPENAIR2_DIR}/GNB_APP")
include_directories("${OPENAIR_DIR}/common/utils")
include_directories("${OPENAIR_DIR}/common/utils/collection")
include_directories("${OPENAIR_DIR}/common/utils/ocp_itti")
include_directories("${OPENAIR3_DIR}/NAS/COMMON")
include_directories("${OPENAIR3_DIR}/NAS/COMMON/API/NETWORK")
include_directories("${OPENAIR3_DIR}/NAS/COMMON/EMM/MSG")
include_directories("${OPENAIR3_DIR}/NAS/COMMON/ESM/MSG")
include_directories("${OPENAIR3_DIR}/NAS/UE/ESM")
include_directories("${OPENAIR3_DIR}/NAS/UE/EMM")
include_directories("${OPENAIR3_DIR}/NAS/UE/API/USER")
include_directories("${OPENAIR3_DIR}/NAS/COMMON/IES")
include_directories("${OPENAIR3_DIR}/NAS/COMMON/UTIL")
include_directories("${OPENAIR_DIR}/radio/COMMON")
include_directories("${OPENAIR2_DIR}/UTIL/OPT")
include_directories("${OPENAIR_DIR}")

###########
# Utilities
###############

# include T directory even if the T is off because T macros are in the code
# no matter what. Note: some files are generated, so we have both source and
# binary directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common/utils/T
                    ${CMAKE_CURRENT_BINARY_DIR}/common/utils/T)

# set the T_LIB name to T if the target exists, otherwise leave empty
set(T_LIB $<TARGET_NAME_IF_EXISTS:T>)

add_library(UTIL
  ${OPENAIR_DIR}/common/utils/LOG/vcd_signal_dumper.c
  ${OPENAIR2_DIR}/UTIL/OPT/probe.c
)

pkg_check_modules(cap IMPORTED_TARGET libcap)
if (cap_FOUND)
  # see system.c for more info
  target_link_libraries(UTIL PRIVATE PkgConfig::cap)
  target_compile_definitions(UTIL PRIVATE HAVE_LIB_CAP)
endif()
target_link_libraries(UTIL PUBLIC ${T_LIB} pthread LOG thread-pool utils barrier actor)
target_link_libraries(UTIL PUBLIC instrumentation)

# Physical Channel Procedures Scheduling
################################"
set(SCHED_SRC
  ${OPENAIR1_DIR}/SCHED/fapi_l1.c
  ${OPENAIR1_DIR}/SCHED/phy_procedures_lte_eNb.c
  ${OPENAIR1_DIR}/SCHED/phy_procedures_lte_common.c
)
add_library(SCHED_LIB ${SCHED_SRC})
target_link_libraries(SCHED_LIB PRIVATE asn1_lte_rrc_hdrs)
target_link_libraries(SCHED_LIB PRIVATE nfapi_pnf_lib)

set(SCHED_NR_SRC
  ${OPENAIR1_DIR}/SCHED_NR/phy_procedures_nr_gNB.c
  ${OPENAIR1_DIR}/SCHED_NR/nr_prach_procedures.c
  ${OPENAIR1_DIR}/SCHED_NR/nr_ru_procedures.c
  ${OPENAIR1_DIR}/SCHED_NR/phy_frame_config_nr.c
)
add_library(SCHED_NR_LIB ${SCHED_NR_SRC})
target_link_libraries(SCHED_NR_LIB PRIVATE asn1_nr_rrc_hdrs UTIL)
target_link_libraries(SCHED_NR_LIB PRIVATE nfapi_nr_lib nfapi_user_lib) # some FAPI messages pack "subPDUs"
if(E3_AGENT)
  # the PHY tap needs e3ap in every consumer of SCHED_NR_LIB, not just nr-softmodem
  target_link_libraries(SCHED_NR_LIB PRIVATE e3ap)
  target_compile_definitions(SCHED_NR_LIB PRIVATE E3_AGENT)
endif()

set(SCHED_SRC_RU
  ${OPENAIR1_DIR}/SCHED/ru_procedures.c
  ${OPENAIR1_DIR}/SCHED/prach_procedures.c
)
add_library(SCHED_RU_LIB ${SCHED_SRC_RU})
target_link_libraries(SCHED_RU_LIB PRIVATE asn1_lte_rrc_hdrs nfapi_user_lib)

set(SCHED_SRC_UE
  ${OPENAIR1_DIR}/SCHED_UE/phy_procedures_lte_ue.c
  ${OPENAIR1_DIR}/SCHED/phy_procedures_lte_common.c
  ${OPENAIR1_DIR}/SCHED_UE/pucch_pc.c
  ${OPENAIR1_DIR}/SCHED_UE/pusch_pc.c
  ${OPENAIR1_DIR}/SCHED_UE/srs_pc.c
)
add_library(SCHED_UE_LIB ${SCHED_SRC_UE})
target_link_libraries(SCHED_UE_LIB PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs UTIL)

set(SCHED_SRC_NR_UE
  ${OPENAIR1_DIR}/SCHED_NR_UE/phy_procedures_nr_ue.c
  ${OPENAIR1_DIR}/SCHED_NR_UE/phy_procedures_nr_ue_sl.c
  ${OPENAIR1_DIR}/SCHED_NR_UE/fapi_nr_ue_l1.c
  ${OPENAIR1_DIR}/SCHED_NR_UE/harq_nr.c
  ${OPENAIR1_DIR}/SCHED_NR_UE/pucch_uci_ue_nr.c
)
add_library(SCHED_NR_UE_LIB ${SCHED_SRC_NR_UE})
target_link_libraries(SCHED_NR_UE_LIB PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs UTIL)

# Layer 1
#############################
set(PHY_TURBOIF
  ${OPENAIR1_DIR}/PHY/CODING/coding_load.c
)

set(PHY_NRLDPC_CODINGIF
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_coding/nrLDPC_coding_interface_load.c
)

add_library(dfts MODULE ${OPENAIR1_DIR}/PHY/TOOLS/oai_dfts.c ${OPENAIR1_DIR}/PHY/TOOLS/oai_dfts_neon.c)
target_compile_options(dfts PRIVATE -fno-semantic-interposition)
target_link_libraries(dfts PRIVATE m)

set(PHY_SRC_COMMON
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/dci_tools_common.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/lte_mcs.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/get_pmi.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/group_hopping.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/phich_common.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pcfich_common.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pmch_common.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/power_control.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/prach_common.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pucch_common.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/dlsch_scrambling.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/srs_modulation.c
  ${OPENAIR1_DIR}/PHY/MODULATION/ofdm_mod.c
  ${OPENAIR1_DIR}/PHY/MODULATION/PHY_ofdm_mod.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_sync_time.c
  ${OPENAIR1_DIR}/PHY/LTE_REFSIG/lte_dl_cell_spec.c
  ${OPENAIR1_DIR}/PHY/LTE_REFSIG/lte_dl_uespec.c
  ${OPENAIR1_DIR}/PHY/LTE_REFSIG/lte_gold.c
  ${OPENAIR1_DIR}/PHY/LTE_REFSIG/lte_gold_mbsfn.c
  ${OPENAIR1_DIR}/PHY/LTE_REFSIG/lte_dl_mbsfn.c
  ${OPENAIR1_DIR}/PHY/LTE_REFSIG/lte_ul_ref.c
  ${OPENAIR1_DIR}/PHY/CODING/lte_segmentation.c
  ${OPENAIR1_DIR}/PHY/CODING/ccoding_byte.c
  ${OPENAIR1_DIR}/PHY/CODING/ccoding_byte_lte.c
  ${OPENAIR1_DIR}/PHY/CODING/3gpplte_sse.c
  ${PHY_TURBOIF}
  ${OPENAIR1_DIR}/PHY/CODING/lte_rate_matching.c
  ${OPENAIR1_DIR}/PHY/CODING/viterbi.c
  ${OPENAIR1_DIR}/PHY/CODING/viterbi_lte.c
  ${OPENAIR1_DIR}/PHY/INIT/init_top.c
  ${OPENAIR1_DIR}/PHY/INIT/lte_parms.c
  )

set(PHY_SRC
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pss.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/sss.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/sss_gen.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pilots.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pilots_mbsfn.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/dlsch_coding.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/dlsch_modulation.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/ulsch_demodulation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_ul_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/freq_equalization.c  
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_adjust_sync_eNB.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/dci_tools.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pbch.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/dci.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/edci.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/phich.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pcfich.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pucch.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/pmch.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/ulsch_decoding.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/rar_tools.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/uci_tools.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_eNB_measurements.c
  ${OPENAIR1_DIR}/PHY/INIT/lte_init.c
  )

set(PHY_SRC_RU
  ${OPENAIR1_DIR}/PHY/if4_tools.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/drs_modulation.c
  ${OPENAIR1_DIR}/PHY/MODULATION/slot_fep_ul.c
  ${OPENAIR1_DIR}/PHY/MODULATION/ul_7_5_kHz.c
  ${OPENAIR1_DIR}/PHY/MODULATION/gen_75KHz.cpp
  ${OPENAIR1_DIR}/PHY/MODULATION/beamforming.c
  ${OPENAIR1_DIR}/PHY/MODULATION/compute_bf_weights.c
  ${OPENAIR1_DIR}/PHY/INIT/lte_init_ru.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/prach.c
  )

set(NR_PHY_SRC_RU
  ${OPENAIR1_DIR}/PHY/MODULATION/nr_beamforming.c
  ${OPENAIR1_DIR}/PHY/MODULATION/slot_fep_nr.c
  ${OPENAIR1_DIR}/PHY/INIT/nr_init_ru.c
  )

set(PHY_SRC_UE
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/sss_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/sss_gen.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/dlsch_demodulation.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/dlsch_llr_computation.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/dlsch_decoding.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/dci_tools_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/uci_tools_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/pbch_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/dci_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/phich_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/pcfich_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/pucch_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/prach_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/pmch_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/pch_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/slss.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/sldch.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/slsch.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/drs_modulation.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/ulsch_modulation.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/ulsch_coding.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/rar_tools_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/initial_sync.c
  ${OPENAIR1_DIR}/PHY/LTE_UE_TRANSPORT/dlsch_llr_computation_avx2.c
  ${OPENAIR1_DIR}/PHY/MODULATION/slot_fep.c
  ${OPENAIR1_DIR}/PHY/MODULATION/slot_fep_mbsfn.c
  ${OPENAIR1_DIR}/PHY/MODULATION/ul_7_5_kHz_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_sync_time.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_sync_timefreq.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_adjust_sync_ue.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_dl_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_dl_bf_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_dl_mbsfn_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_est_freq_offset.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_ue_measurements.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/adjust_gain.c
  ${OPENAIR1_DIR}/PHY/INIT/lte_init_ue.c
  )

  set(PHY_NR_SRC_COMMON
  ${OPENAIR1_DIR}/PHY/CODING/nr_segmentation.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_tbs_tools.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_prach_common.c
  ${OPENAIR1_DIR}/PHY/nr_phy_common/src/nr_phy_common_csirs.c
  ${OPENAIR1_DIR}/PHY/nr_phy_common/src/nr_phy_common_srs.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_scrambling.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/refsig.c
  ${OPENAIR1_DIR}/PHY/MODULATION/ofdm_mod_nr.c
  ${OPENAIR1_DIR}/PHY/MODULATION/PHY_ofdm_mod.c
  )

  set(PHY_NR_SRC
  ${OPENAIR1_DIR}/PHY/INIT/nr_init.c
  ${OPENAIR1_DIR}/PHY/INIT/nr_parms.c
  ${OPENAIR1_DIR}/PHY/MODULATION/nr_modulation.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_prs.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_pss.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_sss.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_pbch.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_dci.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_dci_tools.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_dlsch.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_dlsch_coding.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_ulsch_decoding.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_uci_decoding.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_ulsch.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_sch_dmrs.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_prach.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_ulsch_demodulation.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/ul_ref_seq_nr.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/nr_dmrs_rx.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/nr_gen_mod_table.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/dmrs_nr.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/ptrs_nr.c
  ${OPENAIR1_DIR}/PHY/NR_ESTIMATION/nr_ul_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/NR_ESTIMATION/nr_freq_equalization.c
  ${OPENAIR1_DIR}/PHY/NR_ESTIMATION/nr_measurements_gNB.c
  ${PHY_NRLDPC_CODINGIF}
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/pucch_rx.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/srs_rx.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_uci_tools_common.c
  )
  set(PHY_NR_UE_SRC
  ${OPENAIR1_DIR}/PHY/INIT/nr_parms.c
  ${OPENAIR1_DIR}/PHY/MODULATION/nr_modulation.c
  ${OPENAIR1_DIR}/PHY/MODULATION/slot_fep_nr.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/pss_nr.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/sss_nr.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/cic_filter_nr.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_initial_sync.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_initial_sync_sl.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_ntn_l1.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_ue_rf_helpers.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_pbch.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_psbch_rx.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_psbch_tx.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_dlsch_demodulation.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_ulsch_coding.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_dlsch_decoding.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_prach_common.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_sch_dmrs.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_prach.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/dci_nr.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/pucch_nr.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/csi_rx.c
  ${OPENAIR1_DIR}/PHY/NR_TRANSPORT/nr_uci_tools_common.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/nr_ulsch_ue.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/ul_ref_seq_nr.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/nr_dmrs_rx.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/dmrs_nr.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/ptrs_nr.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/nr_gold_ue.c
  ${OPENAIR1_DIR}/PHY/NR_REFSIG/nr_gen_mod_table.c
  ${OPENAIR1_DIR}/PHY/NR_UE_ESTIMATION/nr_dl_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/NR_UE_ESTIMATION/nr_adjust_synch_ue.c
  ${OPENAIR1_DIR}/PHY/NR_UE_ESTIMATION/nr_ue_measurements.c
  ${OPENAIR1_DIR}/PHY/NR_UE_ESTIMATION/nr_adjust_gain.c
  ${OPENAIR1_DIR}/PHY/INIT/nr_init_ue.c
  ${PHY_NRLDPC_CODINGIF}
  )


if (${SMBV})
  set(PHY_SRC "${PHY_SRC} ${OPENAIR1_DIR}/PHY/TOOLS/smbv.c")
endif  (${SMBV})

add_library(PHY_COMMON ${PHY_SRC_COMMON})
target_link_libraries(PHY_COMMON
  PRIVATE asn1_lte_rrc_hdrs crc_byte
  PUBLIC UTIL PHY_TOOLS
)
add_dependencies(PHY_COMMON dfts)

add_library(PHY ${PHY_SRC})
target_link_libraries(PHY PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)

add_library(PHY_UE ${PHY_SRC_UE})
target_link_libraries(PHY_UE PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs UTIL)

add_library(PHY_NR_COMMON ${PHY_NR_SRC_COMMON})
target_link_libraries(PHY_NR_COMMON PUBLIC UTIL PHY_TOOLS crc_byte)

if (ENABLE_LDPC_CUDA AND LDPC_GPU_BACKEND STREQUAL "CUDA")
  find_package(CUDA REQUIRED)
  if (NOT CUDA_FOUND)
    message(FATAL_ERROR "no CUDA found")
  endif()
  #SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS};-arch=sm_80 -g -ggdb") # A100
  #SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS};-arch=sm_90") # GH200
  #SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS};-arch=sm_87") # Jetson
  #SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS};-arch=sm_89") # L40S
  SET(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS};-arch=sm_121") # GB10
  set(CUDA_VERBOS_BUILD_ON)
  cuda_add_library(PHY_NR ${PHY_NR_SRC} ${PHY_NR_SRC_CU})
  cuda_add_library(PHY_NR_UE ${PHY_NR_UE_SRC} ${PHY_NR_UE_SRC_CU})
else()
  add_library(PHY_NR ${PHY_NR_SRC})
  add_library(PHY_NR_UE ${PHY_NR_UE_SRC})
endif()
target_link_libraries(PHY_NR PHY_NR_COMMON nr_phy_common nr_common nr_fapi_p5 polar smallblock ds)

add_library(PHY_NR_NO_AVX_256 ${PHY_NR_SRC})
target_link_libraries(PHY_NR_NO_AVX_256 PHY_NR_COMMON nr_phy_common_no_avx_256 nr_common)
target_compile_definitions(PHY_NR_NO_AVX_256 PUBLIC USE_128BIT)

target_link_libraries(PHY_NR_UE asn1_nr_rrc_hdrs nr_phy_common nr_common UTIL polar smallblock)

if (ENABLE_LDPC_CUDA AND LDPC_GPU_BACKEND STREQUAL "HIP")
  # these compile the LDPC_CUDA-guarded pinned-memory paths, which call into the
  # HIP runtime; linked publicly so the consuming executables inherit it
  target_link_libraries(PHY_NR hip::host)
  target_link_libraries(PHY_NR_UE hip::host)
  target_link_libraries(PHY_NR_NO_AVX_256 hip::host)
endif()

add_library(PHY_RU ${PHY_SRC_RU})
target_link_libraries(PHY_RU PRIVATE asn1_lte_rrc_hdrs UTIL)

add_library(NR_PHY_RU ${NR_PHY_SRC_RU})

#Layer 2 library
#####################
set(MAC_DIR ${OPENAIR2_DIR}/LAYER2/MAC)
set(NR_GNB_MAC_DIR ${OPENAIR2_DIR}/LAYER2/NR_MAC_gNB)
set(NR_UE_MAC_DIR ${OPENAIR2_DIR}/LAYER2/NR_MAC_UE)
set(PHY_INTERFACE_DIR ${OPENAIR2_DIR}/PHY_INTERFACE)
set(NR_PHY_INTERFACE_DIR ${OPENAIR2_DIR}/NR_PHY_INTERFACE)
set(NR_UE_PHY_INTERFACE_DIR ${OPENAIR2_DIR}/NR_UE_PHY_INTERFACE)
set(RRC_DIR ${OPENAIR2_DIR}/RRC/LTE)
set(NR_RRC_DIR ${OPENAIR2_DIR}/RRC/NR)
set(NR_UE_RRC_DIR ${OPENAIR2_DIR}/RRC/NR_UE)
set(PDCP_DIR  ${OPENAIR2_DIR}/LAYER2/PDCP_v10.1.0)

set(RLC_V2
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_oai_api.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/asn1_utils.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_ue_manager.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_entity.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_entity_am.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_entity_um.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_pdu.c
  ${OPENAIR2_DIR}/LAYER2/rlc_v2/rlc_sdu.c
  )

set(NR_PDCP_SRC
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_oai_api.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_ue_manager.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_entity.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_sdu.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_timer_thread.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_security_nea2.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_security_nea1.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_integrity_nia2.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/nr_pdcp_integrity_nia1.c
  ${OPENAIR2_DIR}/LAYER2/nr_pdcp/asn1_utils.c
  )

# gNB: PDCP + CU-CP/CU-UP interface
set(NR_PDCP_SRC_GNB
  ${NR_PDCP_SRC}
  openair2/LAYER2/nr_pdcp/cucp_cuup_handler.c
  openair2/LAYER2/nr_pdcp/cuup_cucp_if.c
  openair2/LAYER2/nr_pdcp/cuup_cucp_direct.c
  openair2/LAYER2/nr_pdcp/cuup_cucp_e1ap.c
  )

# UE build: PDCP only; CU-CP/CU-UP interface is gNB-only.
set(NR_PDCP_SRC_UE ${NR_PDCP_SRC})

set(NR_SDAP_SRC
  ${OPENAIR2_DIR}/SDAP/nr_sdap/nr_sdap.c
  ${OPENAIR2_DIR}/SDAP/nr_sdap/nr_sdap_entity.c
  ${OPENAIR2_DIR}/SDAP/nr_sdap/nr_sdap_qos_rule.c
  )

set(L2_SRC
  ${PDCP_DIR}/pdcp.c
  ${PDCP_DIR}/pdcp_fifo.c
  ${PDCP_DIR}/pdcp_sequence_manager.c
  ${PDCP_DIR}/pdcp_primitives.c
  ${PDCP_DIR}/pdcp_util.c
  ${PDCP_DIR}/pdcp_security.c
  ${OPENAIR2_DIR}/LAYER2/openair2_proc.c
  ${RRC_DIR}/rrc_eNB.c
  ${RRC_DIR}/rrc_eNB_endc.c
  ${RRC_DIR}/rrc_eNB_S1AP.c
  ${RRC_DIR}/rrc_eNB_M2AP.c
  ${RRC_DIR}/rrc_eNB_UE_context.c
  ${RRC_DIR}/rrc_eNB_GTPV1U.c
  ${RRC_DIR}/rrc_common.c
  ${RRC_DIR}/L2_interface.c
  ${RRC_DIR}/L2_interface_common.c
  ${RRC_DIR}/L2_interface_ue.c
  )

set(L2_RRC_SRC
  ${OPENAIR2_DIR}/LAYER2/openair2_proc.c
  ${RRC_DIR}/rrc_eNB.c
  ${RRC_DIR}/rrc_eNB_endc.c
  ${RRC_DIR}/rrc_eNB_S1AP.c
  ${RRC_DIR}/rrc_eNB_M2AP.c
  ${RRC_DIR}/rrc_eNB_UE_context.c
  ${RRC_DIR}/rrc_common.c
  ${RRC_DIR}/L2_interface.c
  ${RRC_DIR}/L2_interface_common.c
  ${RRC_DIR}/L2_interface_ue.c
  )

set(L2_LTE_SRC
  ${RLC_V2}
  )

set(L2_NR_SRC
  ${NR_PDCP_SRC_GNB}
  ${NR_SDAP_SRC}
  ${NR_RRC_DIR}/rrc_gNB.c
  ${NR_RRC_DIR}/mac_rrc_dl_direct.c
  ${NR_RRC_DIR}/mac_rrc_dl_f1ap.c
  ${NR_RRC_DIR}/rrc_gNB_asn1.c
  ${NR_RRC_DIR}/rrc_gNB_nsa.c
  ${NR_RRC_DIR}/rrc_gNB_UE_context.c
  ${NR_RRC_DIR}/rrc_gNB_NGAP.c
  ${NR_RRC_DIR}/rrc_gNB_radio_bearers.c
  ${NR_RRC_DIR}/rrc_gNB_cuup.c
  ${NR_RRC_DIR}/rrc_gNB_du.c
  ${NR_RRC_DIR}/rrc_gNB_mobility.c
  ${NR_RRC_DIR}/rrc_gNB_NRPPA.c
  ${NR_RRC_DIR}/rrc_gNB_nrdc.c
  )

set(L2_SRC_UE
  ${PDCP_DIR}/pdcp.c
  ${PDCP_DIR}/pdcp_fifo.c
  ${PDCP_DIR}/pdcp_sequence_manager.c
  ${PDCP_DIR}/pdcp_primitives.c
  ${PDCP_DIR}/pdcp_util.c
  ${PDCP_DIR}/pdcp_security.c
  ${RRC_DIR}/rrc_UE.c
  ${RRC_DIR}/rrc_common.c
  ${RRC_DIR}/L2_interface_common.c
  ${RRC_DIR}/L2_interface_ue.c
  )

set(L2_RRC_SRC_UE
  ${RRC_DIR}/rrc_UE.c
  ${RRC_DIR}/rrc_common.c
  ${RRC_DIR}/L2_interface_common.c
  ${RRC_DIR}/L2_interface_ue.c
  )

set(NR_L2_SRC_UE
  ${NR_PDCP_SRC_UE}
  ${NR_SDAP_SRC}
  ${NR_UE_RRC_DIR}/L2_interface_ue.c
  ${NR_UE_RRC_DIR}/main_ue.c
  ${NR_UE_RRC_DIR}/rrc_UE.c
  ${NR_UE_RRC_DIR}/verify_RRC.c
  ${NR_UE_RRC_DIR}/rrc_timers_and_constants.c
  ${NR_UE_RRC_DIR}/rrc_sl_preconfig.c
  )

set (MAC_SRC
  #${PHY_INTERFACE_DIR}/phy_stub_UE.c
  ${PHY_INTERFACE_DIR}/IF_Module.c
  ${MAC_DIR}/main.c
  #${MAC_DIR}/main_ue.c
  #${MAC_DIR}/ue_procedures.c
  #${MAC_DIR}/ra_procedures.c
  ${MAC_DIR}/l1_helpers.c
  ${MAC_DIR}/rar_tools.c
  #${MAC_DIR}/rar_tools_ue.c
  ${MAC_DIR}/eNB_scheduler.c
  ${MAC_DIR}/eNB_scheduler_dlsch.c
  ${MAC_DIR}/eNB_scheduler_ulsch.c
  ${MAC_DIR}/eNB_scheduler_mch.c
  ${MAC_DIR}/eNB_scheduler_bch.c
  ${MAC_DIR}/eNB_scheduler_primitives.c
  ${MAC_DIR}/eNB_scheduler_RA.c
  ${MAC_DIR}/eNB_scheduler_fairRR.c
  ${MAC_DIR}/eNB_scheduler_phytest.c
  ${MAC_DIR}/pre_processor.c
  ${MAC_DIR}/slicing/slicing.c
  ${MAC_DIR}/config.c
  ${MAC_DIR}/config_ue.c
 )

set (MAC_NR_SRC
  ${NR_PHY_INTERFACE_DIR}/NR_IF_Module.c
  ${NR_GNB_MAC_DIR}/main.c
  ${NR_GNB_MAC_DIR}/config.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_bch.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_dlsch.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_dlsch_default_policies.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_ulsch.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_ulsch_default_policies.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_primitives.c
  ${NR_GNB_MAC_DIR}/nr_mac_cell.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_phytest.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_uci.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_srs.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_RA.c
  ${NR_GNB_MAC_DIR}/gNB_scheduler_pcch.c
  ${NR_GNB_MAC_DIR}/mac_rrc_dl_handler.c
  ${NR_GNB_MAC_DIR}/mac_rrc_ul_direct.c
  ${NR_GNB_MAC_DIR}/mac_rrc_ul_f1ap.c
  ${NR_GNB_MAC_DIR}/nr_radio_config.c
  ${NR_GNB_MAC_DIR}/nr_pos_ue_context.c
  ${NR_GNB_MAC_DIR}/nrdc_mac_rrc_dl_handler.c
 )


set (MAC_SRC_UE
  ${PHY_INTERFACE_DIR}/phy_stub_UE.c
  ${MAC_DIR}/main_ue.c
  ${MAC_DIR}/ue_procedures.c
  ${MAC_DIR}/ra_procedures.c
  ${MAC_DIR}/l1_helpers.c
  ${MAC_DIR}/rar_tools_ue.c
  ${MAC_DIR}/config_ue.c
 )

set (MAC_NR_SRC_UE
  ${NR_UE_PHY_INTERFACE_DIR}/NR_IF_Module.c
  ${NR_UE_PHY_INTERFACE_DIR}/NR_Packet_Drop.c
  ${NR_UE_MAC_DIR}/config_ue.c
  ${NR_UE_MAC_DIR}/config_ue_sl.c
  ${NR_UE_MAC_DIR}/mac_tables.c
  ${NR_UE_MAC_DIR}/main_ue_nr.c
  ${NR_UE_MAC_DIR}/nr_ue_procedures.c
  ${NR_UE_MAC_DIR}/nr_ue_procedures_sl.c
  ${NR_UE_MAC_DIR}/nr_ue_scheduler.c
  ${NR_UE_MAC_DIR}/nr_ue_scheduler_sl.c
  ${NR_UE_MAC_DIR}/nr_ue_dci_configuration.c
)

set (ENB_APP_SRC
  ${OPENAIR2_DIR}/ENB_APP/enb_app.c
  ${OPENAIR2_DIR}/ENB_APP/enb_config.c
  ${OPENAIR2_DIR}/ENB_APP/enb_config_SL.c
  ${OPENAIR2_DIR}/ENB_APP/enb_config_eMTC.c
  ${OPENAIR2_DIR}/ENB_APP/RRC_config_tools.c
  )

set (GNB_APP_SRC
  ${OPENAIR2_DIR}/GNB_APP/gnb_app.c
  ${OPENAIR2_DIR}/E1AP/e1ap_setup.c
  ${OPENAIR2_DIR}/GNB_APP/gnb_config.c
  ${OPENAIR2_DIR}/GNB_APP/gnb_config_common.c
  ${OPENAIR2_DIR}/GNB_APP/gnb_config_ng.c
  ${OPENAIR2_DIR}/GNB_APP/gnb_config_xn.c
  )

set (MCE_APP_SRC
  ${OPENAIR2_DIR}/MCE_APP/mce_app.c
  ${OPENAIR2_DIR}/MCE_APP/mce_config.c
  )
set (MISC_NFAPI_LTE
 ${OPENAIR1_DIR}/SCHED/nfapi_lte_dummy.c
 )

add_library(MISC_NFAPI_LTE_LIB ${MISC_NFAPI_LTE})
target_link_libraries(MISC_NFAPI_LTE_LIB PRIVATE nfapi_lte_vnf_lib nfapi_pnf_lib)

add_library(L2
  ${L2_SRC}
  ${MAC_SRC}
  ${ENB_APP_SRC}
  ${MCE_APP_SRC}
  )
target_link_libraries(L2 PRIVATE x2ap s1ap lte_rrc m2ap m3ap SCTP_CLIENT)
target_link_libraries(L2 PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)
target_link_libraries(L2 PRIVATE nfapi_lte_user_lib nfapi_pnf_lib)
if(E2_AGENT)
  target_compile_definitions(L2 PRIVATE ${E2AP_VERSION} ${KPM_VERSION} E2_AGENT)
endif()

add_library(MAC_UE_NR ${MAC_NR_SRC_UE})
target_link_libraries(MAC_UE_NR PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs PUBLIC nr_ue_power_procedures nr_ue_ra_procedures)

add_library(L2_LTE ${L2_LTE_SRC})
target_link_libraries(L2_LTE PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)

add_library(L2_NR
  ${L2_NR_SRC}
  ${MAC_NR_SRC}
  ${GNB_APP_SRC}
  )
if(E3_AGENT)
  target_compile_definitions(L2_NR PRIVATE E3_AGENT)
endif()
target_link_libraries(L2_NR PRIVATE ds alg)
target_link_libraries(L2_NR PRIVATE f1ap f1ap_lib)
target_link_libraries(L2_NR PRIVATE nfapi_nr_user_lib)
target_link_libraries(L2_NR PRIVATE nrppa)
target_include_directories(L2_NR PRIVATE ${F1AP_DIR}/lib)
if(E2_AGENT)
  target_compile_definitions(L2_NR PRIVATE ${E2AP_VERSION} ${KPM_VERSION} E2_AGENT)
endif()

add_library(e1_if
  ${NR_RRC_DIR}/cucp_cuup_direct.c
  ${NR_RRC_DIR}/cucp_cuup_e1ap.c
  )

target_link_libraries(e1_if PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs asn1_f1ap SECURITY e1ap GTPV1U)

target_link_libraries(L2_NR PRIVATE f1ap x2ap s1ap ngap nr_rrc e1ap nr_rlc nr_common xnap)
target_compile_definitions(L2_NR PRIVATE PDCP_CUCP_CUUP)
if(OAI_AERIAL)
    target_compile_definitions(L2_NR PRIVATE ENABLE_AERIAL)
endif()

add_library(L2_LTE_NR
  # temporary solution until 4G/5G code completely untangled (as evidenced by deletion of the following file)
  ${MAC_DIR}/dummy_functions.c
)

target_link_libraries(L2_LTE_NR PRIVATE f1ap s1ap nr_rrc)
target_link_libraries(L2_LTE_NR PRIVATE nfapi_lte_vnf_lib nfapi_nr_vnf_lib)

add_library(L2_UE
  ${L2_SRC_UE}
  ${MAC_SRC_UE}
)
target_link_libraries(L2_UE PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)
target_link_libraries(L2_UE PRIVATE GTPV1U)
target_link_libraries(L2_UE PRIVATE nfapi_pnf_lib)

target_link_libraries(L2_UE PRIVATE asn1_lte_rrc_hdrs)

add_library( NR_L2_UE ${NR_L2_SRC_UE} ${MAC_NR_SRC_UE} )
target_link_libraries(NR_L2_UE PRIVATE nr_rlc ds alg)
target_link_libraries(NR_L2_UE PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs nr_common nr_ue_power_procedures nr_ue_ra_procedures)
target_link_libraries(NR_L2_UE PRIVATE nr_nas)

# L3 Libs
##########################

# CN libs
##########################

set (MME_APP_SRC
  ${OPENAIR3_DIR}/MME_APP/mme_app.c
  ${OPENAIR3_DIR}/MME_APP/mme_config.c
)
add_library(MME_APP ${MME_APP_SRC})
target_link_libraries(MME_APP PRIVATE m2ap m3ap)
target_link_libraries(MME_APP PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)
target_link_libraries(MME_APP PRIVATE nfapi_lte_user_lib)

set(NAS_SRC ${OPENAIR3_DIR}/NAS/)

set(libnas_api_OBJS
  ${NAS_SRC}COMMON/API/NETWORK/as_message.c
  ${NAS_SRC}COMMON/API/NETWORK/nas_message.c
  ${NAS_SRC}COMMON/API/NETWORK/network_api.c
  )

set(libnas_emm_msg_OBJS
  ${NAS_SRC}COMMON/EMM/MSG/AttachAccept.c
  ${NAS_SRC}COMMON/EMM/MSG/AttachComplete.c
  ${NAS_SRC}COMMON/EMM/MSG/AttachReject.c
  ${NAS_SRC}COMMON/EMM/MSG/AttachRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/AuthenticationFailure.c
  ${NAS_SRC}COMMON/EMM/MSG/AuthenticationReject.c
  ${NAS_SRC}COMMON/EMM/MSG/AuthenticationRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/AuthenticationResponse.c
  ${NAS_SRC}COMMON/EMM/MSG/CsServiceNotification.c
  ${NAS_SRC}COMMON/EMM/MSG/DetachAccept.c
  ${NAS_SRC}COMMON/EMM/MSG/DetachRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/DownlinkNasTransport.c
  ${NAS_SRC}COMMON/EMM/MSG/EmmInformation.c
  ${NAS_SRC}COMMON/EMM/MSG/emm_msg.c
  ${NAS_SRC}COMMON/EMM/MSG/EmmStatus.c
  ${NAS_SRC}COMMON/EMM/MSG/ExtendedServiceRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/GutiReallocationCommand.c
  ${NAS_SRC}COMMON/EMM/MSG/GutiReallocationComplete.c
  ${NAS_SRC}COMMON/EMM/MSG/IdentityRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/IdentityResponse.c
  ${NAS_SRC}COMMON/EMM/MSG/SecurityModeCommand.c
  ${NAS_SRC}COMMON/EMM/MSG/SecurityModeComplete.c
  ${NAS_SRC}COMMON/EMM/MSG/SecurityModeReject.c
  ${NAS_SRC}COMMON/EMM/MSG/ServiceReject.c
  ${NAS_SRC}COMMON/EMM/MSG/ServiceRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/TrackingAreaUpdateAccept.c
  ${NAS_SRC}COMMON/EMM/MSG/TrackingAreaUpdateComplete.c
  ${NAS_SRC}COMMON/EMM/MSG/TrackingAreaUpdateReject.c
  ${NAS_SRC}COMMON/EMM/MSG/TrackingAreaUpdateRequest.c
  ${NAS_SRC}COMMON/EMM/MSG/UplinkNasTransport.c
)

set(libnas_esm_msg_OBJS
  ${NAS_SRC}COMMON/ESM/MSG/ActivateDedicatedEpsBearerContextAccept.c
  ${NAS_SRC}COMMON/ESM/MSG/ActivateDedicatedEpsBearerContextReject.c
  ${NAS_SRC}COMMON/ESM/MSG/ActivateDedicatedEpsBearerContextRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/ActivateDefaultEpsBearerContextAccept.c
  ${NAS_SRC}COMMON/ESM/MSG/ActivateDefaultEpsBearerContextReject.c
  ${NAS_SRC}COMMON/ESM/MSG/ActivateDefaultEpsBearerContextRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/BearerResourceAllocationReject.c
  ${NAS_SRC}COMMON/ESM/MSG/BearerResourceAllocationRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/BearerResourceModificationReject.c
  ${NAS_SRC}COMMON/ESM/MSG/BearerResourceModificationRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/DeactivateEpsBearerContextAccept.c
  ${NAS_SRC}COMMON/ESM/MSG/DeactivateEpsBearerContextRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/EsmInformationRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/EsmInformationResponse.c
  ${NAS_SRC}COMMON/ESM/MSG/esm_msg.c
  ${NAS_SRC}COMMON/ESM/MSG/EsmStatus.c
  ${NAS_SRC}COMMON/ESM/MSG/ModifyEpsBearerContextAccept.c
  ${NAS_SRC}COMMON/ESM/MSG/ModifyEpsBearerContextReject.c
  ${NAS_SRC}COMMON/ESM/MSG/ModifyEpsBearerContextRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/PdnConnectivityReject.c
  ${NAS_SRC}COMMON/ESM/MSG/PdnConnectivityRequest.c
  ${NAS_SRC}COMMON/ESM/MSG/PdnDisconnectReject.c
  ${NAS_SRC}COMMON/ESM/MSG/PdnDisconnectRequest.c
)

set(libnas_ies_OBJS
  ${NAS_SRC}COMMON/IES/AccessPointName.c
  ${NAS_SRC}COMMON/IES/AdditionalUpdateResult.c
  ${NAS_SRC}COMMON/IES/AdditionalUpdateType.c
  ${NAS_SRC}COMMON/IES/ApnAggregateMaximumBitRate.c
  ${NAS_SRC}COMMON/IES/AuthenticationFailureParameter.c
  ${NAS_SRC}COMMON/IES/AuthenticationParameterAutn.c
  ${NAS_SRC}COMMON/IES/AuthenticationParameterRand.c
  ${NAS_SRC}COMMON/IES/AuthenticationResponseParameter.c
  ${NAS_SRC}COMMON/IES/CipheringKeySequenceNumber.c
  ${NAS_SRC}COMMON/IES/Cli.c
  ${NAS_SRC}COMMON/IES/CsfbResponse.c
  ${NAS_SRC}COMMON/IES/DaylightSavingTime.c
  ${NAS_SRC}COMMON/IES/DetachType.c
  ${NAS_SRC}COMMON/IES/DrxParameter.c
  ${NAS_SRC}COMMON/IES/EmergencyNumberList.c
  ${NAS_SRC}COMMON/IES/EmmCause.c
  ${NAS_SRC}COMMON/IES/EpsAttachResult.c
  ${NAS_SRC}COMMON/IES/EpsAttachType.c
  ${NAS_SRC}COMMON/IES/EpsBearerContextStatus.c
  ${NAS_SRC}COMMON/IES/EpsBearerIdentity.c
  ${NAS_SRC}COMMON/IES/EpsMobileIdentity.c
  ${NAS_SRC}COMMON/IES/EpsNetworkFeatureSupport.c
  ${NAS_SRC}COMMON/IES/EpsQualityOfService.c
  ${NAS_SRC}COMMON/IES/EpsUpdateResult.c
  ${NAS_SRC}COMMON/IES/EpsUpdateType.c
  ${NAS_SRC}COMMON/IES/EsmCause.c
  ${NAS_SRC}COMMON/IES/EsmInformationTransferFlag.c
  ${NAS_SRC}COMMON/IES/EsmMessageContainer.c
  ${NAS_SRC}COMMON/IES/GprsTimer.c
  ${NAS_SRC}COMMON/IES/GutiType.c
  ${NAS_SRC}COMMON/IES/IdentityType2.c
  ${NAS_SRC}COMMON/IES/ImeisvRequest.c
  ${NAS_SRC}COMMON/IES/KsiAndSequenceNumber.c
  ${NAS_SRC}COMMON/IES/LcsClientIdentity.c
  ${NAS_SRC}COMMON/IES/LcsIndicator.c
  ${NAS_SRC}COMMON/IES/LinkedEpsBearerIdentity.c
  ${NAS_SRC}COMMON/IES/LlcServiceAccessPointIdentifier.c
  ${NAS_SRC}COMMON/IES/LocationAreaIdentification.c
  ${NAS_SRC}COMMON/IES/MessageType.c
  ${NAS_SRC}COMMON/IES/MobileIdentity.c
  ${NAS_SRC}COMMON/IES/MobileStationClassmark2.c
  ${NAS_SRC}COMMON/IES/MobileStationClassmark3.c
  ${NAS_SRC}COMMON/IES/MsNetworkCapability.c
  ${NAS_SRC}COMMON/IES/MsNetworkFeatureSupport.c
  ${NAS_SRC}COMMON/IES/NasKeySetIdentifier.c
  ${NAS_SRC}COMMON/IES/NasMessageContainer.c
  ${NAS_SRC}COMMON/IES/NasRequestType.c
  ${NAS_SRC}COMMON/IES/NasSecurityAlgorithms.c
  ${NAS_SRC}COMMON/IES/NetworkName.c
  ${NAS_SRC}COMMON/IES/Nonce.c
  ${NAS_SRC}COMMON/IES/PacketFlowIdentifier.c
  ${NAS_SRC}COMMON/IES/PagingIdentity.c
  ${NAS_SRC}COMMON/IES/PdnAddress.c
  ${NAS_SRC}COMMON/IES/PdnType.c
  ${NAS_SRC}COMMON/IES/PlmnList.c
  ${NAS_SRC}COMMON/IES/ProcedureTransactionIdentity.c
  ${NAS_SRC}COMMON/IES/ProtocolConfigurationOptions.c
  ${NAS_SRC}COMMON/IES/ProtocolDiscriminator.c
  ${NAS_SRC}COMMON/IES/PTmsiSignature.c
  ${NAS_SRC}COMMON/IES/QualityOfService.c
  ${NAS_SRC}COMMON/IES/RadioPriority.c
  ${NAS_SRC}COMMON/IES/SecurityHeaderType.c
  ${NAS_SRC}COMMON/IES/ServiceType.c
  ${NAS_SRC}COMMON/IES/ShortMac.c
  ${NAS_SRC}COMMON/IES/SsCode.c
  ${NAS_SRC}COMMON/IES/SupportedCodecList.c
  ${NAS_SRC}COMMON/IES/TimeZoneAndTime.c
  ${NAS_SRC}COMMON/IES/TimeZone.c
  ${NAS_SRC}COMMON/IES/TmsiStatus.c
  ${NAS_SRC}COMMON/IES/TrackingAreaIdentity.c
  ${NAS_SRC}COMMON/IES/TrackingAreaIdentityList.c
  ${NAS_SRC}COMMON/IES/TrafficFlowAggregateDescription.c
  ${NAS_SRC}COMMON/IES/TrafficFlowTemplate.c
  ${NAS_SRC}COMMON/IES/TransactionIdentifier.c
  ${NAS_SRC}COMMON/IES/UeNetworkCapability.c
  ${NAS_SRC}COMMON/IES/UeRadioCapabilityInformationUpdateNeeded.c
  ${NAS_SRC}COMMON/IES/UeSecurityCapability.c
  ${NAS_SRC}COMMON/IES/VoiceDomainPreferenceAndUeUsageSetting.c
)

set (libnas_utils_OBJS
  ${NAS_SRC}COMMON/UTIL/device.c
  common/utils/mem/memory.c
  ${NAS_SRC}COMMON/UTIL/nas_timer.c
  ${NAS_SRC}COMMON/UTIL/socket.c
  ${NAS_SRC}COMMON/UTIL/stty.c
  ${NAS_SRC}COMMON/UTIL/TLVDecoder.c
  ${NAS_SRC}COMMON/UTIL/OctetString.c
)

set(libnas_ue_api_OBJS
  ${NAS_SRC}UE/API/USER/at_command.c
  ${NAS_SRC}UE/API/USER/at_error.c
  ${NAS_SRC}UE/API/USER/at_response.c
  ${NAS_SRC}UE/API/USER/user_api.c
  ${NAS_SRC}UE/API/USER/user_indication.c
  ${NAS_SRC}UE/API/USIM/aka_functions.c
  ${NAS_SRC}UE/API/USIM/usim_api.c
  )
set(libnas_ue_emm_OBJS
  ${NAS_SRC}UE/EMM/Attach.c
  ${NAS_SRC}UE/EMM/Authentication.c
  ${NAS_SRC}UE/EMM/Detach.c
  ${NAS_SRC}UE/EMM/emm_main.c
  ${NAS_SRC}UE/EMM/EmmStatusHdl.c
  ${NAS_SRC}UE/EMM/Identification.c
  ${NAS_SRC}UE/EMM/IdleMode.c
  ${NAS_SRC}UE/EMM/LowerLayer.c
  ${NAS_SRC}UE/EMM/SecurityModeControl.c
  ${NAS_SRC}UE/EMM/ServiceRequestHdl.c
  ${NAS_SRC}UE/EMM/TrackingAreaUpdate.c
  )
set(libnas_ue_emm_sap_OBJS
  ${NAS_SRC}UE/EMM/SAP/emm_as.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredAttachNeeded.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredAttemptingToAttach.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregistered.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredInitiated.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredLimitedService.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredNoCellAvailable.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredNoImsi.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredNormalService.c
  ${NAS_SRC}UE/EMM/SAP/EmmDeregisteredPlmnSearch.c
  ${NAS_SRC}UE/EMM/SAP/emm_esm.c
  ${NAS_SRC}UE/EMM/SAP/emm_fsm.c
  ${NAS_SRC}UE/EMM/SAP/EmmNull.c
  ${NAS_SRC}UE/EMM/SAP/emm_recv.c
  ${NAS_SRC}UE/EMM/SAP/emm_reg.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredAttemptingToUpdate.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegistered.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredImsiDetachInitiated.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredInitiated.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredLimitedService.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredNoCellAvailable.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredNormalService.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredPlmnSearch.c
  ${NAS_SRC}UE/EMM/SAP/EmmRegisteredUpdateNeeded.c
  ${NAS_SRC}UE/EMM/SAP/emm_sap.c
  ${NAS_SRC}UE/EMM/SAP/emm_send.c
  ${NAS_SRC}UE/EMM/SAP/EmmServiceRequestInitiated.c
  ${NAS_SRC}UE/EMM/SAP/EmmTrackingAreaUpdatingInitiated.c
  )
set (libnas_ue_esm_OBJS
  ${NAS_SRC}UE/ESM/DedicatedEpsBearerContextActivation.c
  ${NAS_SRC}UE/ESM/DefaultEpsBearerContextActivation.c
  ${NAS_SRC}UE/ESM/EpsBearerContextDeactivation.c
  ${NAS_SRC}UE/ESM/esm_ebr.c
  ${NAS_SRC}UE/ESM/esm_ebr_context.c
  ${NAS_SRC}UE/ESM/esm_ip.c
  ${NAS_SRC}UE/ESM/esm_main.c
  ${NAS_SRC}UE/ESM/esm_pt.c
  ${NAS_SRC}UE/ESM/EsmStatusHdl.c
  ${NAS_SRC}UE/ESM/PdnConnectivity.c
  ${NAS_SRC}UE/ESM/PdnDisconnect.c
  )
set(libnas_ue_esm_sap_OBJS
  ${NAS_SRC}UE/ESM/SAP/esm_recv.c
  ${NAS_SRC}UE/ESM/SAP/esm_send.c
  ${NAS_SRC}UE/ESM/SAP/esm_sap.c
  )

add_library(LIB_NAS_UE
  ${NAS_SRC}UE/nas_itti_messaging.c
  ${NAS_SRC}UE/nas_network.c
  ${NAS_SRC}UE/nas_parser.c
  ${NAS_SRC}UE/nas_proc.c
  ${NAS_SRC}UE/nas_user.c
  ${libnas_api_OBJS}
  ${libnas_ue_api_OBJS}
  ${libnas_emm_msg_OBJS}
  ${libnas_esm_msg_OBJS}
  ${libnas_ies_OBJS}
  ${libnas_utils_OBJS}
  ${libnas_ue_emm_OBJS}
  ${libnas_ue_emm_sap_OBJS}
  ${libnas_ue_esm_OBJS}
  ${libnas_ue_esm_sap_OBJS}
  $<TARGET_OBJECTS:ds>
  )
target_include_directories(LIB_NAS_UE PRIVATE ${OPENAIR_DIR}/common/utils/ds/)
set(NAS_UE_LIB LIB_NAS_UE)
target_link_libraries(LIB_NAS_UE PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs SECURITY)

include_directories(${NAS_SRC}NR_UE)
include_directories(${NAS_SRC}UE)
include_directories(${NAS_SRC}UE/API/USER)
include_directories(${NAS_SRC}UE/API/USIM)
include_directories(${NAS_SRC}UE/EMM)
include_directories(${NAS_SRC}UE/EMM/MSG)
include_directories(${NAS_SRC}UE/EMM/SAP)
include_directories(${NAS_SRC}UE/ESM)
include_directories(${NAS_SRC}UE/ESM/SAP)

# Simulation library
##########################
set (SIMUSRC
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/taus.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/multipath_channel.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/multipath_tv_channel.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/abstraction.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/channel_sim.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/random_channel.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/rangen_double.c
  ${OPENAIR1_DIR}/SIMULATION/TOOLS/phase_noise.c
  ${OPENAIR1_DIR}/SIMULATION/RF/rf.c
  ${OPENAIR1_DIR}/SIMULATION/RF/dac.c
  ${OPENAIR1_DIR}/SIMULATION/RF/adc.c
)
add_library(SIMU STATIC ${SIMUSRC} )
target_include_directories(SIMU PUBLIC ${OPENAIR1_DIR}/SIMULATION/TOOLS ${OPENAIR1_DIR}/SIMULATION/RF)

option(CHANNEL_SSE "Enable SSE optimizations for channel simulation" OFF)
if(CHANNEL_SSE)
    message(STATUS "SSE-optimized channel simulation enabled.")
    target_compile_definitions(SIMU PRIVATE CHANNEL_SSE)
endif()

if(CUDA_FOUND)
  set_property(TARGET SIMU PROPERTY CUDA_ARCHITECTURES ${CMAKE_CUDA_ARCHITECTURES})
  set_property(TARGET SIMU PROPERTY LINKER_LANGUAGE CXX)
endif()

include_directories("${NFAPI_DIR}/nfapi/public_inc")
include_directories("${NFAPI_DIR}/common/public_inc")

add_library(oai_iqplayer MODULE ${OPENAIR_DIR}/radio/iqplayer/iqplayer_lib.c)
target_link_libraries(oai_iqplayer PRIVATE log_headers)

#################################
# add executables for operation
#################################
add_library(minimal_lib
  ${OPENAIR_DIR}/common/utils/minimal_stub.c
  )
target_link_libraries(minimal_lib PUBLIC pthread dl ${T_LIB} LOG)

add_executable(measurement_display
  ${OPENAIR_DIR}/common/utils/threadPool/measurement_display.c)
target_link_libraries (measurement_display minimal_lib)

# lte-softmodem is both eNB and UE implementation
###################################################

add_executable(lte-softmodem
  ${OPENAIR_DIR}/executables/lte-enb.c
  ${OPENAIR_DIR}/executables/lte-ru.c
  ${OPENAIR_DIR}/executables/ru_control.c
  ${OPENAIR_DIR}/executables/lte-softmodem.c
  ${OPENAIR2_DIR}/ENB_APP/NB_IoT_interface.c
  ${OPENAIR_DIR}/executables/create_tasks.c
  ${OPENAIR_DIR}/executables/create_tasks_mbms.c
  ${OPENAIR3_DIR}/NAS/UE/nas_ue_task.c
  ${OPENAIR_DIR}/common/utils/lte/ue_power.c
  ${OPENAIR_DIR}/common/utils/lte/prach_utils.c
  ${OPENAIR1_DIR}/PHY/TOOLS/phy_scope_interface.c
  )
add_dependencies(lte-softmodem oai_iqplayer)

target_link_libraries(lte-softmodem PRIVATE
  -Wl,--start-group
  lte_rrc nr_rrc s1ap m2ap x2ap m3ap GTPV1U SECURITY UTIL SCTP_CLIENT MME_APP SCHED_LIB SCHED_RU_LIB
  PHY_COMMON PHY PHY_RU  L2 L2_LTE nfapi_lte_vnf_lib nfapi_pnf_lib nfapi_user_lib nfapi_lte_user_lib MISC_NFAPI_LTE_LIB
  ${NAS_UE_LIB} ITTI SIMU radio_common softmodem_common
  -Wl,--end-group dl)

target_link_libraries(lte-softmodem PRIVATE pthread m CONFIG_LIB rt)
target_link_libraries(lte-softmodem PRIVATE ${T_LIB})
target_link_libraries(lte-softmodem PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

if(E2_AGENT)
  target_link_libraries(lte-softmodem PUBLIC e2_agent e2_agent_arg e2_ran_func_du_cucp_cuup)
  target_compile_definitions(lte-softmodem PRIVATE ${E2AP_VERSION} ${KPM_VERSION} E2_AGENT)
endif()

add_executable(oairu
  ${OPENAIR_DIR}/executables/lte-ru.c
  ${OPENAIR_DIR}/executables/ru_control.c
  ${OPENAIR_DIR}/openair1/SCHED/phy_procedures_lte_common.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_ul_channel_estimation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/freq_equalization.c
  ${OPENAIR1_DIR}/PHY/LTE_TRANSPORT/ulsch_demodulation.c
  ${OPENAIR1_DIR}/PHY/LTE_ESTIMATION/lte_adjust_sync_eNB.c
  ${OPENAIR_DIR}/executables/main_ru.c
  ${OPENAIR_DIR}/common/utils/lte/prach_utils.c
)
target_link_libraries(oairu PRIVATE
  -Wl,--start-group
  SCHED_RU_LIB PHY_COMMON PHY_RU UTIL radio_common softmodem_common
  -Wl,--end-group dl)

target_link_libraries(oairu PRIVATE pthread m CONFIG_LIB rt ${T_LIB})
target_link_libraries(oairu PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

# force the generation of ASN.1 so that we don't need to wait during the build
target_link_libraries(lte-softmodem PRIVATE
                      asn1_lte_rrc asn1_s1ap asn1_m2ap asn1_m3ap asn1_x2ap)


# lte-uesoftmodem is  UE implementation
#######################################

add_executable(lte-uesoftmodem
  ${OPENAIR_DIR}/executables/lte-ue.c
  ${OPENAIR_DIR}/executables/lte-uesoftmodem.c
  ${OPENAIR_DIR}/executables/create_tasks_ue.c
  ${OPENAIR3_DIR}/NAS/UE/nas_ue_task.c
  ${OPENAIR_DIR}/common/utils/lte/ue_power.c
  ${OPENAIR_DIR}/common/utils/lte/prach_utils.c
  ${OPENAIR1_DIR}/PHY/TOOLS/phy_scope_interface.c
  )

target_link_libraries(lte-uesoftmodem PRIVATE
  -Wl,--start-group
  lte_rrc nr_rrc s1ap x2ap m2ap m3ap
  SECURITY UTIL SCTP_CLIENT MME_APP SCHED_RU_LIB SCHED_UE_LIB PHY_COMMON
  PHY_UE PHY_RU L2_UE L2_LTE SIMU nfapi_pnf_lib nfapi_user_lib MISC_NFAPI_LTE_LIB
  ${NAS_UE_LIB} ITTI radio_common softmodem_common
  -Wl,--end-group dl)

target_link_libraries(lte-uesoftmodem PRIVATE pthread m CONFIG_LIB rt)
target_link_libraries(lte-uesoftmodem PRIVATE ${T_LIB})
target_link_libraries(lte-uesoftmodem PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

# force the generation of ASN.1 so that we don't need to wait during the build
target_link_libraries(lte-uesoftmodem PRIVATE
                      asn1_lte_rrc asn1_s1ap asn1_m2ap asn1_m3ap asn1_x2ap)

if (OAI_RU_FRONTHAUL)
  add_executable(nr-oru
    ${OPENAIR_DIR}/executables/nr-ru.c
    ${OPENAIR_DIR}/openair1/PHY/INIT/nr_parms.c
    ${OPENAIR_DIR}/openair1/SCHED_NR/phy_frame_config_nr.c
    ${OPENAIR_DIR}/openair1/SCHED_NR/nr_prach_procedures.c
    ${OPENAIR_DIR}/openair1/SCHED_NR/nr_ru_procedures.c
    ${OPENAIR_DIR}/openair1/SCHED/phy_procedures_lte_common.c
    ${OPENAIR_DIR}/executables/main_nr_ru.c
    ${OPENAIR_DIR}/executables/nr-oru.c
  )
  target_link_libraries(nr-oru PRIVATE
    UTIL NR_PHY_RU PHY_NR shlib_loader dl oru_fh oru_beamforming MAC_NR_COMMON PHY_NR_COMMON
    radio_common softmodem_common nfapi_pnf_lib symbol_reorder)
  target_link_libraries(nr-oru PRIVATE pthread m CONFIG_LIB rt ${T_LIB} utils
    barrier actor nfapi_user_lib)
  target_link_libraries(nr-oru PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs time_management ds)
endif()
# nr-softmodem
###################################################

add_executable(nr-softmodem
  ${rrc_h}
  ${nr_rrc_h}
  ${s1ap_h}
  ${OPENAIR_DIR}/executables/nr-gnb.c
  ${OPENAIR_DIR}/executables/nr-ru.c
  ${OPENAIR_DIR}/executables/nr-softmodem.c
  ${OPENAIR_DIR}/common/utils/lte/ue_power.c
  ${OPENAIR_DIR}/common/utils/lte/prach_utils.c
  ${OPENAIR1_DIR}/PHY/TOOLS/phy_scope_interface.c
  )

target_link_libraries(nr-softmodem PRIVATE
  -Wl,--start-group
  UTIL SCTP_CLIENT SCHED_LIB SCHED_NR_LIB PHY_NR PHY PHY_NR_COMMON NR_PHY_RU GTPV1U SECURITY
  ITTI ${NAS_UE_LIB} nr_rrc
  ngap s1ap L2_LTE_NR L2_NR MAC_NR_COMMON nfapi_nr_vnf_lib nfapi_pnf_lib nfapi_user_lib nfapi_lte_user_lib nfapi_nr_user_lib SIMU
  x2ap f1ap m2ap m3ap e1ap radio_common
  time_management softmodem_common nrppa
  -Wl,--end-group dl)

target_link_libraries(nr-softmodem PRIVATE pthread m CONFIG_LIB rt)
target_link_libraries(nr-softmodem PRIVATE ${T_LIB})
target_link_libraries(nr-softmodem PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)
target_link_libraries(nr-softmodem PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)
add_boolean_option(OAI_WLS OFF "Activate OAI's WLS driver" OFF)
add_boolean_option(OAI_AERIAL OFF "Activate OAI's AERIAL driver" OFF)
if (OAI_WLS)
    target_link_libraries(nr-softmodem PUBLIC wls_integration_lib)
elseif (OAI_AERIAL)
    target_compile_definitions(nr-softmodem PUBLIC ENABLE_AERIAL)
    target_link_libraries(nr-softmodem PUBLIC aerial_lib)
else()
    target_link_libraries(nr-softmodem PUBLIC nfapi_socket_lib)
endif()
if(E2_AGENT)
  target_link_libraries(nr-softmodem PUBLIC e2_agent e2_agent_arg e2_ran_func_du_cucp_cuup)
  target_compile_definitions(nr-softmodem PRIVATE ${E2AP_VERSION} ${KPM_VERSION} E2_AGENT)
endif()

if (E3_AGENT)
    target_link_libraries(nr-softmodem PRIVATE e3ap)
    target_compile_definitions(nr-softmodem PRIVATE E3_AGENT)
endif()


# force the generation of ASN.1 so that we don't need to wait during the build
target_link_libraries(nr-softmodem PRIVATE
  asn1_lte_rrc asn1_nr_rrc asn1_s1ap asn1_ngap asn1_m2ap asn1_m3ap asn1_x2ap asn1_xnap asn1_f1ap asn1_lpp asn1_nrppa)

add_executable(nr-cuup
  executables/nr-cuup.c
  ${NR_RRC_DIR}/rrc_gNB_UE_context.c
  ${OPENAIR2_DIR}/E1AP/e1ap_setup.c
  ${NR_PDCP_SRC_GNB}
  ${NR_SDAP_SRC}
  )

target_link_libraries(nr-cuup PRIVATE
                      CONFIG_LIB ITTI SCTP_CLIENT
                      GTPV1U e1ap f1ap
                      time_management softmodem_common
                      alg
                      dl pthread ${T_LIB})
target_link_libraries(nr-cuup PRIVATE asn1_lte_rrc_hdrs asn1_nr_rrc_hdrs)
target_compile_definitions(nr-cuup PRIVATE PDCP_CUCP_CUUP)
if(E2_AGENT)
  target_link_libraries(nr-cuup PRIVATE e2_agent e2_agent_arg e2_ran_func_cuup)
  target_compile_definitions(nr-cuup PRIVATE ${E2AP_VERSION} ${KPM_VERSION} E2_AGENT)
endif()

# nr-uesoftmodem is  UE implementation
#######################################

add_executable(nr-uesoftmodem
  ${rrc_h}
  ${s1ap_h}
  ${OPENAIR_DIR}/executables/nr-uesoftmodem.c
  ${OPENAIR_DIR}/executables/position_interface.c
  ${OPENAIR_DIR}/executables/nr-ue.c
  ${OPENAIR_DIR}/executables/nr-ue-ru.c
  ${OPENAIR1_DIR}/PHY/TOOLS/phy_scope_interface.c
  ${NFAPI_USER_DIR}/nfapi.c
  )

target_link_libraries(nr-uesoftmodem PRIVATE
  -Wl,--start-group
  nr_rrc SECURITY UTIL SCHED_NR_UE_LIB
  PHY_NR_COMMON PHY_NR_UE NR_L2_UE MAC_NR_COMMON nfapi_nr_lib
  ITTI SIMU radio_common
  time_management softmodem_common
  -Wl,--end-group dl)

target_link_libraries(nr-uesoftmodem PRIVATE pthread m CONFIG_LIB rt nr_ue_phy_meas)
target_link_libraries(nr-uesoftmodem PRIVATE ${T_LIB})
target_link_libraries(nr-uesoftmodem PRIVATE nr_nas lib_uicc usim_lib SECURITY)
target_link_libraries(nr-uesoftmodem PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

# force the generation of ASN.1 so that we don't need to wait during the build
target_link_libraries(nr-uesoftmodem PRIVATE
                      asn1_lte_rrc asn1_nr_rrc asn1_s1ap asn1_ngap asn1_m2ap asn1_m3ap asn1_x2ap asn1_f1ap asn1_lpp)

###################################"
# Addexecutables for tests
####################################


# Unitary tests for each piece of L1: example, mbmssim is MBMS L1 simulator
#####################################

#special case for dlim TM4, which uses its own version of phy_scope code
#add_executable(dlsim_tm4
#  ${OPENAIR1_DIR}/SIMULATION/LTE_PHY/dlsim_tm4.c
#  ${OPENAIR1_DIR}/PHY/TOOLS/lte_phy_scope_tm4.c
#  )
#target_link_libraries (dlsim_tm4
#  -Wl,--start-group SIMU UTIL SCHED_LIB SCHED_RU_LIB PHY ITTI -Wl,--end-group
#  pthread m rt CONFIG_LIB ${T_LIB}
#  )

add_executable(polartest
  ${OPENAIR1_DIR}/PHY/CODING/TESTBENCH/polartest.c
  )
target_link_libraries(polartest PRIVATE
  -Wl,--start-group UTIL SIMU PHY_COMMON PHY_NR PHY_NR_COMMON PHY_NR_UE CONFIG_LIB -Wl,--end-group
  m pthread dl ${T_LIB}
  )

add_executable(smallblocktest
  ${OPENAIR1_DIR}/PHY/CODING/TESTBENCH/smallblocktest.c
)

target_link_libraries(smallblocktest PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR PHY_COMMON PHY_NR_COMMON -Wl,--end-group
  m pthread dl ${T_LIB}
)


add_executable(ldpctest
  ${OPENAIR1_DIR}/PHY/CODING/TESTBENCH/ldpctest.c
  ${OPENAIR1_DIR}/PHY/CODING/nrLDPC_load.c
)
target_link_libraries(ldpctest PRIVATE
  -Wl,--start-group UTIL SIMU PHY_COMMON PHY_NR_COMMON -Wl,--end-group
  m pthread dl ${T_LIB} ${CUDA_LIBRARIES}
  # link 'check_crc' to make it resolved in the LDPC coding libraries
  # 'check_crc' is not used in ldpctest so it is not linked in the executable by default
  # --whole-archive links 'check_crc' in the executable even though it is note used, see 'man ld'
  -Wl,--whole-archive crc_byte -Wl,--no-whole-archive
)
if (ENABLE_LDPC_CUDA)
  # ldpctest.c uses gpuHostAlloc(), so include dir/link libr to make available
  # TODO don't write it directly into ldpctest, but separate compilation unit?
  if (LDPC_GPU_BACKEND STREQUAL "HIP")
    target_link_libraries(ldpctest PRIVATE hip::host)
  else()
    find_package(CUDA REQUIRED)
    target_link_libraries(ldpctest PRIVATE ${CUDA_LIBRARIES})
    target_include_directories(ldpctest PRIVATE ${CUDA_INCLUDE_DIRS})
  endif()
endif()

add_library(physim_common OBJECT ${OPENAIR1_DIR}/SIMULATION/NR_PHY/nr_unitary_common.c)
target_link_libraries(physim_common PRIVATE UTIL)

add_executable(nr_dlschsim ${OPENAIR1_DIR}/SIMULATION/NR_PHY/dlschsim.c)
target_link_libraries(nr_dlschsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB CONFIG_LIB MAC_NR_COMMON -Wl,--end-group
  m pthread ${T_LIB} ITTI dl physim_common
  )
target_link_libraries(nr_dlschsim PRIVATE asn1_nr_rrc_hdrs)

add_executable(nr_pbchsim ${OPENAIR1_DIR}/SIMULATION/NR_PHY/pbchsim.c)
target_link_libraries(nr_pbchsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB CONFIG_LIB MAC_NR_COMMON -Wl,--end-group
  m pthread ${T_LIB} ITTI dl physim_common
)
target_link_libraries(nr_pbchsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_psbchsim
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/psbchsim.c
  )
target_link_libraries(nr_psbchsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB SCHED_NR_UE_LIB MAC_UE_NR MAC_NR_COMMON CONFIG_LIB -Wl,--end-group
  m pthread ${T_LIB} ITTI dl physim_common softmodem_common
  )
target_link_libraries(nr_psbchsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_pucchsim ${OPENAIR1_DIR}/SIMULATION/NR_PHY/pucchsim.c)
target_link_libraries(nr_pucchsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB CONFIG_LIB MAC_NR_COMMON -Wl,--end-group
  m pthread ${T_LIB} ITTI dl physim_common
)
target_link_libraries(nr_pucchsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_dlsim
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/dlsim.c
  )
target_link_libraries(nr_dlsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB SCHED_NR_UE_LIB MAC_UE_NR MAC_NR_COMMON CONFIG_LIB L2_NR time_management_core -Wl,--end-group
  m pthread ${T_LIB} ITTI dl nr_ue_phy_meas physim_common NR_L2_UE
  softmodem_common
  )
target_link_libraries(nr_dlsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_prachsim ${OPENAIR1_DIR}/SIMULATION/NR_PHY/prachsim.c)
target_link_libraries(nr_prachsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR NR_PHY_RU PHY_NR_UE MAC_NR_COMMON SCHED_NR_LIB CONFIG_LIB -Wl,--end-group
  m pthread ${T_LIB} ITTI dl physim_common)
target_link_libraries(nr_prachsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_ulschsim
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/ulschsim.c
  )
target_link_libraries(nr_ulschsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB CONFIG_LIB MAC_NR_COMMON -Wl,--end-group
  m pthread ${T_LIB} ITTI dl physim_common
)
target_link_libraries(nr_ulschsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_ulsim
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/ulsim.c
  )

target_link_libraries(nr_ulsim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB SCHED_NR_UE_LIB MAC_UE_NR MAC_NR_COMMON CONFIG_LIB L2_NR time_management_core -Wl,--end-group
  m pthread ${T_LIB} ITTI dl nr_ue_phy_meas physim_common softmodem_common NR_L2_UE
  )
target_link_libraries(nr_ulsim PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_executable(nr_srssim
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/srssim.c
  )

target_link_libraries(nr_srssim PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB SCHED_NR_UE_LIB MAC_UE_NR MAC_NR_COMMON -Wl,--end-group
  m pthread ITTI dl nr_ue_phy_meas physim_common softmodem_common
  )

add_executable(nr_prssim
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/prssim.c
  )

target_link_libraries(nr_prssim PRIVATE
  SCHED_NR_LIB SCHED_NR_UE_LIB PHY_NR PHY_NR_UE SCHED_NR_UE_LIB SCHED_NR_LIB PHY_NR SIMU ds ITTI m physim_common softmodem_common
  )

add_executable(nr_ulsim_mu_mimo
  ${OPENAIR1_DIR}/SIMULATION/NR_PHY/ulsim_mu_mimo.c
  )

target_link_libraries(nr_ulsim_mu_mimo PRIVATE
  -Wl,--start-group UTIL SIMU PHY_NR_COMMON PHY_NR PHY_NR_UE SCHED_NR_LIB SCHED_NR_UE_LIB MAC_UE_NR MAC_NR_COMMON CONFIG_LIB L2_NR -Wl,--end-group
  m pthread ${T_LIB} ITTI dl nr_ue_phy_meas physim_common softmodem_common NR_L2_UE time_management_core
  )
target_link_libraries(nr_ulsim_mu_mimo PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

if(ENABLE_CHANNEL_SIM_CUDA)
  if (TARGET oai_cuda_lib)
      target_link_libraries(nr_dlsim PRIVATE oai_cuda_lib)
      target_link_libraries(nr_ulsim PRIVATE oai_cuda_lib)

      target_compile_definitions(nr_dlsim PRIVATE CHANNEL_SIM_CUDA)
      target_compile_definitions(nr_ulsim PRIVATE CHANNEL_SIM_CUDA)
  endif()
endif()

# these simulators do not compile:
# dlsim_tm7 pbchsim scansim mbmssim pdcchsim pucchsim prachsim syncsim
foreach(myExe dlsim ulsim)
  add_executable(${myExe}
    ${OPENAIR1_DIR}/SIMULATION/LTE_PHY/${myExe}.c
    ${OPENAIR_DIR}/common/utils/lte/ue_power.c
    ${OPENAIR_DIR}/common/utils/lte/prach_utils.c
    ${OPENAIR1_DIR}/PHY/INIT/lte_param_init.c
    )
  target_link_libraries (${myExe} PRIVATE
    -Wl,--start-group SIMU UTIL SCHED_LIB SCHED_RU_LIB SCHED_UE_LIB PHY_COMMON PHY_NR_COMMON PHY PHY_UE PHY_RU ITTI -Wl,--end-group
    pthread m rt CONFIG_LIB ${T_LIB} dl
    )
  target_link_libraries(${myExe} PRIVATE asn1_lte_rrc_hdrs)
  add_dependencies(${myExe} coding)

endforeach(myExe)

#unitary tests for Core NEtwork pieces
#################################
#foreach(myExe
#    s1ap
#    secu_knas_encrypt_eia1
#    secu_kenb
#    aes128_ctr
#    secu_knas_encrypt_eea2
#    secu_knas secu_knas_encrypt_eea1
#    kdf
#    aes128_cmac_encrypt
#    secu_knas_encrypt_eia2)
#  add_executable(test_${myExe}
#    ${OPENAIR3_DIR}/TEST/test_${myExe}.c
#    )
#  target_link_libraries (test_${myExe}
#    -Wl,--start-group SECURITY UTIL -Wl,--end-group m rt CONFIG_LIB
#    )
#endforeach(myExe)

#ensure that the T header files are generated before targets depending on them
if (${T_TRACER})
  foreach(i
        #all "add_executable" definitions (except tests, updatefw)
        lte-softmodem lte-uesoftmodem nr-softmodem
        nr-uesoftmodem dlsim dlsim_tm4 dlsim_tm7
        ulsim pbchsim scansim mbmssim pdcchsim pucchsim prachsim
        syncsim nr_ulsim nr_dlsim nr_dlschsim nr_pbchsim nr_pucchsim
        nr_ulschsim ldpctest polartest smallblocktest nr_srssim nr_prssim nr_ulsim_mu_mimo
        #all "add_library" definitions
        ITTI lte_rrc nr_rrc s1ap x2ap m2ap m3ap f1ap
        params_libconfig
        oai_eth_transpro UTIL
        SECURITY SCHED_LIB SCHED_NR_LIB SCHED_RU_LIB SCHED_UE_LIB SCHED_NR_UE_LIB
        nfapi_lte_lib nfapi_nr_lib nfapi_pnf_lib nfapi_lte_vnf_lib nfapi_nr_vnf_lib nfapi_user_lib nfapi_lte_user_lib nfapi_nr_user_lib
        MISC_NFAPI_LTE_LIB PHY_TOOLS
        PHY_COMMON PHY PHY_UE PHY_NR PHY_NR_COMMON PHY_NR_UE PHY_RU NR_PHY_RU
        L2 L2_LTE L2_NR L2_LTE_NR L2_UE NR_L2_UE MAC_NR_COMMON MAC_UE_NR ngap
        GTPV1U SCTP_CLIENT MME_APP LIB_NAS_UE SIMU
        dfts config_internals nr_common crc_byte)
    if (TARGET ${i})
      add_dependencies(${i} generate_T)
    endif()
  endforeach(i)
endif (${T_TRACER})

set(ENABLE_TESTS OFF CACHE STRING "Activate build of tests")
set_property(CACHE ENABLE_TESTS PROPERTY TYPE BOOL)
set(ENABLE_PHYSIM_TESTS OFF CACHE STRING "Activate build of physim tests")
set_property(CACHE ENABLE_PHYSIM_TESTS PROPERTY TYPE BOOL)

if(ENABLE_TESTS OR ENABLE_PHYSIM_TESTS)
  # meta-target: each test is supposed to add_dependencies(tests ${NEWTEST})
  # then, it is possible to build and execute all tests using "ninja tests && ctest"
  add_custom_target(tests)
  # activates globally adding tests such that they can be run using ctest
  enable_testing()
endif()

if(ENABLE_TESTS)
  find_package(GTest)
  if (NOT GTest_FOUND)
    message(STATUS "GTest package not found, will download googletest automatically. To prevent that install google test on your system (libgtest-dev)")
    CPMAddPackage(
      NAME googletest
      GITHUB_REPOSITORY google/googletest
      GIT_TAG release-1.12.1
      VERSION 1.12.1
      OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt" "BUILD_GMOCK OFF"
    )
  endif()

  find_package(benchmark)
  if (NOT benchmark_FOUND)
    message(STATUS "benchmark package not found, will download benchmark automatically. To prevent that install google benchmark on your system (libbenchmark-dev)")
    CPMAddPackage(
      NAME benchmark
      GITHUB_REPOSITORY google/benchmark
      VERSION 1.9.0
      OPTIONS "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_WERROR OFF"
    )
  endif()
endif()

add_subdirectory(common)
add_subdirectory(executables)
add_subdirectory(doc)
add_subdirectory(nfapi)
add_subdirectory(openair1)
add_subdirectory(openair2)
add_subdirectory(openair3)
add_subdirectory(radio)
add_subdirectory(tests)

add_boolean_option(OAI_RU_FRONTHAUL OFF "Whether to configure OAI fronthaul library" OFF)
if(OAI_RU_FRONTHAUL)
  add_subdirectory(fronthaul)
endif()

if(TARGET oai_cuda_lib)
    message(STATUS "CUDA library 'oai_cuda_lib' found, linking to targets...")
    target_link_libraries(nr_dlsim PRIVATE oai_cuda_lib)
    target_link_libraries(nr_ulsim PRIVATE oai_cuda_lib)
endif()

if(PACKAGING_LTE OR PACKAGING_NR OR PACKAGING_COMMON OR PACKAGING_USRP OR PACKAGING_PHYSIM)
  include("tools/packages/packages.cmake")
endif()
