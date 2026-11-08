# Mac-side unit tests for the pure components (cora, hidparse, deck_ui).
#   build/render_screens <dir> writes every deck_ui screen as PPM images, to look at layouts.
#   cmake -S firmware/test/host -B firmware/test/host/build && cmake --build firmware/test/host/build && firmware/test/host/build/deck32_tests
cmake_minimum_required(VERSION 3.22)
project(deck32_host_tests C CXX)
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
  test_deck_ui.cpp
  test_web_auth.cpp
  ${COMP}/cora/src/frame.cpp
  ${COMP}/cora/src/protocol.cpp
  ${COMP}/cora/src/session.cpp
  ${COMP}/hidparse/src/hid_descriptor.cpp)
target_include_directories(deck32_tests PRIVATE ${COMP}/cora/include ${COMP}/hidparse/include ${COMP}/web/include)
target_compile_options(deck32_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(deck32_tests PRIVATE doctest::doctest deck_ui_core)

# deck_ui's pure core (no ESP-IDF): canvas, fonts, models, screens, QR codes.
add_library(deck_ui_core STATIC
  ${COMP}/deck_ui/src/canvas.cpp
  ${COMP}/deck_ui/src/font_data.cpp
  ${COMP}/deck_ui/src/model.cpp
  ${COMP}/deck_ui/src/ui.cpp
  ${COMP}/deck_ui/third_party/qrcodegen.c)
target_include_directories(deck_ui_core PUBLIC ${COMP}/deck_ui/include ${COMP}/deck_ui/third_party)
target_compile_options(deck_ui_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Werror>)

add_executable(render_screens render_screens.cpp)
target_link_libraries(render_screens PRIVATE deck_ui_core)
