add_library(lob_test_fixtures INTERFACE)
add_library(lob::test_fixtures ALIAS lob_test_fixtures)
target_sources(lob_test_fixtures INTERFACE README.md)
