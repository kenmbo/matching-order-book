execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE LOB_BENCHMARK_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git status --porcelain
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE LOB_BENCHMARK_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(LOB_BENCHMARK_GIT_STATUS STREQUAL "")
  set(LOB_BENCHMARK_GIT_DIRTY false)
else()
  set(LOB_BENCHMARK_GIT_DIRTY true)
endif()

add_library(lob_benchmark_support STATIC benchmark_support.cpp)
target_include_directories(lob_benchmark_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_benchmark_support PUBLIC lob_build_options)

add_executable(lob_phase1_benchmark phase1_benchmark.cpp)
target_link_libraries(
  lob_phase1_benchmark PRIVATE lob::engine lob_benchmark_support
)
target_compile_definitions(
  lob_phase1_benchmark
  PRIVATE
    LOB_BENCHMARK_GIT_COMMIT="${LOB_BENCHMARK_GIT_COMMIT}"
    LOB_BENCHMARK_GIT_DIRTY=${LOB_BENCHMARK_GIT_DIRTY}
)

add_executable(lob_phase1_allocation_audit phase1_benchmark.cpp)
target_link_libraries(
  lob_phase1_allocation_audit PRIVATE lob::engine lob_benchmark_support
)
target_compile_definitions(
  lob_phase1_allocation_audit
  PRIVATE
    LOB_ENABLE_ALLOCATION_AUDIT=1
    LOB_BENCHMARK_GIT_COMMIT="${LOB_BENCHMARK_GIT_COMMIT}"
    LOB_BENCHMARK_GIT_DIRTY=${LOB_BENCHMARK_GIT_DIRTY}
)

add_executable(lob_fixed_object_pool_benchmark fixed_object_pool_benchmark.cpp)
target_include_directories(
  lob_fixed_object_pool_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(
  lob_fixed_object_pool_benchmark PRIVATE lob_benchmark_support lob_build_options
)
target_compile_definitions(
  lob_fixed_object_pool_benchmark
  PRIVATE
    LOB_BENCHMARK_GIT_COMMIT="${LOB_BENCHMARK_GIT_COMMIT}"
    LOB_BENCHMARK_GIT_DIRTY=${LOB_BENCHMARK_GIT_DIRTY}
    LOB_BENCHMARK_BUILD_TYPE="$<CONFIG>"
)

add_executable(
  lob_fixed_object_pool_allocation_audit
  fixed_object_pool_benchmark.cpp
)
target_include_directories(
  lob_fixed_object_pool_allocation_audit PRIVATE ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(
  lob_fixed_object_pool_allocation_audit
  PRIVATE lob_benchmark_support lob_build_options
)
target_compile_definitions(
  lob_fixed_object_pool_allocation_audit
  PRIVATE
    LOB_ENABLE_POOL_ALLOCATION_AUDIT=1
    LOB_BENCHMARK_GIT_COMMIT="${LOB_BENCHMARK_GIT_COMMIT}"
    LOB_BENCHMARK_GIT_DIRTY=${LOB_BENCHMARK_GIT_DIRTY}
    LOB_BENCHMARK_BUILD_TYPE="$<CONFIG>"
)

add_custom_target(
  benchmarks
  DEPENDS
    lob_phase1_benchmark
    lob_phase1_allocation_audit
    lob_fixed_object_pool_benchmark
    lob_fixed_object_pool_allocation_audit
)
