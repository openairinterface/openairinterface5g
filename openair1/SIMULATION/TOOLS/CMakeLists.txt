# SPDX-License-Identifier: LicenseRef-CSSL-1.0

add_library(noise_device noise_device.c)
target_link_libraries(noise_device PRIVATE log_headers m)
target_include_directories(noise_device PUBLIC ./)
add_library(channel_pipeline channel_pipeline.c)
if (ENABLE_CHANNEL_SIM_CUDA)
  target_sources(channel_pipeline PRIVATE
    channel_pipeline_gpu.cu
  )
endif()
target_link_libraries(channel_pipeline PUBLIC thread-pool noise_device)
if (ENABLE_CHANNEL_SIM_CUDA)
  target_link_libraries(channel_pipeline PUBLIC CUDA::toolkit CUDA::cufft)
endif()

if(ENABLE_CHANNEL_SIM_CUDA)
    add_library(oai_cuda_lib STATIC
        multipath_channel.cu
        phase_noise.cu
        channel_pipeline.cu
    )

    target_link_libraries(oai_cuda_lib PUBLIC CUDA::toolkit)

    target_include_directories(oai_cuda_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(channel_pipeline PUBLIC oai_cuda_lib)
endif()
