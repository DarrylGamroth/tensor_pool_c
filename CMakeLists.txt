cmake_minimum_required(VERSION 3.24)

project(tensor_pool_c LANGUAGES C)

include(CheckIncludeFile)
include(CheckSymbolExists)
include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TP_ENABLE_SBE_CODEGEN "Enable SBE code generation" ON)
option(TP_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
option(TP_ENABLE_FUZZ "Enable libFuzzer targets (requires clang)" OFF)
option(TP_USE_SYSTEM_AERON "Prefer system Aeron install when available" ON)
set(TP_COVERAGE_MIN 0 CACHE STRING "Minimum line coverage percent for coverage target (0 disables)")

set(AERON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../aeron" CACHE PATH "Path to Aeron source tree")
set(AERON_INCLUDE_DIR "" CACHE PATH "Path to Aeron headers for system installs")
set(AERON_LIBRARY "" CACHE FILEPATH "Path to Aeron library for system installs")
set(SBE_TOOL_JAR "" CACHE FILEPATH "Path to sbe-all jar")
set(SBE_TOOL_VERSION "" CACHE STRING "SBE tool version to download (empty = latest)")
set(SBE_TOOL_CACHE_DIR "" CACHE PATH "Cache directory for downloaded sbe-all jars")
set(SBE_TOOL_CLASSPATH "" CACHE STRING "Classpath for SBE tool invocation")
set(SBE_JAVA_MAIN "" CACHE STRING "SBE tool main class (auto-detected if empty)")
set(TP_TOML_SOURCE "https://github.com/cktan/tomlc17.git" CACHE STRING "TOML parser source repository")
set(TP_TOML_TAG "main" CACHE STRING "TOML parser git tag/branch")

set(AERON_TARGET "")
set(AERON_VERSION_TXT "0.0.0")
if (EXISTS "${AERON_ROOT}/version.txt")
    file(STRINGS "${AERON_ROOT}/version.txt" AERON_VERSION_TXT LIMIT_COUNT 1 REGEX "^[0-9]+(\\.[0-9]+)+")
    if (AERON_VERSION_TXT STREQUAL "")
        set(AERON_VERSION_TXT "0.0.0")
    endif ()
endif ()

string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*$" "\\1;\\2;\\3" AERON_VERSION_PARTS "${AERON_VERSION_TXT}")
list(GET AERON_VERSION_PARTS 0 AERON_VERSION_MAJOR)
list(GET AERON_VERSION_PARTS 1 AERON_VERSION_MINOR)
list(GET AERON_VERSION_PARTS 2 AERON_VERSION_PATCH)

add_definitions(-DAERON_VERSION_TXT="${AERON_VERSION_TXT}")
add_definitions(-DAERON_VERSION_MAJOR=${AERON_VERSION_MAJOR})
add_definitions(-DAERON_VERSION_MINOR=${AERON_VERSION_MINOR})
add_definitions(-DAERON_VERSION_PATCH=${AERON_VERSION_PATCH})
add_definitions(-DAERON_VERSION_GITSHA="unknown")

FetchContent_Declare(
    tomlc17
    GIT_REPOSITORY ${TP_TOML_SOURCE}
    GIT_TAG ${TP_TOML_TAG}
)
FetchContent_MakeAvailable(tomlc17)
add_library(tomlc17 STATIC "${tomlc17_SOURCE_DIR}/src/tomlc17.c")
target_include_directories(tomlc17 PUBLIC "${tomlc17_SOURCE_DIR}/src")

if (TP_USE_SYSTEM_AERON)
    if (AERON_INCLUDE_DIR AND AERON_LIBRARY)
        add_library(aeron::aeron INTERFACE IMPORTED)
        target_include_directories(aeron::aeron INTERFACE "${AERON_INCLUDE_DIR}")
        target_link_libraries(aeron::aeron INTERFACE "${AERON_LIBRARY}")
        set(AERON_TARGET aeron::aeron)
    endif ()
endif ()

if (AERON_TARGET STREQUAL "")
    if (NOT EXISTS "${AERON_ROOT}/aeron-client/src/main/c/CMakeLists.txt")
        message(FATAL_ERROR "Aeron C client not found at ${AERON_ROOT}/aeron-client/src/main/c. Install Aeron or set -DAERON_ROOT=..., -DAERON_INCLUDE_DIR=..., -DAERON_LIBRARY=...")
    endif ()
    set(AERON_INSTALL_TARGETS OFF CACHE BOOL "Disable Aeron install targets" FORCE)
    add_subdirectory("${AERON_ROOT}/aeron-client/src/main/c" "${CMAKE_CURRENT_BINARY_DIR}/aeron-client")
    set(AERON_TARGET aeron::aeron_static)
    set(AERON_C_CLIENT_SOURCE_PATH "${AERON_ROOT}/aeron-client/src/main/c")
    if (EXISTS "${AERON_ROOT}/aeron-driver/src/main/c/CMakeLists.txt")
        add_subdirectory("${AERON_ROOT}/aeron-driver/src/main/c" "${CMAKE_CURRENT_BINARY_DIR}/aeron-driver")
    endif ()
endif ()

set(TP_PUBLIC_HEADERS
    include/tensor_pool/tp.h
    include/tensor_pool/tp_driver.h
    include/tensor_pool/common/tp_agent.h
    include/tensor_pool/common/tp_clock.h
    include/tensor_pool/common/tp_context.h
    include/tensor_pool/common/tp_error.h
    include/tensor_pool/common/tp_handles.h
    include/tensor_pool/common/tp_join_barrier.h
    include/tensor_pool/common/tp_log.h
    include/tensor_pool/common/tp_merge_map.h
    include/tensor_pool/common/tp_seqlock.h
    include/tensor_pool/common/tp_shm.h
    include/tensor_pool/common/tp_slot.h
    include/tensor_pool/common/tp_tensor.h
    include/tensor_pool/common/tp_trace.h
    include/tensor_pool/common/tp_tracelink.h
    include/tensor_pool/common/tp_types.h
    include/tensor_pool/common/tp_uri.h
    include/tensor_pool/common/tp_version.h
    include/tensor_pool/driver/tp_driver.h
    include/tensor_pool/driver/tp_driver_agent.h
    include/tensor_pool/discovery/tp_discovery_service.h
    include/tensor_pool/tp_discovery_service.h
    include/tensor_pool/supervisor/tp_supervisor.h
    include/tensor_pool/tp_supervisor.h
    include/tensor_pool/client/tp_client.h
    include/tensor_pool/client/tp_consumer.h
    include/tensor_pool/client/tp_control.h
    include/tensor_pool/client/tp_control_view.h
    include/tensor_pool/client/tp_discovery_client.h
    include/tensor_pool/client/tp_driver_client.h
    include/tensor_pool/client/tp_producer.h
)

set(TP_SOURCES
    src/common/tp_agent.c
    src/common/tp_aeron.c
    src/common/tp_aeron_wrap.c
    src/common/tp_clock.c
    src/common/tp_context.c
    src/common/tp_join_barrier.c
    src/common/tp_log.c
    src/common/tp_merge_map.c
    src/common/tp_mpsc_queue.c
    src/common/tp_shm.c
    src/common/tp_slot.c
    src/common/tp_tensor.c
    src/common/tp_trace.c
    src/common/tp_tracelink.c
    src/common/tp_uri.c
    src/common/tp_version.c
    src/client/tp_client.c
    src/client/tp_client_conductor_agent.c
    src/client/tp_client_conductor.c
    src/client/tp_consumer.c
    src/client/tp_consumer_manager.c
    src/client/tp_consumer_registry.c
    src/client/tp_control.c
    src/client/tp_control_adapter.c
    src/client/tp_control_poller.c
    src/client/tp_discovery_client.c
    src/client/tp_driver_client.c
    src/client/tp_metadata_poller.c
    src/client/tp_producer.c
    src/client/tp_progress_poller.c
    src/client/tp_qos.c
    src/driver/tp_driver.c
    src/driver/tp_driver_agent.c
    src/driver/tp_driver_config.c
    src/discovery/tp_discovery_service.c
    src/discovery/tp_discovery_config.c
    src/supervisor/tp_supervisor.c
    src/supervisor/tp_supervisor_config.c
)

if (TP_ENABLE_SBE_CODEGEN)
    find_package(Java REQUIRED COMPONENTS Runtime)

    if (SBE_TOOL_CLASSPATH STREQUAL "" AND DEFINED ENV{SBE_TOOL_CLASSPATH})
        set(SBE_TOOL_CLASSPATH "$ENV{SBE_TOOL_CLASSPATH}")
    endif ()

    if (SBE_TOOL_JAR STREQUAL "" OR NOT EXISTS "${SBE_TOOL_JAR}")
        include("${CMAKE_CURRENT_LIST_DIR}/cmake/FetchSbeTool.cmake")
        tp_fetch_sbe_tool(SBE_TOOL_JAR)
    endif ()

    if (NOT EXISTS "${SBE_TOOL_JAR}")
        message(FATAL_ERROR "SBE tool jar not found. Set -DSBE_TOOL_JAR=/path/to/sbe-all-<ver>.jar")
    endif ()
    if (SBE_TOOL_CLASSPATH STREQUAL "")
        set(SBE_TOOL_CLASSPATH "${SBE_TOOL_JAR}")
    endif ()

    set(SBE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(SBE_OUTPUT_WIRE "${SBE_OUTPUT_DIR}/wire")
    set(SBE_OUTPUT_DRIVER "${SBE_OUTPUT_DIR}/driver")
    set(SBE_OUTPUT_DISCOVERY "${SBE_OUTPUT_DIR}/discovery")
    set(SBE_OUTPUT_BRIDGE "${SBE_OUTPUT_DIR}/bridge")
    set(SBE_OUTPUT_MERGE "${SBE_OUTPUT_DIR}/merge")
    set(SBE_OUTPUT_TRACE "${SBE_OUTPUT_DIR}/trace")
    set(SBE_SCHEMA_DIR "${CMAKE_CURRENT_LIST_DIR}/schemas")
    set(SBE_JAVA_OPTS "--add-opens" "java.base/jdk.internal.misc=ALL-UNNAMED")
    if (SBE_JAVA_MAIN STREQUAL "")
        find_program(JAR_EXECUTABLE jar)
        if (JAR_EXECUTABLE)
            execute_process(
                COMMAND "${JAR_EXECUTABLE}" tf "${SBE_TOOL_JAR}"
                RESULT_VARIABLE SBE_JAR_RESULT
                OUTPUT_VARIABLE SBE_JAR_CONTENTS
                ERROR_QUIET)
        else ()
            set(SBE_JAR_RESULT 1)
        endif ()

        if (SBE_JAR_RESULT EQUAL 0)
            if (SBE_JAR_CONTENTS MATCHES "uk/co/real_logic/sbe/SbeTool.class")
                set(SBE_JAVA_MAIN "uk.co.real_logic.sbe.SbeTool")
            elseif (SBE_JAR_CONTENTS MATCHES "org/agrona/sbe/SbeTool.class")
                set(SBE_JAVA_MAIN "org.agrona.sbe.SbeTool")
            else ()
                message(FATAL_ERROR "SBE tool main class not found in ${SBE_TOOL_JAR}. Set -DSBE_JAVA_MAIN=...")
            endif ()
        else ()
            set(SBE_JAVA_MAIN "uk.co.real_logic.sbe.SbeTool")
        endif ()
    endif ()

    add_custom_command(
        OUTPUT "${SBE_OUTPUT_DIR}/tp_codegen.stamp"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_WIRE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_DRIVER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_DISCOVERY}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_BRIDGE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_MERGE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_TRACE}"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_WIRE}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/wire-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_DRIVER}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/driver-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_DISCOVERY}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/discovery-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_BRIDGE}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/bridge-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_MERGE}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/join-barrier-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_TRACE}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/trace-link-schema.xml"
        COMMAND "${CMAKE_COMMAND}" -E touch "${SBE_OUTPUT_DIR}/tp_codegen.stamp"
        DEPENDS
            "${SBE_SCHEMA_DIR}/wire-schema.xml"
            "${SBE_SCHEMA_DIR}/driver-schema.xml"
            "${SBE_SCHEMA_DIR}/discovery-schema.xml"
            "${SBE_SCHEMA_DIR}/bridge-schema.xml"
            "${SBE_SCHEMA_DIR}/join-barrier-schema.xml"
            "${SBE_SCHEMA_DIR}/trace-link-schema.xml"
        VERBATIM
    )

    add_custom_target(tp_sbe_codegen DEPENDS "${SBE_OUTPUT_DIR}/tp_codegen.stamp")
endif ()

add_library(tensor_pool ${TP_SOURCES} ${TP_PUBLIC_HEADERS})
add_library(tensor_pool::tensor_pool ALIAS tensor_pool)

if (TP_ENABLE_SBE_CODEGEN)
    add_dependencies(tensor_pool tp_sbe_codegen)
    target_include_directories(tensor_pool PUBLIC "${SBE_OUTPUT_DIR}")
endif ()

target_include_directories(tensor_pool
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src/common>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src/internal>"
)

target_link_libraries(tensor_pool PUBLIC ${AERON_TARGET})
target_link_libraries(tensor_pool PUBLIC tomlc17)

install(TARGETS tensor_pool
    EXPORT tensor_poolTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

foreach (header IN LISTS TP_PUBLIC_HEADERS)
    get_filename_component(header_dir "${CMAKE_CURRENT_LIST_DIR}/${header}" DIRECTORY)
    file(RELATIVE_PATH header_rel_dir "${CMAKE_CURRENT_LIST_DIR}/include" "${header_dir}")
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/${header}" DESTINATION "include/${header_rel_dir}")
endforeach ()

if (TP_ENABLE_COVERAGE)
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
        target_compile_options(tensor_pool PRIVATE -O0 -g --coverage)
        target_link_options(tensor_pool PRIVATE --coverage)
    else ()
        message(WARNING "TP_ENABLE_COVERAGE is set but compiler does not support gcov-style coverage")
    endif ()
endif ()

enable_testing()

add_executable(tensor_pool_tests
    tests/test_tp_smoke.c
    tests/test_tp_control.c
    tests/test_tp_control_listen_json.c
    tests/test_tp_control_poller.c
    tests/test_tp_control_poller_errors.c
    tests/test_tp_control_poller_driver.c
    tests/test_tp_qos_poller.c
    tests/test_tp_metadata_poller.c
    tests/test_tp_consumer_registry.c
    tests/test_tp_tensor_extra.c
    tests/test_tp_aeron.c
    tests/test_tp_agent.c
    tests/test_tp_accessors.c
    tests/test_tp_client_errors.c
    tests/test_tp_client_context.c
    tests/test_tp_consumer_errors.c
    tests/test_tp_producer_errors.c
    tests/test_tp_driver_client.c
    tests/test_tp_driver_client_live.c
    tests/test_tp_driver_integration.c
    tests/test_tp_discovery_client.c
    tests/test_tp_discovery_client_live.c
    tests/test_tp_merge_map.c
    tests/test_tp_join_barrier.c
    tests/test_tp_tracelink.c
    tests/test_tp_client_conductor.c
    tests/test_tp_pollers.c
    tests/test_tp_progress_poller_extra.c
    tests/test_tp_lease_revoked.c
    tests/test_tp_rollover.c
    tests/test_tp_shm_roundtrip.c
    tests/test_tp_producer_claim.c
    tests/test_tp_log.c
    tests/test_tp_shm_security.c
    tests/test_tp_consumer_apply.c
    tests/test_tp_driver_config.c
    tests/test_tp_discovery_service.c
    tests/test_tp_supervisor.c)
target_link_libraries(tensor_pool_tests PRIVATE tensor_pool ${AERON_TARGET})
target_include_directories(tensor_pool_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/common")
target_include_directories(tensor_pool_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/internal")
target_compile_definitions(tensor_pool_tests PRIVATE TP_TEST_CONFIG_DIR="${CMAKE_CURRENT_LIST_DIR}/config")
target_compile_definitions(tensor_pool_tests PRIVATE TP_TESTING=1)
target_compile_definitions(tensor_pool PRIVATE TP_TESTING=1)
if (TARGET aeron::aeron_driver_static OR TARGET aeron_driver_static)
    if (TARGET aeron::aeron_driver_static)
        target_link_libraries(tensor_pool_tests PRIVATE aeron::aeron_driver_static)
    else ()
        target_link_libraries(tensor_pool_tests PRIVATE aeron_driver_static)
    endif ()
    target_compile_definitions(tensor_pool_tests PRIVATE TP_TEST_EMBEDDED_DRIVER=1)
endif ()
if (TP_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tensor_pool_tests PRIVATE -O0 -g --coverage)
    target_link_options(tensor_pool_tests PRIVATE --coverage)
endif ()
add_test(NAME tensor_pool_smoke COMMAND tensor_pool_tests)

if (TP_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    find_program(GCOVR_EXECUTABLE gcovr)
    if (GCOVR_EXECUTABLE)
        set(GCOVR_ARGS
            -r "${CMAKE_CURRENT_LIST_DIR}"
            --exclude "${AERON_ROOT}"
            --exclude ".*CMakeFiles.*"
            --exclude ".*generated.*"
            --exclude ".*aeron-client.*"
            --exclude ".*build.*"
            --exclude ".*tests/.*"
            --exclude ".*tools/.*"
            --exclude ".*fuzz/.*"
            --gcov-ignore-errors=no_working_dir_found
            --gcov-ignore-errors=source_not_found
            --gcov-ignore-errors=output_error
            --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
            --print-summary
            --xml-pretty -o coverage.xml
            --html-details -o coverage.html)
        if (TP_COVERAGE_MIN GREATER 0)
            list(APPEND GCOVR_ARGS --fail-under-line ${TP_COVERAGE_MIN})
        endif ()
        add_custom_target(coverage
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            COMMAND "${GCOVR_EXECUTABLE}" ${GCOVR_ARGS}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS tensor_pool_tests)
    else ()
        add_custom_target(coverage
            COMMAND "${CMAKE_COMMAND}" -E echo "gcovr not found; install gcovr to generate coverage reports."
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS tensor_pool_tests)
    endif ()
endif ()

add_executable(tp_shm_inspect tools/tp_shm_inspect.c)
target_link_libraries(tp_shm_inspect PRIVATE tensor_pool)

add_library(tp_example_util STATIC examples/tp_sample_util.c)
target_link_libraries(tp_example_util PRIVATE tensor_pool)
target_include_directories(tp_example_util PUBLIC "${CMAKE_CURRENT_LIST_DIR}/examples")

add_executable(tp_example_producer_driver examples/tp_example_producer_driver.c)
add_executable(tp_example_producer_nodriver examples/tp_example_producer_nodriver.c)
add_executable(tp_example_consumer_driver examples/tp_example_consumer_driver.c)
add_executable(tp_example_consumer_nodriver examples/tp_example_consumer_nodriver.c)
add_executable(tp_example_join_barrier examples/tp_example_join_barrier.c)
target_link_libraries(tp_example_producer_driver PRIVATE tensor_pool tp_example_util)
target_link_libraries(tp_example_producer_nodriver PRIVATE tensor_pool tp_example_util)
target_link_libraries(tp_example_consumer_driver PRIVATE tensor_pool tp_example_util)
target_link_libraries(tp_example_consumer_nodriver PRIVATE tensor_pool tp_example_util)
target_link_libraries(tp_example_join_barrier PRIVATE tensor_pool tp_example_util)
set(TP_INTERNAL_INCLUDE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/src/common"
    "${CMAKE_CURRENT_LIST_DIR}/src/internal")
target_include_directories(tp_example_producer_driver PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")
target_include_directories(tp_example_producer_nodriver PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")
target_include_directories(tp_example_consumer_driver PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")
target_include_directories(tp_example_consumer_nodriver PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")
target_include_directories(tp_example_join_barrier PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")

add_executable(tp_control_listen tools/tp_control_listen.c)
target_link_libraries(tp_control_listen PRIVATE tensor_pool)
add_executable(tp_descriptor_listen tools/tp_descriptor_listen.c)
target_link_libraries(tp_descriptor_listen PRIVATE tensor_pool)
target_include_directories(tp_control_listen PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")
target_include_directories(tp_descriptor_listen PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")

add_executable(tp_example_bgapi examples/tp_example_bgapi.c)
target_link_libraries(tp_example_bgapi PRIVATE tensor_pool tp_example_util)
target_include_directories(tp_example_bgapi PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")

add_executable(tp_shm_create tools/tp_shm_create.c)
target_link_libraries(tp_shm_create PRIVATE tensor_pool)

add_executable(tp_driver tools/tp_driver.c)
target_link_libraries(tp_driver PRIVATE tensor_pool)
target_include_directories(tp_driver PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")

add_executable(tp_discoveryd tools/tp_discoveryd.c)
target_link_libraries(tp_discoveryd PRIVATE tensor_pool)
target_include_directories(tp_discoveryd PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")

add_executable(tp_discovery_query tools/tp_discovery_query.c)
target_link_libraries(tp_discovery_query PRIVATE tensor_pool)
target_include_directories(tp_discovery_query PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")

if (TP_ENABLE_FUZZ)
    if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "TP_ENABLE_FUZZ requires clang with libFuzzer; configure with CC=clang")
    endif ()

    function(tp_add_fuzzer name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE tensor_pool)
        target_include_directories(${name} PRIVATE "${TP_INTERNAL_INCLUDE_DIR}")
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_compile_definitions(${name} PRIVATE TP_ENABLE_FUZZ=1)
    endfunction()

    target_compile_definitions(tensor_pool PRIVATE TP_ENABLE_FUZZ=1)

    tp_add_fuzzer(tp_fuzz_control_decode fuzz/fuzz_control_decode.c)
    tp_add_fuzzer(tp_fuzz_control_poller fuzz/fuzz_control_poller.c)
    tp_add_fuzzer(tp_fuzz_discovery_decode fuzz/fuzz_discovery_decode.c)
    tp_add_fuzzer(tp_fuzz_merge_map_decode fuzz/fuzz_merge_map_decode.c)
    tp_add_fuzzer(tp_fuzz_metadata_poller fuzz/fuzz_metadata_poller.c)
    tp_add_fuzzer(tp_fuzz_producer_claim fuzz/fuzz_producer_claim.c)
    tp_add_fuzzer(tp_fuzz_progress_validate fuzz/fuzz_progress_validate.c)
    tp_add_fuzzer(tp_fuzz_qos_poller fuzz/fuzz_qos_poller.c)
    tp_add_fuzzer(tp_fuzz_shm_superblock fuzz/fuzz_shm_superblock.c)
    tp_add_fuzzer(tp_fuzz_slot_tensor_decode fuzz/fuzz_slot_tensor_decode.c)
    tp_add_fuzzer(tp_fuzz_tensor_header fuzz/fuzz_tensor_header.c)
    tp_add_fuzzer(tp_fuzz_tracelink_decode fuzz/fuzz_tracelink_decode.c)
    tp_add_fuzzer(tp_fuzz_slot_decode fuzz/fuzz_slot_decode.c)
    tp_add_fuzzer(tp_fuzz_shm_uri fuzz/fuzz_shm_uri.c)
    tp_add_fuzzer(tp_fuzz_shm_uri_validate fuzz/fuzz_shm_uri_validate.c)

    add_executable(tp_fuzz_seed_gen examples/tp_fuzz_seed_gen.c)
    target_link_libraries(tp_fuzz_seed_gen PRIVATE tensor_pool)
    add_executable(tp_fuzz_seed_gen_discovery examples/tp_fuzz_seed_gen_discovery.c)
    target_link_libraries(tp_fuzz_seed_gen_discovery PRIVATE tensor_pool)
endif ()
