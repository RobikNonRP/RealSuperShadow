# Project build adapter. Microsoft Detours itself is obtained separately.
add_library(detours STATIC)
target_sources(detours PRIVATE
    "${DETOURS_SOURCE_DIR}/src/creatwth.cpp"
    "${DETOURS_SOURCE_DIR}/src/disolarm.cpp"
    "${DETOURS_SOURCE_DIR}/src/disolx64.cpp"
    "${DETOURS_SOURCE_DIR}/src/modules.cpp"
    "${DETOURS_SOURCE_DIR}/src/detours.cpp"
    "${DETOURS_SOURCE_DIR}/src/disolarm64.cpp"
    "${DETOURS_SOURCE_DIR}/src/disolx86.cpp"
    "${DETOURS_SOURCE_DIR}/src/disasm.cpp"
    "${DETOURS_SOURCE_DIR}/src/disolia64.cpp"
    "${DETOURS_SOURCE_DIR}/src/image.cpp"
)
target_include_directories(detours PUBLIC "${DETOURS_SOURCE_DIR}/src")
