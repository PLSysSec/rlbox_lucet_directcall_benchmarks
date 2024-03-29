cmake_minimum_required(VERSION 3.13)

project(direct_call_benchmarks
        VERSION 0.1
        DESCRIPTION "Testing performance of direct calls with rlbox_lucet")

# Project Settings ###################

# set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
SET(ASM_OPTIONS "-x assembler-with-cpp")
enable_language(C CXX ASM)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-stack-protector")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector")

find_package(Threads REQUIRED)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(DYLIB_EXT "dylib")
else()
  set(DYLIB_EXT "so")
endif()

set(RUST_LIB_DEBUG_PATH   "${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/build_debug/cargo/debug/librlbox_lucet_sandbox.a")
set(RUST_LIB_RELEASE_PATH "${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/build_release/cargo/release/librlbox_lucet_sandbox.a")

add_library(lucet_sandbox_rustlib SHARED IMPORTED)
set_target_properties(lucet_sandbox_rustlib PROPERTIES IMPORTED_LOCATION ${RUST_LIB_DEBUG_PATH})
set_target_properties(lucet_sandbox_rustlib PROPERTIES IMPORTED_LOCATION_DEBUG ${RUST_LIB_DEBUG_PATH})
set_target_properties(lucet_sandbox_rustlib PROPERTIES IMPORTED_LOCATION_RELEASE ${RUST_LIB_RELEASE_PATH})

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  target_link_options(lucet_sandbox_rustlib INTERFACE "-framework" "Security" "-rdynamic")
else()
  target_link_options(lucet_sandbox_rustlib INTERFACE "-rdynamic")
endif()

if(MSVC)
  set(LINK_LUCET_LIB -WHOLEARCHIVE lucet_sandbox_rustlib)
else()
  set(LINK_LUCET_LIB -Wl,--whole-archive lucet_sandbox_rustlib -Wl,--no-whole-archive)
endif()


#######################################

add_executable(main_fullsave
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/lucet_binary.S
    ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src/trampoline_src.cpp
)
target_compile_definitions(main_fullsave
  PUBLIC GLUE_LIB_LUCET_PATH="${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/build_release/wasm/glue_lib_lucet.${DYLIB_EXT}"
)
target_include_directories(main_fullsave PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_sandboxing_api/code/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src
)
target_link_libraries(main_fullsave
                      ${LINK_LUCET_LIB}
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  target_link_libraries(main_fullsave rt)
endif()

#######################################

add_executable(main_regsave
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/lucet_binary.S
    ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src/trampoline_src.cpp
)
target_compile_definitions(main_regsave
  PUBLIC GLUE_LIB_LUCET_PATH="${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/build_release/wasm/glue_lib_lucet.${DYLIB_EXT}"
  PUBLIC RLBOX_ZEROCOST_NOSWITCHSTACK
)
target_include_directories(main_regsave PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_sandboxing_api/code/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src
)
target_link_libraries(main_regsave
                      ${LINK_LUCET_LIB}
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  target_link_libraries(main_regsave rt)
endif()

#######################################

add_executable(main_win
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/lucet_binary.S
    ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src/trampoline_src.cpp
)
target_compile_definitions(main_win
  PUBLIC GLUE_LIB_LUCET_PATH="${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/build_release/wasm/glue_lib_lucet.${DYLIB_EXT}"
  PUBLIC RLBOX_ZEROCOST_WINDOWSMODE
)
target_include_directories(main_win PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_sandboxing_api/code/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src
)
target_link_libraries(main_win
                      ${LINK_LUCET_LIB}
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  target_link_libraries(main_win rt)
endif()

#######################################

add_executable(main_zerocost
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/lucet_binary.S
    ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src/trampoline_src.cpp
)
target_compile_definitions(main_zerocost
  PUBLIC GLUE_LIB_LUCET_PATH="${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/build_release/wasm/glue_lib_lucet.${DYLIB_EXT}"
  PUBLIC RLBOX_ZEROCOST_TRANSITION_ZEROCOST
)
target_include_directories(main_zerocost PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_testing_sandbox/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_sandboxing_api/code/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src
)
target_link_libraries(main_zerocost
                      ${LINK_LUCET_LIB}
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  target_link_libraries(main_zerocost rt)
endif()

#######################################

add_executable(main_mpkfullsave
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/mpk_binary.S
    ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src/trampoline_src.cpp
)
set_target_properties(main_mpkfullsave PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
target_compile_definitions(main_mpkfullsave
  PUBLIC GLUE_LIB_LUCET_PATH="${CMAKE_SOURCE_DIR}/../rlbox_mpk_sandbox/build_release/librlbox_glue_lib_shared_32.${DYLIB_EXT}"
  PUBLIC RLBOX_ZEROCOST_TRANSITION_MPKFULLSAVE
)
target_include_directories(main_mpkfullsave PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_mpk_sandbox/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_sandboxing_api/code/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/include
                                         PUBLIC ${CMAKE_SOURCE_DIR}/../zerocost_heavy_trampoline/c_src
)
target_link_libraries(main_mpkfullsave
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  target_link_libraries(main_mpkfullsave rt)
endif()

#######################################

add_executable(main_segmentsfi
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/segmentsfi_binary.S
    ${CMAKE_SOURCE_DIR}/../rlbox_segmentsfizerocost_sandbox/c_src/segmentsfi_sandbox_runtime.cpp
)
set_target_properties(main_segmentsfi PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
target_compile_definitions(main_segmentsfi
  PUBLIC GLUE_LIB_LUCET_PATH=""
  PUBLIC RLBOX_ZEROCOST_TRANSITION_SEGMENTSFI
)
target_include_directories(main_segmentsfi PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_sandboxing_api/code/include
                                           PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_segmentsfizerocost_sandbox/include
                                           PUBLIC ${CMAKE_SOURCE_DIR}/../rlbox_segmentsfizerocost_sandbox/c_src
)
target_link_libraries(main_segmentsfi
                      ${CMAKE_THREAD_LIBS_INIT}
                      ${CMAKE_DL_LIBS}
)
if(UNIX AND NOT (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  target_link_libraries(main_segmentsfi rt)
endif()
