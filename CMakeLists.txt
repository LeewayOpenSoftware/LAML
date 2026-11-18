cmake_minimum_required(VERSION 3.20)
project(LAML LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(laml_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# XBF structure reader: parse, validate and describe the container of a binary XAML file.
# No node decoding, no activation.
add_library(laml_xbf STATIC src/xbf/xbf_reader.cpp src/xbf/xbf_nodes.cpp)
target_include_directories(laml_xbf PUBLIC src)
laml_warnings(laml_xbf)

add_executable(xbfdump tools/xbfdump/xbfdump.cpp)
target_link_libraries(xbfdump PRIVATE laml_xbf)

enable_testing()
add_executable(xbf_tests tests/xbf_tests.cpp)
target_link_libraries(xbf_tests PRIVATE laml_xbf)
add_test(NAME xbf_tests COMMAND xbf_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-ui-xaml)

# Node-stream decoder tests (inert decode and validation; same fixtures).
add_executable(xbf_nodes_tests tests/xbf_nodes_tests.cpp)
target_link_libraries(xbf_nodes_tests PRIVATE laml_xbf)
add_test(NAME xbf_nodes_tests COMMAND xbf_nodes_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-ui-xaml)

# Animation math: KeySpline and the XAML easing functions (adapted from MIT-licensed Microsoft.UI.Xaml).
add_library(laml_anim STATIC src/anim/keyspline.cpp src/anim/easing.cpp)
target_include_directories(laml_anim PUBLIC src)
laml_warnings(laml_anim)

add_executable(anim_tests tests/anim_tests.cpp)
target_link_libraries(anim_tests PRIVATE laml_anim)
add_test(NAME anim_tests COMMAND anim_tests)

# Resource lookup: ResourceDictionary with merged and theme dictionaries (independent implementation).
add_library(laml_resources STATIC src/resources/resource_dictionary.cpp)
target_include_directories(laml_resources PUBLIC src)
laml_warnings(laml_resources)

add_executable(resource_tests tests/resource_tests.cpp)
target_link_libraries(resource_tests PRIVATE laml_resources)
add_test(NAME resource_tests COMMAND resource_tests)

# Type catalog and property system (M2): native registry of types, namespaces, inheritance and
# property metadata; text metadata loader; read-only bridge to the XBF structure reader.
add_library(laml_types STATIC src/types/type_catalog.cpp src/types/metadata_text.cpp src/types/xbf_bridge.cpp src/types/stable_index.cpp)
target_include_directories(laml_types PUBLIC src)
target_link_libraries(laml_types PUBLIC laml_xbf)
laml_warnings(laml_types)

add_executable(types_tests tests/types_tests.cpp)
target_link_libraries(types_tests PRIVATE laml_types)
add_test(NAME types_tests COMMAND types_tests ${CMAKE_CURRENT_SOURCE_DIR}/data/xaml-core.types)

add_executable(types_xbf_tests tests/types_xbf_tests.cpp)
target_link_libraries(types_xbf_tests PRIVATE laml_types)
add_test(NAME types_xbf_tests COMMAND types_xbf_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-ui-xaml)

# Optional import of the WinUI stable XBF index tables (MIT test fixtures; nothing embedded).
add_executable(stable_index_tests tests/stable_index_tests.cpp)
target_link_libraries(stable_index_tests PRIVATE laml_types)
add_test(NAME stable_index_tests COMMAND stable_index_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-metadata ${CMAKE_CURRENT_SOURCE_DIR}/data/xaml-core.types)

# Logical tree (M3): XBF references resolved against the type catalog, and the node stream folded into a
# GUI-independent tree of descriptions. No objects are created and nothing is rendered.
add_library(laml_tree STATIC src/tree/ref_resolver.cpp src/tree/logical_tree.cpp src/tree/validate.cpp)
target_include_directories(laml_tree PUBLIC src)
target_link_libraries(laml_tree PUBLIC laml_types laml_xbf)
laml_warnings(laml_tree)

add_executable(tree_tests tests/tree_tests.cpp)
target_link_libraries(tree_tests PRIVATE laml_tree)
add_test(NAME tree_tests COMMAND tree_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-ui-xaml
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-metadata ${CMAKE_CURRENT_SOURCE_DIR}/data/xaml-core.types)

add_executable(m4_tests tests/m4_tests.cpp)
target_link_libraries(m4_tests PRIVATE laml_tree)
add_test(NAME m4_tests COMMAND m4_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-ui-xaml
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/winui-metadata ${CMAKE_CURRENT_SOURCE_DIR}/data/winui-api.types
         ${CMAKE_CURRENT_SOURCE_DIR}/data/xaml-core.types)

# xbftree (M4): fold a .xbf into the logical tree and report what the catalog resolves.
add_executable(xbftree tools/xbftree/xbftree.cpp)
target_link_libraries(xbftree PRIVATE laml_tree)
