cmake_minimum_required(VERSION 3.5.0)
project(lsm)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH} ${CORE})

add_library(lsm SHARED
    napi_init.cpp
    ${CORE}/lsm_host.c
    ${CORE}/core.c
    ${CORE}/xcrypto.c
    ${CORE}/third_party/monocypher.c
    ${CORE}/third_party/monocypher-ed25519.c)

target_compile_definitions(lsm PRIVATE LSM_APP_VERSION="0.7.4")
target_compile_options(lsm PRIVATE -O2 -Wno-unused-function)
set_source_files_properties(
    ${CORE}/lsm_host.c ${CORE}/core.c ${CORE}/xcrypto.c
    ${CORE}/third_party/monocypher.c ${CORE}/third_party/monocypher-ed25519.c
    PROPERTIES LANGUAGE C COMPILE_FLAGS "-std=gnu99 -D_GNU_SOURCE")

target_link_libraries(lsm PUBLIC libace_napi.z.so libhilog_ndk.z.so pthread)
