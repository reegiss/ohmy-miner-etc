cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
project(ohmy-miner-etc VERSION 1.0.0 LANGUAGES CXX CUDA)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CUDA Standard
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# CUDA flags
# Suppress NVCC warning 20044 (extern __constant__ treated as static) globally for cleaner builds.
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -use_fast_math -diag-suppress=20044 -Xnvlink=-w")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CUDA_FLAGS_DEBUG "-g -G -O0")

# Find packages
find_package(CUDA REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${CUDA_INCLUDE_DIRS}
)

# Add subdirectories
add_subdirectory(src)

# Enable testing
enable_testing()
add_subdirectory(tests)

# Installation
install(TARGETS ohmy-miner-etc
    RUNTIME DESTINATION bin
)

# Documentation
option(BUILD_DOCS "Build documentation" OFF)
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()
