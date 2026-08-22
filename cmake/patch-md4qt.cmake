function(mqt_replace_in_file file_path search_text replace_text)
    file(READ "${file_path}" file_content)
    string(REPLACE "${search_text}" "${replace_text}" patched_content "${file_content}")
    if(NOT file_content STREQUAL patched_content)
        file(WRITE "${file_path}" "${patched_content}")
    endif()
endfunction()

mqt_replace_in_file(
    "${MD4QT_SOURCE_DIR}/src/text_stream.h"
    "QString(view().sliced(pos, len)).replace(QChar(), s_replaceChar)"
    "view().sliced(pos, len).toString().replace(QChar(), s_replaceChar)"
)
mqt_replace_in_file(
    "${MD4QT_SOURCE_DIR}/src/atx_heading_parser.cpp"
    "QString(currentLine.view())"
    "currentLine.view().toString()"
)
mqt_replace_in_file(
    "${MD4QT_SOURCE_DIR}/src/parser.cpp"
    "fn.removeAt(0);"
    "fn.remove(0, 1);"
)
foreach(md4qt_qstring_remove_last
        "src/autolink_parser.cpp:url"
        "src/link_image_parser.cpp:url"
        "src/inline_html_parser.cpp:tag"
        "src/inline_code_parser.cpp:code"
        "src/inline_math_parser.cpp:code"
        "src/table_parser.cpp:data"
        "src/paragraph_parser.cpp:url"
        "src/utils.cpp:tag")
    string(REPLACE ":" ";" md4qt_patch_parts "${md4qt_qstring_remove_last}")
    list(GET md4qt_patch_parts 0 md4qt_patch_file)
    list(GET md4qt_patch_parts 1 md4qt_patch_variable)
    mqt_replace_in_file(
        "${MD4QT_SOURCE_DIR}/${md4qt_patch_file}"
        "${md4qt_patch_variable}.removeLast();"
        "${md4qt_patch_variable}.chop(1);"
    )
endforeach()

foreach(md4qt_qstring_remove_first
        "src/link_image_parser.cpp:url"
        "src/inline_html_parser.cpp:tag"
        "src/inline_code_parser.cpp:code"
        "src/inline_math_parser.cpp:code"
        "src/paragraph_parser.cpp:url"
        "src/utils.cpp:tag")
    string(REPLACE ":" ";" md4qt_patch_parts "${md4qt_qstring_remove_first}")
    list(GET md4qt_patch_parts 0 md4qt_patch_file)
    list(GET md4qt_patch_parts 1 md4qt_patch_variable)
    mqt_replace_in_file(
        "${MD4QT_SOURCE_DIR}/${md4qt_patch_file}"
        "${md4qt_patch_variable}.removeFirst();"
        "${md4qt_patch_variable}.remove(0, 1);"
    )
endforeach()
