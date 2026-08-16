cmake_minimum_required(VERSION 3.20)

project(mdt_cc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mdtc_lib
    src/compiler.cpp
)

target_include_directories(mdtc_lib PUBLIC include)
target_compile_options(mdtc_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(mdtc src/main.cpp)
target_link_libraries(mdtc PRIVATE mdtc_lib)

enable_testing()

add_executable(mdtc_tests tests/compiler_tests.cpp)
target_link_libraries(mdtc_tests PRIVATE mdtc_lib)
add_test(NAME mdtc_tests COMMAND mdtc_tests)
