# Mac-side unit tests for the pure components (cora, hidparse).
#   cmake -S firmware/test/host -B firmware/test/host/build && cmake --build firmware/test/host/build && firmware/test/host/build/deck32_tests
cmake_minimum_required(VERSION 3.22)
project(deck32_host_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# doctest 2.4.x still declares an old cmake_minimum_required
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/../../components)
add_executable(deck32_tests
  main.cpp
  test_frame.cpp
  test_protocol.cpp
  test_session.cpp
  test_hidparse.cpp
  ${COMP}/cora/src/frame.cpp
  ${COMP}/cora/src/protocol.cpp
  ${COMP}/cora/src/session.cpp
  ${COMP}/hidparse/src/hid_descriptor.cpp)
target_include_directories(deck32_tests PRIVATE ${COMP}/cora/include ${COMP}/hidparse/include)
target_compile_options(deck32_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(deck32_tests PRIVATE doctest::doctest)
