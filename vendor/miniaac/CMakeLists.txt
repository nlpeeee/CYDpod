cmake_minimum_required(VERSION 3.16)
set(MINIAAC_SOURCES src/miniaac.c
    src/helix/aacdec.c src/helix/aactabs.c src/helix/bitstream.c
    src/helix/buffers.c src/helix/dct4.c src/helix/decelmnt.c
    src/helix/dequant.c src/helix/fft.c src/helix/filefmt.c
    src/helix/huffman.c src/helix/hufftabs.c src/helix/imdct.c
    src/helix/noiseless.c src/helix/pns.c src/helix/stproc.c
    src/helix/tns.c src/helix/trigtabs.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${MINIAAC_SOURCES} INCLUDE_DIRS src)
    set(MINIAAC_TARGET ${COMPONENT_LIB})
else()
    project(miniaac VERSION 1.0.0 LANGUAGES C)
    add_library(miniaac STATIC ${MINIAAC_SOURCES})
    target_include_directories(miniaac PUBLIC src)
    set(MINIAAC_TARGET miniaac)
    option(MINIAAC_BUILD_TOOLS "Build host decoder and API checks" ON)
    if(MINIAAC_BUILD_TOOLS)
        enable_testing()
        add_executable(miniaac_decode tests/decode.c)
        target_link_libraries(miniaac_decode PRIVATE miniaac)
        add_executable(miniaac_api_test tests/api_test.c)
        target_link_libraries(miniaac_api_test PRIVATE miniaac)
        add_test(NAME api COMMAND miniaac_api_test)
    endif()
endif()
target_compile_features(${MINIAAC_TARGET} PRIVATE c_std_99)
if(MSVC)
    target_compile_options(${MINIAAC_TARGET} PRIVATE /O2 /W3)
    target_compile_definitions(${MINIAAC_TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${MINIAAC_TARGET} PRIVATE -Os -fwrapv -fno-strict-aliasing
        -ffunction-sections -fdata-sections)
endif()
