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

include(FetchContent)

set(BUILD_MD4QT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_MD4QT_BENCHMARK OFF CACHE BOOL "" FORCE)
set(BUILD_MD2HTML_APP OFF CACHE BOOL "" FORCE)
set(BUILD_MD2QDOC_APP OFF CACHE BOOL "" FORCE)
set(INSTALL_MD4QT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(md4qt
    GIT_REPOSITORY https://invent.kde.org/libraries/md4qt.git
    GIT_TAG 1a878810adeeb534de4de395f4951edd54ab6072
)

FetchContent_MakeAvailable(md4qt)

# md4qt targets C++17 internally. Restore this project's standard for targets below.
set(CMAKE_CXX_STANDARD 20)

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

find_package(Qt6 COMPONENTS Widgets Svg REQUIRED)

set(MATHJAX_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/mathjax_svg.mjs")
set(MATHJAX_NODE_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/node_modules")
option(MQT_BUNDLE_MATHJAX_RUNTIME "Bundle the local MathJax node_modules runtime into the app." OFF)
if(NOT EXISTS "${MATHJAX_NODE_MODULES}/mathjax")
    message(FATAL_ERROR "MathJax npm dependency is missing. Run `npm install` before configuring formula preview support.")
endif()
find_program(MQT_NODE_EXECUTABLE NAMES node)

add_executable(markdown_qt_p0
    src/cli/main.cpp
)

target_link_libraries(markdown_qt_p0
    PRIVATE
        markdown_qt_core
)

add_executable(markdown_qt_gui
    resources/resources.qrc
    resources/brand/loomark.icns
    src/gui/main.cpp
    src/gui/markdown_document_renderer.cpp
    src/gui/main_window.cpp
)

target_link_libraries(markdown_qt_gui
    PRIVATE
        markdown_qt_core
        md4qt::md4qt
        Qt6::Widgets
        Qt6::Svg
)

target_compile_options(markdown_qt_gui
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)
if(MQT_NODE_EXECUTABLE)
    target_compile_definitions(markdown_qt_gui
        PRIVATE
            "MQT_CONFIGURED_NODE_EXECUTABLE=\"${MQT_NODE_EXECUTABLE}\""
    )
endif()

if(APPLE)
    set_source_files_properties(resources/brand/loomark.icns
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
    )

    set_target_properties(markdown_qt_gui
        PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME "Loomark"
            MACOSX_BUNDLE_BUNDLE_NAME "Loomark"
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.loomark.app"
            MACOSX_BUNDLE_ICON_FILE "loomark.icns"
    )

    add_custom_command(TARGET markdown_qt_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${md4qt_SOURCE_DIR}/LICENSES/MIT.txt"
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/licenses/md4qt-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MATHJAX_SCRIPT}"
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/mathjax_svg.mjs"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MATHJAX_NODE_MODULES}/mathjax/LICENSE"
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/licenses/mathjax-Apache-2.0.txt"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules"
        VERBATIM
    )
    if(MQT_BUNDLE_MATHJAX_RUNTIME)
        add_custom_command(TARGET markdown_qt_gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MATHJAX_NODE_MODULES}"
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules"
            VERBATIM
        )
    endif()
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

add_executable(markdown_qt_markdown_renderer_tests
    src/gui/markdown_document_renderer.cpp
    tests/markdown_document_renderer_tests.cpp
)

target_link_libraries(markdown_qt_markdown_renderer_tests
    PRIVATE
        md4qt::md4qt
        Qt6::Widgets
        Qt6::Svg
)

target_include_directories(markdown_qt_markdown_renderer_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(markdown_qt_markdown_renderer_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)
if(MQT_NODE_EXECUTABLE)
    target_compile_definitions(markdown_qt_markdown_renderer_tests
        PRIVATE
            "MQT_CONFIGURED_NODE_EXECUTABLE=\"${MQT_NODE_EXECUTABLE}\""
    )
endif()

add_test(NAME markdown_qt_markdown_renderer_tests COMMAND markdown_qt_markdown_renderer_tests)
set_tests_properties(markdown_qt_markdown_renderer_tests
    PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "MQT_MATHJAX_SCRIPT=${MATHJAX_SCRIPT}"
)
