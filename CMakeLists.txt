cmake_minimum_required(VERSION 3.20)
project(LAML LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# XBF structure reader: parse, validate and describe the container of a binary XAML file.
# No node decoding, no activation.
add_library(laml_xbf STATIC src/xbf/xbf_reader.cpp)
target_include_directories(laml_xbf PUBLIC src)
target_compile_options(laml_xbf PRIVATE -Wall -Wextra)

add_executable(xbfdump tools/xbfdump/xbfdump.cpp)
target_link_libraries(xbfdump PRIVATE laml_xbf)

enable_testing()
add_executable(xbf_tests tests/xbf_tests.cpp)
target_link_libraries(xbf_tests PRIVATE laml_xbf)
add_test(NAME xbf_tests COMMAND xbf_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-ui-xaml)

# Animation math: KeySpline and the XAML easing functions (adapted from MIT-licensed Microsoft.UI.Xaml).
add_library(laml_anim STATIC src/anim/keyspline.cpp src/anim/easing.cpp)
target_include_directories(laml_anim PUBLIC src)
target_compile_options(laml_anim PRIVATE -Wall -Wextra)

add_executable(anim_tests tests/anim_tests.cpp)
target_link_libraries(anim_tests PRIVATE laml_anim)
add_test(NAME anim_tests COMMAND anim_tests)
