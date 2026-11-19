cmake_minimum_required(VERSION 3.18)
project(hyperdeduce LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(HYPERDEDUCE_WITH_CUDA "Build the GPU backend" ON)
option(HYPERDEDUCE_BUILD_TESTS "Build unit tests" ON)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(TBB REQUIRED)
find_package(gflags CONFIG REQUIRED)

if(HYPERDEDUCE_WITH_CUDA)
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 17)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 86)
  endif()
  find_package(CUDAToolkit REQUIRED)
  message(STATUS "[hyperdeduce] CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
endif()

message(STATUS "[hyperdeduce] build type: ${CMAKE_BUILD_TYPE}, CUDA: ${HYPERDEDUCE_WITH_CUDA}")

if(HYPERDEDUCE_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
endif()

add_subdirectory(hyperdeduce)
add_subdirectory(examples)