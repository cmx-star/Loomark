cmake_minimum_required(VERSION 3.25)

project(markdown_qt
    VERSION 0.1.0
    DESCRIPTION "Native large-file Markdown editor P0 prototype"
    LANGUAGES CXX
)

include(GNUInstallDirs)

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

target_include_directories(md4qt
    BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt-compat"
)

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

set(MATHJAX_RUNTIME_MATHJAX_FILES
    "${MATHJAX_NODE_MODULES}/mathjax/package.json"
    "${MATHJAX_NODE_MODULES}/mathjax/LICENSE"
    "${MATHJAX_NODE_MODULES}/mathjax/node-main.mjs"
    "${MATHJAX_NODE_MODULES}/mathjax/node-main.js"
    "${MATHJAX_NODE_MODULES}/mathjax/node-main-setup.cjs"
)
set(MATHJAX_RUNTIME_INPUT_DIR "${MATHJAX_NODE_MODULES}/mathjax/input")
set(MATHJAX_RUNTIME_OUTPUT_DIR "${MATHJAX_NODE_MODULES}/mathjax/output")
set(MATHJAX_RUNTIME_ADAPTORS_DIR "${MATHJAX_NODE_MODULES}/mathjax/adaptors")
set(MATHJAX_RUNTIME_FONT_FILES
    "${MATHJAX_NODE_MODULES}/@mathjax/mathjax-newcm-font/package.json"
    "${MATHJAX_NODE_MODULES}/@mathjax/mathjax-newcm-font/svg.js"
)
set(MATHJAX_RUNTIME_FONT_SVG_DYNAMIC_DIR "${MATHJAX_NODE_MODULES}/@mathjax/mathjax-newcm-font/svg/dynamic")
set(MATHJAX_TEST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/mathjax-runtime")
set(MATHJAX_TEST_RUNTIME_STAMP "${MATHJAX_TEST_RUNTIME_DIR}/.stamp")

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
    src/gui/markdown_document_renderer.cpp
    src/gui/main_window.cpp
)

if(APPLE)
    target_sources(markdown_qt_gui
        PRIVATE
            resources/brand/loomark.icns
    )
    set_source_files_properties(resources/brand/loomark.icns
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
    )
endif()

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
if(WIN32)
    set_target_properties(markdown_qt_gui
        PROPERTIES
            WIN32_EXECUTABLE TRUE
    )
endif()
if(MQT_NODE_EXECUTABLE)
    target_compile_definitions(markdown_qt_gui
        PRIVATE
            "MQT_CONFIGURED_NODE_EXECUTABLE=\"${MQT_NODE_EXECUTABLE}\""
    )
endif()

if(APPLE)
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
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/mathjax"
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/@mathjax/mathjax-newcm-font"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MATHJAX_RUNTIME_MATHJAX_FILES}
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/mathjax"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MATHJAX_RUNTIME_INPUT_DIR}"
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/mathjax/input"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MATHJAX_RUNTIME_OUTPUT_DIR}"
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/mathjax/output"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MATHJAX_RUNTIME_ADAPTORS_DIR}"
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/mathjax/adaptors"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MATHJAX_RUNTIME_FONT_FILES}
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/@mathjax/mathjax-newcm-font"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MATHJAX_RUNTIME_FONT_SVG_DYNAMIC_DIR}"
                "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/node_modules/@mathjax/mathjax-newcm-font/svg/dynamic"
            VERBATIM
        )
    endif()
endif()

install(TARGETS markdown_qt_p0 markdown_qt_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(NOT APPLE)
    install(FILES tools/mathjax_svg.mjs DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(MQT_BUNDLE_MATHJAX_RUNTIME)
        install(FILES ${MATHJAX_RUNTIME_MATHJAX_FILES}
            DESTINATION ${CMAKE_INSTALL_BINDIR}/node_modules/mathjax
        )
        install(DIRECTORY "${MATHJAX_RUNTIME_INPUT_DIR}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}/node_modules/mathjax/input
        )
        install(DIRECTORY "${MATHJAX_RUNTIME_OUTPUT_DIR}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}/node_modules/mathjax/output
        )
        install(DIRECTORY "${MATHJAX_RUNTIME_ADAPTORS_DIR}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}/node_modules/mathjax/adaptors
        )
        install(FILES ${MATHJAX_RUNTIME_FONT_FILES}
            DESTINATION ${CMAKE_INSTALL_BINDIR}/node_modules/@mathjax/mathjax-newcm-font
        )
        install(DIRECTORY "${MATHJAX_RUNTIME_FONT_SVG_DYNAMIC_DIR}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}/node_modules/@mathjax/mathjax-newcm-font/svg/dynamic
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

add_custom_command(
    OUTPUT "${MATHJAX_TEST_RUNTIME_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${MATHJAX_TEST_RUNTIME_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/mathjax"
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/@mathjax/mathjax-newcm-font"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MATHJAX_SCRIPT}"
        "${MATHJAX_TEST_RUNTIME_DIR}/mathjax_svg.mjs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MATHJAX_RUNTIME_MATHJAX_FILES}
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/mathjax"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MATHJAX_RUNTIME_INPUT_DIR}"
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/mathjax/input"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MATHJAX_RUNTIME_OUTPUT_DIR}"
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/mathjax/output"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MATHJAX_RUNTIME_ADAPTORS_DIR}"
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/mathjax/adaptors"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MATHJAX_RUNTIME_FONT_FILES}
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/@mathjax/mathjax-newcm-font"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MATHJAX_RUNTIME_FONT_SVG_DYNAMIC_DIR}"
        "${MATHJAX_TEST_RUNTIME_DIR}/node_modules/@mathjax/mathjax-newcm-font/svg/dynamic"
    COMMAND ${CMAKE_COMMAND} -E touch
        "${MATHJAX_TEST_RUNTIME_STAMP}"
    DEPENDS
        "${MATHJAX_SCRIPT}"
        ${MATHJAX_RUNTIME_MATHJAX_FILES}
        ${MATHJAX_RUNTIME_FONT_FILES}
        "${MATHJAX_RUNTIME_FONT_SVG_DYNAMIC_DIR}"
    VERBATIM
)
add_custom_target(markdown_renderer_test_mathjax_runtime
    DEPENDS "${MATHJAX_TEST_RUNTIME_STAMP}"
)
add_dependencies(markdown_qt_markdown_renderer_tests markdown_renderer_test_mathjax_runtime)

add_test(NAME markdown_qt_markdown_renderer_tests COMMAND markdown_qt_markdown_renderer_tests)
set(MARKDOWN_RENDERER_TEST_ENV "MQT_MATHJAX_SCRIPT=${MATHJAX_TEST_RUNTIME_DIR}/mathjax_svg.mjs")
if(MQT_NODE_EXECUTABLE)
    list(APPEND MARKDOWN_RENDERER_TEST_ENV "MQT_NODE=${MQT_NODE_EXECUTABLE}")
endif()
set_tests_properties(markdown_qt_markdown_renderer_tests
    PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "${MARKDOWN_RENDERER_TEST_ENV}"
)
