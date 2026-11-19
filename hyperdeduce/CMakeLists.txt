# The hyperdeduce library. Directories mirror the namespace; each module owns one
# body of domain knowledge. Tests are colocated as *_test.cc.

file(GLOB_RECURSE HYPERDEDUCE_SOURCES CONFIGURE_DEPENDS "*.cc")
list(FILTER HYPERDEDUCE_SOURCES EXCLUDE REGEX ".*_test\\.cc$")

set(HYPERDEDUCE_GPU_SOURCES "")
if(HYPERDEDUCE_WITH_CUDA)
  file(GLOB_RECURSE HYPERDEDUCE_GPU_SOURCES CONFIGURE_DEPENDS "backend/gpu/*.cu")
endif()

add_library(hyperdeduce ${HYPERDEDUCE_SOURCES} ${HYPERDEDUCE_GPU_SOURCES})

target_include_directories(hyperdeduce PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/third_party/rapidcsv/src)

target_link_libraries(hyperdeduce PUBLIC TBB::tbb gflags yaml-cpp::yaml-cpp)

if(HYPERDEDUCE_WITH_CUDA)
  target_compile_definitions(hyperdeduce PUBLIC HYPERDEDUCE_WITH_CUDA=1)
  target_link_libraries(hyperdeduce PUBLIC CUDA::cudart)
  set_target_properties(hyperdeduce PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()

if(HYPERDEDUCE_BUILD_TESTS)
  file(GLOB_RECURSE HYPERDEDUCE_TEST_SOURCES CONFIGURE_DEPENDS "*_test.cc")
  foreach(test_src ${HYPERDEDUCE_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE hyperdeduce GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()