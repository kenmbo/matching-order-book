add_subdirectory(fixtures)

add_executable(lob_smoke_test smoke_test.cpp)
target_link_libraries(lob_smoke_test PRIVATE lob::engine lob::test_fixtures)

add_test(NAME lob.smoke COMMAND lob_smoke_test)
