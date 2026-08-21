cmake_minimum_required(VERSION 3.25)

project(markdown_qt
    VERSION 0.1.0
    DESCRIPTION "Native large-file Markdown editor P0 prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

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

find_package(Qt6 COMPONENTS Widgets REQUIRED)

add_executable(markdown_qt_p0
    src/cli/main.cpp
)

target_link_libraries(markdown_qt_p0
    PRIVATE
        markdown_qt_core
)

add_executable(markdown_qt_gui
    resources/resources.qrc
    src/gui/main.cpp
    src/gui/main_window.cpp
)

target_link_libraries(markdown_qt_gui
    PRIVATE
        markdown_qt_core
        Qt6::Widgets
)

target_compile_options(markdown_qt_gui
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

if(APPLE)
    set_target_properties(markdown_qt_gui
        PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Markdown Qt"
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.markdown-qt.app"
    )
endif()

enable_testing()

add_executable(markdown_qt_core_tests
    tests/core_tests.cpp
)

target_link_libraries(markdown_qt_core_tests
    PRIVATE
        markdown_qt_core
)

add_test(NAME markdown_qt_core_tests COMMAND markdown_qt_core_tests)
