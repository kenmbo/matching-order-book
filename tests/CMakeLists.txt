add_subdirectory(fixtures)

add_executable(lob_smoke_test smoke_test.cpp)
target_link_libraries(lob_smoke_test PRIVATE lob::engine lob::test_fixtures)

add_executable(lob_domain_contracts_test domain_contracts_test.cpp)
target_link_libraries(lob_domain_contracts_test PRIVATE lob::engine)

add_executable(lob_order_book_storage_test order_book_storage_test.cpp)
target_link_libraries(lob_order_book_storage_test PRIVATE lob::engine)

add_executable(lob_lossless_outbox_test lossless_outbox_test.cpp)
target_link_libraries(lob_lossless_outbox_test PRIVATE lob::engine)

add_executable(lob_matching_engine_test matching_engine_test.cpp)
target_link_libraries(lob_matching_engine_test PRIVATE lob::engine)

add_executable(lob_instrument_lifecycle_test instrument_lifecycle_test.cpp)
target_link_libraries(lob_instrument_lifecycle_test PRIVATE lob::engine)

add_executable(
  lob_phase1_reference_model_test
  phase1_reference_model.cpp
  phase1_reference_model_test.cpp
)
target_link_libraries(lob_phase1_reference_model_test PRIVATE lob::engine)
target_compile_definitions(
  lob_phase1_reference_model_test
  PRIVATE
    LOB_PHASE1_TRACE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/phase1_replay.trace"
)

add_executable(lob_phase1_hardening_test phase1_hardening_test.cpp)
target_link_libraries(lob_phase1_hardening_test PRIVATE lob::engine)

add_executable(lob_fixed_object_pool_test fixed_object_pool_test.cpp)
target_link_libraries(lob_fixed_object_pool_test PRIVATE lob::engine)

add_executable(
  lob_fixed_object_pool_allocation_test
  fixed_object_pool_allocation_test.cpp
)
target_link_libraries(
  lob_fixed_object_pool_allocation_test PRIVATE lob::engine
)

add_executable(
  lob_benchmark_support_test
  benchmark_support_test.cpp
  ../benchmarks/benchmark_support.cpp
)
target_include_directories(
  lob_benchmark_support_test PRIVATE ${CMAKE_SOURCE_DIR}/benchmarks
)
target_link_libraries(lob_benchmark_support_test PRIVATE lob_build_options)

add_test(NAME lob.smoke COMMAND lob_smoke_test)
add_test(NAME lob.domain_contracts COMMAND lob_domain_contracts_test)
add_test(NAME lob.order_book_storage COMMAND lob_order_book_storage_test)
add_test(NAME lob.lossless_outbox COMMAND lob_lossless_outbox_test)
add_test(NAME lob.matching_engine COMMAND lob_matching_engine_test)
add_test(NAME lob.instrument_lifecycle COMMAND lob_instrument_lifecycle_test)
add_test(NAME lob.phase1_reference_model COMMAND lob_phase1_reference_model_test)
add_test(NAME lob.phase1_hardening COMMAND lob_phase1_hardening_test)
add_test(NAME lob.fixed_object_pool COMMAND lob_fixed_object_pool_test)
add_test(
  NAME lob.fixed_object_pool_allocation
  COMMAND lob_fixed_object_pool_allocation_test
)
add_test(NAME lob.benchmark_support COMMAND lob_benchmark_support_test)
add_test(
  NAME lob.benchmark_smoke
  COMMAND lob_phase1_benchmark
    --mode smoke
    --workload mixed
    --seeds 24301
    --samples 1000
    --warmup 100
    --repetitions 1
)
set_tests_properties(
  lob.benchmark_smoke
  PROPERTIES PASS_REGULAR_EXPRESSION "\"schema\": \"lob.phase1.performance.v1\""
)
add_test(
  NAME lob.fixed_object_pool_benchmark_smoke
  COMMAND lob_fixed_object_pool_benchmark
    --mode smoke
    --capacity 256
    --repetitions 1
    --full-cycles 2
    --churn-operations 1000
    --warmup-cycles 1
)
set_tests_properties(
  lob.fixed_object_pool_benchmark_smoke
  PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"schema\": \"lob.fixed_object_pool.performance.v1\""
)
add_test(
  NAME lob.fixed_object_pool_benchmark_allocation_smoke
  COMMAND lob_fixed_object_pool_allocation_audit
    --mode smoke
    --capacity 256
    --repetitions 1
    --full-cycles 2
    --churn-operations 1000
    --warmup-cycles 1
    --allocation-output
      ${CMAKE_CURRENT_BINARY_DIR}/fixed_object_pool_smoke.alloc
)
set_tests_properties(
  lob.fixed_object_pool_benchmark_allocation_smoke
  PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"accepted\":true,\"timed_pool_allocations\":0"
)
add_test(
  NAME lob.benchmark_invalid_config
  COMMAND lob_phase1_benchmark --mode acceptance --workload mixed
)
set_tests_properties(lob.benchmark_invalid_config PROPERTIES WILL_FAIL TRUE)
