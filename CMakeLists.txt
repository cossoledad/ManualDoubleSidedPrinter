cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project(ManualDoubleSidedPrinter VERSION 3.0.0 LANGUAGES CXX)

option(MDSP_BUILD_APP "Build the Windows desktop application" ${WIN32})
include(CTest)

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(mdsp_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(duplex STATIC src/duplex.cpp)
target_include_directories(duplex PUBLIC src)
target_compile_features(duplex PUBLIC cxx_std_20)
mdsp_target(duplex)

if(MDSP_BUILD_APP)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The desktop application requires Windows and MSVC.")
    endif()

    enable_language(RC)
    configure_file(src/app.rc.in app.rc @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app.rc" PROPERTIES OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/Assets/app.ico")
    add_executable(ManualDoubleSidedPrinter WIN32
        src/main.cpp
        src/paper_guide.cpp
        src/pdf_document.cpp
        src/pdf_preview.cpp
        src/printer.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    )
    target_compile_definitions(ManualDoubleSidedPrinter PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ManualDoubleSidedPrinter PRIVATE duplex comctl32 gdi32 ole32 shcore shell32 shlwapi uxtheme windowsapp winspool)
    target_link_options(ManualDoubleSidedPrinter PRIVATE /MANIFEST:NO)
    mdsp_target(ManualDoubleSidedPrinter)
    install(TARGETS ManualDoubleSidedPrinter RUNTIME DESTINATION .)
endif()

if(BUILD_TESTING)
    add_executable(duplex_test tests/duplex_test.cpp)
    target_link_libraries(duplex_test PRIVATE duplex)
    mdsp_target(duplex_test)
    add_test(NAME duplex COMMAND duplex_test)
endif()
