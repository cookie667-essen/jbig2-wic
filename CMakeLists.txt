cmake_minimum_required(VERSION 3.22)
project(jbig2_wic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statische CRT -> DLL lÃƒÆ’Ã‚Â¤uft auf jedem Windows ohne VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

file(GLOB PDFIUM_JBIG2_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/third_party/pdfium/core/fxcodec/jbig2/*.cpp"
)
set(PDFIUM_FAX_SOURCES
    "${CMAKE_SOURCE_DIR}/third_party/pdfium/core/fxcodec/fax/faxmodule.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/pdfium/core/fxcodec/scanlinedecoder.cpp"
)

add_library(jbig2_core STATIC
    src/jbig2_core.cpp
    src/jbig2_file_parser.cpp
    src/jbig2_mq_encoder.cpp
    ${PDFIUM_JBIG2_SOURCES}
    ${PDFIUM_FAX_SOURCES}
)
target_include_directories(jbig2_core
    PUBLIC  "${CMAKE_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/pdfium_shim"
            "${CMAKE_SOURCE_DIR}/third_party/pdfium"
)
if(MSVC)
    set_source_files_properties(${PDFIUM_JBIG2_SOURCES} ${PDFIUM_FAX_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
endif()

if(WIN32)
    add_library(jbig2_wic SHARED
        src/dll_main.cpp
        src/wic_decoder.cpp
        src/wic_encoder.cpp
        src/wic_registry.cpp
        src/jbig2_wic.def
    )
    target_link_libraries(jbig2_wic PRIVATE
        jbig2_core windowscodecs.lib ole32.lib shell32.lib advapi32.lib
    )
    target_compile_definitions(jbig2_wic PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    add_executable(wic_smoke_test tests/wic_smoke_test.cpp)
    target_link_libraries(wic_smoke_test PRIVATE windowscodecs.lib ole32.lib)
    target_compile_definitions(wic_smoke_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE jbig2_core)

    add_executable(encoder_test tests/encoder_test.cpp)
    target_link_libraries(encoder_test PRIVATE jbig2_core)

    add_executable(wic_roundtrip_test tests/wic_roundtrip_test.cpp)
    target_include_directories(wic_roundtrip_test PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wic_roundtrip_test PRIVATE windowscodecs.lib ole32.lib)
    target_compile_definitions(wic_roundtrip_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()