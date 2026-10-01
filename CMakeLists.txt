cmake_minimum_required(VERSION 3.28)

project(RealSuperShadow VERSION 0.7.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Real Super Shadow requires Windows and the MSVC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure for x64 (for example, with -A x64).")
endif()

set(MILLER_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/miller-sdk" CACHE PATH
    "Directory containing the separately obtained miller-sdk")
set(DETOURS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/detours/Detours" CACHE PATH
    "Directory containing the separately obtained Microsoft Detours")

if(NOT EXISTS "${MILLER_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "miller-sdk is missing. Run setup_dependencies.ps1 or set MILLER_SDK_DIR.")
endif()
if(NOT EXISTS "${DETOURS_SOURCE_DIR}/src/detours.cpp")
    message(FATAL_ERROR "Microsoft Detours is missing. Run setup_dependencies.ps1 or set DETOURS_SOURCE_DIR.")
endif()

include(cmake/Detours.cmake)
add_subdirectory("${MILLER_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/miller-sdk" EXCLUDE_FROM_ALL)

add_library(mod SHARED
    src/DllMain.cpp
    src/Mod.cpp
    src/BetaAura.cpp
    src/BetaVersion.rc
    src/Runtime.h
)
target_compile_features(mod PRIVATE cxx_std_20)
target_compile_options(mod PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
target_compile_definitions(mod PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(mod PRIVATE miller-sdk detours)
set_target_properties(mod PROPERTIES OUTPUT_NAME "RealSuperShadowPOC")

configure_file(mod.ini.in mod.ini @ONLY)

# Stage locally by default; never infer a game installation directory.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/stage" CACHE PATH "Staging directory" FORCE)
endif()
install(TARGETS mod RUNTIME DESTINATION .)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mod.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/SuperVisuals.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/RealSuperShadow.ini"
    DESTINATION .
)
