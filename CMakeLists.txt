cmake_minimum_required(VERSION 3.16)

project(sysy2022_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

find_path(KOOPA_INCLUDE_DIR koopa.h PATHS /opt/include)
find_library(KOOPA_LIBRARY koopa PATHS /opt/lib/native /opt/lib)

if (NOT KOOPA_INCLUDE_DIR OR NOT KOOPA_LIBRARY)
    message(FATAL_ERROR "libkoopa was not found. Build inside maxxing/compiler-dev or set KOOPA paths.")
endif()

BISON_TARGET(
    SysYParser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/sysy.y
    ${CMAKE_CURRENT_BINARY_DIR}/sysy_parser.cpp
    DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/sysy_parser.hpp
)

FLEX_TARGET(
    SysYScanner
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/sysy.l
    ${CMAKE_CURRENT_BINARY_DIR}/sysy_lexer.cpp
)

ADD_FLEX_BISON_DEPENDENCY(SysYScanner SysYParser)

add_executable(compiler
    ${BISON_SysYParser_OUTPUTS}
    ${FLEX_SysYScanner_OUTPUTS}
    src/main.cpp
    src/ast/ast.cpp
    src/frontend/driver.cpp
    src/semantic/SemanticAnalyzer.cpp
    src/backend/RiscV.cpp
)

target_include_directories(compiler PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${KOOPA_INCLUDE_DIR}
)

target_link_libraries(compiler PRIVATE ${KOOPA_LIBRARY})

if (MSVC)
    target_compile_options(compiler PRIVATE /W4)
else()
    target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
