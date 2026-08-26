# 给 Scintilla 源码打 Qt6 补丁：把 QTextCodec 引用替换为本项目的兼容垫片
# 用法（由 FetchContent PATCH_COMMAND 调用）：
#   cmake -DSCINTILLA_PATCH_DIR=<SOURCE_DIR> \
#         -DMQT_PROJECT_SOURCE_DIR=<项目根目录> \
#         -P patch-scintilla-qt6.cmake
#
# 替换具有幂等性：重复运行时找不到目标字符串则内容不变。

set(files_to_patch
    qt/ScintillaEditBase/PlatQt.h
    qt/ScintillaEditBase/PlatQt.cpp
    qt/ScintillaEditBase/ScintillaQt.cpp
)

set(replacement
"#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include \"../../../cmake/qt_textcodec_compat.h\"
using QtCompat::QTextCodec;
#else
#include <QTextCodec>
#endif")

if(NOT EXISTS "${MQT_PROJECT_SOURCE_DIR}/cmake/qt_textcodec_compat.h")
    message(FATAL_ERROR "compat header missing: ${MQT_PROJECT_SOURCE_DIR}/cmake/qt_textcodec_compat.h")
endif()

file(COPY "${MQT_PROJECT_SOURCE_DIR}/cmake/qt_textcodec_compat.h"
     DESTINATION "${SCINTILLA_PATCH_DIR}/cmake")

foreach(rel ${files_to_patch})
    set(f "${SCINTILLA_PATCH_DIR}/${rel}")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "patch target not found: ${f}")
    endif()
    file(READ "${f}" content)
    string(FIND "${content}" "#include <QTextCodec>" pos)
    if(NOT pos EQUAL -1)
        string(REPLACE "#include <QTextCodec>" "${replacement}" content "${content}")
        file(WRITE "${f}" "${content}")
        message(STATUS "scintilla qt6 patch applied: ${rel}")
    else()
        message(STATUS "scintilla qt6 patch skipped (already patched?): ${rel}")
    endif()
endforeach()
