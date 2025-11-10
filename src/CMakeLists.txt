# Source files
set(SOURCES
    miner_main.cpp
    core/mining_engine.cpp
    core/ethash.cpp
    core/keccak.cpp
    network/stratum_client.cpp
    network/connection.cpp
    dag/dag_generator.cpp
    utils/logger.cpp
    utils/hex_utils.cpp
    utils/difficulty.cpp
    cuda/core/device_state.cpp
    cuda/kernels/kernel_launcher.cpp
    cuda/pipeline/search_executor.cpp
)

# CUDA source files
set(CUDA_SOURCES
    cuda/device_manager.cu
    cuda/threading/mining_thread.cu
    cuda/stats/device_stats.cpp
    cuda/pipeline/pipeline_manager.cpp
    cuda/utils/memory_profiler.cpp
    cuda/result_callback.cu
    cuda/dag_kernel.cu
    cuda/modules/device_initializer.cu
    cuda/kernels/ethash_kernel.cu
    cuda/kernels/search_kernel.cu
    cuda/kernels/search_kernel_warp.cu
    cuda/kernels/keccak_constants.cu
)

# Create executable
add_executable(ohmy-miner-etc 
    ${SOURCES}
    ${CUDA_SOURCES}
)

# Link libraries
target_link_libraries(ohmy-miner-etc
    PRIVATE
    ${CUDA_LIBRARIES}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# CUDA architectures (adjust based on target GPUs)
set_target_properties(ohmy-miner-etc PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_ARCHITECTURES "60;61;70;75;80;86;89"
)

# Include directories
target_include_directories(ohmy-miner-etc
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/cuda
)
