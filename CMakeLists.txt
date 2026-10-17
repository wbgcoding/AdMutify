cmake_minimum_required(VERSION 3.21)
project(AdMutify VERSION 1.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_CXX_FLAGS_RELEASE "/DNDEBUG")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

option(ADMUTIFY_REVIEW "Build with the surface review tool" OFF)

# The one place that holds the copyright year (version info, About line, installer via build.bat).
set(ADMUTIFY_COPYRIGHT_YEAR 2026)

configure_file(src/version.h.in "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)
configure_file(src/app.manifest.in "${CMAKE_BINARY_DIR}/generated/app.manifest" @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}")
file(WRITE "${CMAKE_BINARY_DIR}/copyright_year.txt" "${ADMUTIFY_COPYRIGHT_YEAR}")

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "/main[.]cpp$")
if(NOT ADMUTIFY_REVIEW)
  list(FILTER CORE_SOURCES EXCLUDE REGEX "/review[.]cpp$")
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")

# Static imports load from System32 only; control flow guard and CET shadow stack.
set(ADMUTIFY_HARDEN_LINK /DEPENDENTLOADFLAG:0x800 /guard:cf /CETCOMPAT)

function(admutify_common target)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8 /O1 /Zi /guard:cf>)
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
  if(ADMUTIFY_REVIEW)
    target_compile_definitions(${target} PRIVATE ADMUTIFY_REVIEW)
  endif()
  set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endfunction()

add_library(admutify_core STATIC ${CORE_SOURCES})
admutify_common(admutify_core)
target_link_libraries(admutify_core PUBLIC dbghelp ole32 mfplat mfuuid shlwapi delayimp comctl32 uxtheme dwmapi shell32 advapi32 user32 gdi32 gdiplus shcore windowscodecs)
# Media Foundation loads only when ad music first plays, so the idle footprint stays small.
target_link_options(admutify_core INTERFACE /DELAYLOAD:mfplat.dll)

add_executable(AdMutify WIN32 src/main.cpp src/app.rc)
admutify_common(AdMutify)
target_link_libraries(AdMutify PRIVATE admutify_core)
target_link_options(AdMutify PRIVATE ${ADMUTIFY_HARDEN_LINK} /DEBUG /OPT:REF /OPT:ICF /MANIFEST:NO)

# Same app as a single portable exe: settings and logs next to it, no marker file needed.
add_executable(AdMutifyPortable WIN32 src/main.cpp src/app.rc)
admutify_common(AdMutifyPortable)
target_compile_definitions(AdMutifyPortable PRIVATE ADMUTIFY_PORTABLE)
target_link_libraries(AdMutifyPortable PRIVATE admutify_core)
target_link_options(AdMutifyPortable PRIVATE ${ADMUTIFY_HARDEN_LINK} /DEBUG /OPT:REF /OPT:ICF /MANIFEST:NO)

add_executable(admutify_tests ${TEST_SOURCES})
admutify_common(admutify_tests)
target_link_libraries(admutify_tests PRIVATE admutify_core)
target_link_options(admutify_tests PRIVATE /DEBUG /OPT:REF /OPT:ICF)
target_compile_definitions(admutify_tests PRIVATE
  ADMUTIFY_TEST_DATA_DIR=L"${CMAKE_BINARY_DIR}/testdata"
  ADMUTIFY_SOURCE_DIR=L"${CMAKE_SOURCE_DIR}")

add_subdirectory(tests/rig)

enable_testing()
add_test(NAME admutify_tests COMMAND admutify_tests)
