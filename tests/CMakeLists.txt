# Unit tests - compile separately to avoid multiple main() functions
add_executable(test_ethash
    unit/test_ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/keccak.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/logger.cpp
)

add_executable(test_dag
    unit/test_dag.cpp
    ${PROJECT_SOURCE_DIR}/src/dag/dag_generator.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/keccak.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/logger.cpp
    ${PROJECT_SOURCE_DIR}/src/cuda/dag_kernel.cu
)

add_executable(test_stratum
    unit/test_stratum.cpp
    ${PROJECT_SOURCE_DIR}/src/network/stratum_client.cpp
    ${PROJECT_SOURCE_DIR}/src/network/connection.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/logger.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/hex_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/keccak.cpp
)

add_executable(test_hex_utils
    unit/test_hex_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/hex_utils.cpp
)

target_link_libraries(test_ethash PRIVATE Threads::Threads)
target_link_libraries(test_dag PRIVATE Threads::Threads ${CUDA_LIBRARIES})
target_link_libraries(test_stratum PRIVATE Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

target_include_directories(test_ethash PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(test_dag PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(test_stratum PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(test_hex_utils PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Benchmark tests - compile separately
add_executable(bench_ethash
    benchmark/bench_ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/keccak.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/logger.cpp
)

add_executable(bench_cuda
    benchmark/bench_cuda_kernels.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/logger.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ethash.cpp
    ${PROJECT_SOURCE_DIR}/src/core/keccak.cpp
    ${PROJECT_SOURCE_DIR}/src/cuda/device_manager.cu
    ${PROJECT_SOURCE_DIR}/src/cuda/kernels/search_kernel.cu
)

target_link_libraries(bench_cuda PRIVATE ${CUDA_LIBRARIES} Threads::Threads)
target_link_libraries(bench_cuda PRIVATE ${CUDA_LIBRARIES} Threads::Threads)

target_include_directories(bench_ethash PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(bench_cuda PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Add tests to CTest
add_test(NAME TestEthash COMMAND test_ethash)
add_test(NAME TestDAG COMMAND test_dag)
add_test(NAME TestStratum COMMAND test_stratum)
add_test(NAME TestHexUtils COMMAND test_hex_utils)
add_test(NAME BenchEthash COMMAND bench_ethash)
add_test(NAME BenchCUDA COMMAND bench_cuda)
