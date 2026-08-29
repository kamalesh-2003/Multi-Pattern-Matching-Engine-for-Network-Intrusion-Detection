cmake_minimum_required(VERSION 3.20)
project(mpm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MPM_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(MPM_FUZZER "Build libFuzzer targets (Clang only)" OFF)
option(MPM_WERROR "Treat warnings as errors" ON)

# ---- Common warning / flag interface ---------------------------------------
add_library(mpm_flags INTERFACE)
target_compile_options(mpm_flags INTERFACE -Wall -Wextra -Wpedantic)
if(MPM_WERROR)
  target_compile_options(mpm_flags INTERFACE -Werror)
endif()
if(MPM_SANITIZE)
  target_compile_options(mpm_flags INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(mpm_flags INTERFACE -fsanitize=address,undefined)
endif()

# ---- Core library ----------------------------------------------------------
# NOTE: the whole binary is NEVER compiled with -march=native. Only the two SIMD
# translation units below get architecture flags, and they are reached solely
# through runtime CPUID dispatch (see src/simd/prefilter_scalar.cpp).
add_library(mpm STATIC
  src/aho_corasick.cpp
  src/packet.cpp
  src/simd/prefilter_scalar.cpp
  src/simd/prefilter_sse42.cpp
  src/simd/prefilter_avx2.cpp
)
target_include_directories(mpm PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mpm PUBLIC mpm_flags)

# Per-file architecture flags: these must stay narrowly scoped.
set_source_files_properties(src/simd/prefilter_sse42.cpp PROPERTIES COMPILE_OPTIONS "-msse4.2")
set_source_files_properties(src/simd/prefilter_avx2.cpp  PROPERTIES COMPILE_OPTIONS "-mavx2")

find_package(Threads REQUIRED)

# ---- Benchmark -------------------------------------------------------------
add_executable(bench bench/bench.cpp)
target_link_libraries(bench PRIVATE mpm mpm_flags Threads::Threads)

# ---- Tests -----------------------------------------------------------------
enable_testing()

set(MPM_TESTS test_basic test_differential test_streaming test_prefilter test_spsc test_packet)
foreach(t ${MPM_TESTS})
  add_executable(${t} tests/${t}.cpp)
  target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(${t} PRIVATE mpm mpm_flags Threads::Threads)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# The heavy randomized oracles get a generous timeout.
set_tests_properties(test_differential PROPERTIES TIMEOUT 300)
set_tests_properties(test_streaming    PROPERTIES TIMEOUT 300)
set_tests_properties(test_prefilter    PROPERTIES TIMEOUT 300)
set_tests_properties(test_spsc         PROPERTIES TIMEOUT 300)
set_tests_properties(test_packet       PROPERTIES TIMEOUT 300)

# ---- Fuzzer (Clang only) ---------------------------------------------------
if(MPM_FUZZER)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "MPM_FUZZER requires Clang (-fsanitize=fuzzer)")
  endif()
  add_executable(fuzz_packet fuzz/fuzz_packet.cpp src/packet.cpp)
  target_include_directories(fuzz_packet PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_compile_options(fuzz_packet PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
  target_link_options(fuzz_packet PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
