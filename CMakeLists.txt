cmake_minimum_required(VERSION 3.16)
project(limit_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOB_BUILD_TESTS "Build unit tests" ON)

add_library(lob_core src/order_book.cpp)
target_include_directories(lob_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(lob_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lob_engine src/main.cpp)
target_link_libraries(lob_engine PRIVATE lob_core)
target_compile_options(lob_engine PRIVATE -Wall -Wextra -Wpedantic)

if(LOB_BUILD_TESTS)
  # Prefer vendored single-header Catch2 (third_party/catch.hpp); else FetchContent.
  set(CATCH_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch.hpp")
  add_executable(lob_tests tests/test_order_book.cpp)
  target_link_libraries(lob_tests PRIVATE lob_core)
  target_compile_options(lob_tests PRIVATE -Wall -Wextra -Wpedantic)

  if(EXISTS "${CATCH_HEADER}")
    target_include_directories(lob_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  else()
    include(FetchContent)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v2.13.10
    )
    FetchContent_MakeAvailable(Catch2)
    target_link_libraries(lob_tests PRIVATE Catch2::Catch2)
    target_include_directories(lob_tests PRIVATE ${catch2_SOURCE_DIR}/single_include)
  endif()

  enable_testing()
  add_test(NAME lob_tests COMMAND lob_tests)
endif()
