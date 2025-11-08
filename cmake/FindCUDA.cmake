# FindCUDA module for CMake
# This is a helper module for finding CUDA Toolkit

if(NOT CUDA_FOUND)
    find_package(CUDAToolkit REQUIRED)
    
    set(CUDA_FOUND TRUE)
    set(CUDA_INCLUDE_DIRS ${CUDAToolkit_INCLUDE_DIRS})
    set(CUDA_LIBRARIES CUDA::cudart CUDA::cuda_driver)
    
    message(STATUS "CUDA Toolkit version: ${CUDAToolkit_VERSION}")
    message(STATUS "CUDA Include dirs: ${CUDA_INCLUDE_DIRS}")
endif()
