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

add_test(NAME lob.smoke COMMAND lob_smoke_test)
add_test(NAME lob.domain_contracts COMMAND lob_domain_contracts_test)
add_test(NAME lob.order_book_storage COMMAND lob_order_book_storage_test)
add_test(NAME lob.lossless_outbox COMMAND lob_lossless_outbox_test)
add_test(NAME lob.matching_engine COMMAND lob_matching_engine_test)
add_test(NAME lob.instrument_lifecycle COMMAND lob_instrument_lifecycle_test)
