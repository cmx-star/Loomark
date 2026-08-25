cmake_minimum_required(VERSION 3.25)

project(markdown_qt
    VERSION 0.1.0
    DESCRIPTION "Native large-file Markdown editor P0 prototype"
    LANGUAGES C CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
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
    PATCH_COMMAND
        ${CMAKE_COMMAND}
        -DMD4QT_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-md4qt.cmake
)

FetchContent_MakeAvailable(md4qt)

FetchContent_Declare(quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
    GIT_TAG af0a40d94424476ce9365e881848af4c030517ec
)

FetchContent_GetProperties(quickjs)
if(NOT quickjs_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(quickjs)
    cmake_policy(POP)
endif()

add_library(qjs STATIC
    "${quickjs_SOURCE_DIR}/dtoa.c"
    "${quickjs_SOURCE_DIR}/libregexp.c"
    "${quickjs_SOURCE_DIR}/libunicode.c"
    "${quickjs_SOURCE_DIR}/quickjs.c"
)

target_include_directories(qjs
    PUBLIC
        "${quickjs_SOURCE_DIR}"
)

target_compile_definitions(qjs
    PRIVATE
        QUICKJS_NG_BUILD
        _GNU_SOURCE
)

if(WIN32)
    target_compile_definitions(qjs
        PRIVATE
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0601
    )
endif()

set_target_properties(qjs PROPERTIES C_STANDARD 11)
set_target_properties(qjs PROPERTIES AUTOMOC OFF)

if(MSVC)
    target_compile_options(qjs PRIVATE /experimental:c11atomics /wd4018 /wd4100 /wd4244 /wd4267 /wd4996)
else()
    target_compile_options(qjs PRIVATE -w)
endif()

find_package(Threads)
target_link_libraries(qjs PUBLIC Threads::Threads)
if(NOT WIN32)
    target_link_libraries(qjs PUBLIC ${CMAKE_DL_LIBS} m)
endif()

target_include_directories(md4qt
    BEFORE PUBLIC
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

find_package(Qt6 COMPONENTS Network Widgets Svg REQUIRED)

set(MQT_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version displayed in the GUI and update checks.")
if(DEFINED ENV{GITHUB_REF_NAME})
    set(MQT_GITHUB_REF_NAME "$ENV{GITHUB_REF_NAME}")
    if(MQT_GITHUB_REF_NAME MATCHES "^v[0-9]+(\\.[0-9]+)*")
        string(REGEX REPLACE "^v" "" MQT_APP_VERSION "${MQT_GITHUB_REF_NAME}")
    endif()
endif()

set(MATHJAX_BUNDLE_ENTRY "${CMAKE_CURRENT_SOURCE_DIR}/tools/quickjax_renderer/index.js")
set(MATHJAX_QUICKJS_BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mathjax-runtime")
set(MATHJAX_QUICKJS_BUNDLE "${MATHJAX_QUICKJS_BUNDLE_DIR}/mathjax_bundle.js")
option(MQT_BUNDLE_MATHJAX_RUNTIME "Bundle the local MathJax QuickJS runtime into the app." OFF)
find_program(MQT_NPM_EXECUTABLE NAMES npm npm.cmd)
if(NOT MQT_NPM_EXECUTABLE)
    message(FATAL_ERROR "npm was not found. Run `npm install` with Node.js before building formula preview support.")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/mathjax-full")
    message(FATAL_ERROR "MathJax npm dependency is missing. Run `npm install` before configuring formula preview support.")
endif()

add_custom_command(
    OUTPUT "${MATHJAX_QUICKJS_BUNDLE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MATHJAX_QUICKJS_BUNDLE_DIR}"
    COMMAND ${MQT_NPM_EXECUTABLE} exec -- esbuild
        "${MATHJAX_BUNDLE_ENTRY}"
        --bundle
        --minify
        --platform=browser
        --format=iife
        "--outfile=${MATHJAX_QUICKJS_BUNDLE}"
    DEPENDS
        "${MATHJAX_BUNDLE_ENTRY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(mathjax_quickjs_bundle
    DEPENDS "${MATHJAX_QUICKJS_BUNDLE}"
)

add_executable(markdown_qt_p0
    src/cli/main.cpp
)

target_link_libraries(markdown_qt_p0
    PRIVATE
        markdown_qt_core
)

add_executable(markdown_qt_gui
    resources/resources.qrc
    src/gui/application_logger.h
    src/gui/application_logger.cpp
    src/gui/file_inspect_worker.h
    src/gui/file_inspect_worker.cpp
    src/gui/main.cpp
    src/gui/markdown_document_renderer.cpp
    src/gui/preview_index_worker.h
    src/gui/preview_index_worker.cpp
    src/gui/update_checker.h
    src/gui/main_window.cpp
    src/gui/update_checker.cpp
    src/gui/window_boundary.h
    src/gui/window_boundary.cpp
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
        qjs
        Qt6::Network
        Qt6::Widgets
        Qt6::Svg
)
target_compile_definitions(markdown_qt_gui
    PRIVATE
        "MQT_APP_VERSION=\"${MQT_APP_VERSION}\""
)

target_compile_options(markdown_qt_gui
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)
if(WIN32)
    set_target_properties(markdown_qt_gui
        PROPERTIES
            WIN32_EXECUTABLE TRUE
            OUTPUT_NAME "Loomark"
    )
endif()
add_dependencies(markdown_qt_gui mathjax_quickjs_bundle)

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
            "${quickjs_SOURCE_DIR}/LICENSE"
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/licenses/quickjs-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/mathjax-full/LICENSE"
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/licenses/mathjax-Apache-2.0.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MATHJAX_QUICKJS_BUNDLE}"
            "$<TARGET_BUNDLE_CONTENT_DIR:markdown_qt_gui>/Resources/mathjax_bundle.js"
        VERBATIM
    )
endif()

install(TARGETS markdown_qt_p0 markdown_qt_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(NOT APPLE)
    install(FILES "${MATHJAX_QUICKJS_BUNDLE}" DESTINATION ${CMAKE_INSTALL_BINDIR})
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

add_executable(markdown_qt_window_boundary_tests
    src/gui/window_boundary.cpp
    tests/window_boundary_tests.cpp
)

target_include_directories(markdown_qt_window_boundary_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(markdown_qt_window_boundary_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_test(NAME markdown_qt_window_boundary_tests COMMAND markdown_qt_window_boundary_tests)

add_executable(markdown_qt_markdown_renderer_tests
    src/gui/markdown_document_renderer.cpp
    tests/markdown_document_renderer_tests.cpp
)

target_link_libraries(markdown_qt_markdown_renderer_tests
    PRIVATE
        md4qt::md4qt
        qjs
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
add_dependencies(markdown_qt_markdown_renderer_tests mathjax_quickjs_bundle)

add_test(NAME markdown_qt_markdown_renderer_tests COMMAND markdown_qt_markdown_renderer_tests)

add_executable(markdown_qt_update_checker_tests
    src/gui/update_checker.h
    src/gui/update_checker.cpp
    tests/update_checker_tests.cpp
)

target_link_libraries(markdown_qt_update_checker_tests
    PRIVATE
        Qt6::Network
)

target_include_directories(markdown_qt_update_checker_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(markdown_qt_update_checker_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_test(NAME markdown_qt_update_checker_tests COMMAND markdown_qt_update_checker_tests)

add_executable(markdown_qt_main_window_tests
    resources/resources.qrc
    src/gui/application_logger.h
    src/gui/application_logger.cpp
    src/gui/file_inspect_worker.h
    src/gui/file_inspect_worker.cpp
    src/gui/main_window.h
    src/gui/main_window.cpp
    src/gui/markdown_document_renderer.h
    src/gui/markdown_document_renderer.cpp
    src/gui/preview_index_worker.h
    src/gui/preview_index_worker.cpp
    src/gui/update_checker.h
    src/gui/update_checker.cpp
    src/gui/window_boundary.h
    src/gui/window_boundary.cpp
    tests/main_window_tests.cpp
)

target_link_libraries(markdown_qt_main_window_tests
    PRIVATE
        markdown_qt_core
        md4qt::md4qt
        qjs
        Qt6::Network
        Qt6::Widgets
        Qt6::Svg
)

target_compile_definitions(markdown_qt_main_window_tests
    PRIVATE
        "MQT_APP_VERSION=\"${MQT_APP_VERSION}\""
        MQT_BUILD_TESTS
)

target_compile_options(markdown_qt_main_window_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_dependencies(markdown_qt_main_window_tests mathjax_quickjs_bundle)
add_test(NAME markdown_qt_main_window_tests COMMAND markdown_qt_main_window_tests)

set(MARKDOWN_RENDERER_TEST_ENV "MQT_MATHJAX_BUNDLE=${MATHJAX_QUICKJS_BUNDLE}")
set_tests_properties(markdown_qt_markdown_renderer_tests
    PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "${MARKDOWN_RENDERER_TEST_ENV}"
)
set_tests_properties(markdown_qt_main_window_tests
    PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${MARKDOWN_RENDERER_TEST_ENV}"
)
