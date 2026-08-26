cmake_minimum_required(VERSION 3.25)

project(
  limit_order_book
  VERSION 0.1.0
  DESCRIPTION "Deterministic limit order book"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LOB_ENABLE_ASAN "Enable AddressSanitizer instrumentation" OFF)
option(LOB_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer instrumentation" OFF)

if(LOB_ENABLE_ASAN AND LOB_ENABLE_UBSAN)
  message(FATAL_ERROR "ASan and UBSan configurations must remain separate")
endif()

add_library(lob_build_options INTERFACE)
target_compile_options(
  lob_build_options
  INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    $<$<CXX_COMPILER_ID:GNU>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU>:-Werror>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-march=native>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-ffast-math>
)

if(LOB_ENABLE_ASAN)
  target_compile_options(
    lob_build_options
    INTERFACE -fsanitize=address -fno-omit-frame-pointer -g3
  )
  target_link_options(lob_build_options INTERFACE -fsanitize=address)
endif()

if(LOB_ENABLE_UBSAN)
  target_compile_options(
    lob_build_options
    INTERFACE -fsanitize=undefined -fno-omit-frame-pointer -g3
  )
  target_link_options(lob_build_options INTERFACE -fsanitize=undefined)
endif()

add_library(
  lob_engine STATIC
  src/matching_engine.cpp
  src/order_book_storage.cpp
  src/project.cpp
)
add_library(lob::engine ALIAS lob_engine)
target_include_directories(
  lob_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(lob_engine PUBLIC lob_build_options)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

add_subdirectory(benchmarks)
