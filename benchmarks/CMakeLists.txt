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

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(
  lob_benchmark_support STATIC
  benchmark_support.cpp
  benchmark_provenance.cpp
)
target_include_directories(lob_benchmark_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lob_benchmark_support PUBLIC lob_build_options)

add_executable(lob_phase2_pool_benchmark phase1_benchmark.cpp)
target_link_libraries(
  lob_phase2_pool_benchmark PRIVATE lob::engine lob_benchmark_support
)

add_executable(lob_phase2_pool_allocation_audit phase1_benchmark.cpp)
target_link_libraries(
  lob_phase2_pool_allocation_audit PRIVATE lob::engine lob_benchmark_support
)
target_compile_definitions(
  lob_phase2_pool_allocation_audit
  PRIVATE
    LOB_ENABLE_ALLOCATION_AUDIT=1
)

set(
  LOB_PHASE2_BENCHMARK_PROVENANCE
  ${CMAKE_CURRENT_BINARY_DIR}/phase2_benchmark.provenance
)
add_custom_target(
  lob_phase2_benchmark_provenance ALL
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/capture_benchmark_provenance.py
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --compile-commands ${CMAKE_BINARY_DIR}/compile_commands.json
    --latency-target lob_phase2_pool_benchmark
    --latency-executable $<TARGET_FILE:lob_phase2_pool_benchmark>
    --allocation-target lob_phase2_pool_allocation_audit
    --allocation-executable $<TARGET_FILE:lob_phase2_pool_allocation_audit>
    --build-type $<CONFIG>
    --compiler-id ${CMAKE_CXX_COMPILER_ID}
    --compiler-version ${CMAKE_CXX_COMPILER_VERSION}
    --output ${LOB_PHASE2_BENCHMARK_PROVENANCE}
  BYPRODUCTS ${LOB_PHASE2_BENCHMARK_PROVENANCE}
  DEPENDS lob_phase2_pool_benchmark lob_phase2_pool_allocation_audit
  VERBATIM
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
    lob_phase2_pool_benchmark
    lob_phase2_pool_allocation_audit
    lob_phase2_benchmark_provenance
    lob_fixed_object_pool_benchmark
    lob_fixed_object_pool_allocation_audit
)
