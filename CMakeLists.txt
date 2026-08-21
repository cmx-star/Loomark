cmake_minimum_required(VERSION 3.25)

project(markdown_qt
    VERSION 0.1.0
    DESCRIPTION "Native large-file Markdown editor P0 prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(markdown_qt_core
    src/core/document_file.cpp
    src/core/file_tier.cpp
    src/core/markdown_index.cpp
)

target_include_directories(markdown_qt_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(markdown_qt_core
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_executable(markdown_qt_p0
    src/cli/main.cpp
)

target_link_libraries(markdown_qt_p0
    PRIVATE
        markdown_qt_core
)

enable_testing()

add_executable(markdown_qt_core_tests
    tests/core_tests.cpp
)

target_link_libraries(markdown_qt_core_tests
    PRIVATE
        markdown_qt_core
)

add_test(NAME markdown_qt_core_tests COMMAND markdown_qt_core_tests)
