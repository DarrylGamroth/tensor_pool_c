cmake_minimum_required(VERSION 3.24)

project(tensor_pool_c LANGUAGES C)

include(CheckIncludeFile)
include(CheckSymbolExists)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TP_ENABLE_SBE_CODEGEN "Enable SBE code generation" ON)

set(AERON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../aeron" CACHE PATH "Path to Aeron source tree")
set(SBE_TOOL_JAR "" CACHE FILEPATH "Path to sbe-tool or sbe-all jar")
set(SBE_TOOL_VERSION "" CACHE STRING "SBE tool version to download (empty = latest)")
set(SBE_TOOL_CACHE_DIR "" CACHE PATH "Cache directory for downloaded sbe-tool jars")

if (NOT EXISTS "${AERON_ROOT}/aeron-client/src/main/c/CMakeLists.txt")
    message(FATAL_ERROR "Aeron C client not found at ${AERON_ROOT}/aeron-client/src/main/c")
endif ()

file(STRINGS "${AERON_ROOT}/version.txt" AERON_VERSION_TXT LIMIT_COUNT 1 REGEX "^[0-9]+(\\.[0-9]+)+")
if (AERON_VERSION_TXT STREQUAL "")
    set(AERON_VERSION_TXT "0.0.0")
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

set(AERON_INSTALL_TARGETS OFF CACHE BOOL "Disable Aeron install targets" FORCE)
add_subdirectory("${AERON_ROOT}/aeron-client/src/main/c" "${CMAKE_CURRENT_BINARY_DIR}/aeron-client")

set(TP_PUBLIC_HEADERS
    include/tensor_pool/tp_aeron.h
    include/tensor_pool/tp.h
    include/tensor_pool/tp_client.h
    include/tensor_pool/tp_client_conductor.h
    include/tensor_pool/tp_consumer.h
    include/tensor_pool/tp_consumer_manager.h
    include/tensor_pool/tp_consumer_registry.h
    include/tensor_pool/tp_context.h
    include/tensor_pool/tp_control.h
    include/tensor_pool/tp_control_adapter.h
    include/tensor_pool/tp_control_poller.h
    include/tensor_pool/tp_discovery_client.h
    include/tensor_pool/tp_driver_client.h
    include/tensor_pool/tp_error.h
    include/tensor_pool/tp_join_barrier.h
    include/tensor_pool/tp_log.h
    include/tensor_pool/tp_merge_map.h
    include/tensor_pool/tp_metadata_poller.h
    include/tensor_pool/tp_producer.h
    include/tensor_pool/tp_progress_poller.h
    include/tensor_pool/tp_qos.h
    include/tensor_pool/tp_seqlock.h
    include/tensor_pool/tp_shm.h
    include/tensor_pool/tp_slot.h
    include/tensor_pool/tp_trace.h
    include/tensor_pool/tp_tracelink.h
    include/tensor_pool/tp_tensor.h
    include/tensor_pool/tp_types.h
    include/tensor_pool/tp_uri.h
    include/tensor_pool/tp_version.h
)

set(TP_SOURCES
    src/tp_aeron.c
    src/tp_client.c
    src/tp_client_conductor.c
    src/tp_clock.c
    src/tp_consumer.c
    src/tp_consumer_manager.c
    src/tp_consumer_registry.c
    src/tp_context.c
    src/tp_control.c
    src/tp_control_adapter.c
    src/tp_control_poller.c
    src/tp_discovery_client.c
    src/tp_driver_client.c
    src/tp_join_barrier.c
    src/tp_log.c
    src/tp_merge_map.c
    src/tp_metadata_poller.c
    src/tp_producer.c
    src/tp_progress_poller.c
    src/tp_qos.c
    src/tp_shm.c
    src/tp_slot.c
    src/tp_trace.c
    src/tp_tracelink.c
    src/tp_tensor.c
    src/tp_uri.c
    src/tp_version.c
)

if (TP_ENABLE_SBE_CODEGEN)
    find_package(Java REQUIRED COMPONENTS Runtime)

    if (SBE_TOOL_JAR STREQUAL "" OR NOT EXISTS "${SBE_TOOL_JAR}")
        include("${CMAKE_CURRENT_LIST_DIR}/cmake/FetchSbeTool.cmake")
        tp_fetch_sbe_tool(SBE_TOOL_JAR)
    endif ()

    if (NOT EXISTS "${SBE_TOOL_JAR}")
        message(FATAL_ERROR "SBE tool jar not found. Set -DSBE_TOOL_JAR=/path/to/sbe-tool-<ver>.jar")
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
    set(SBE_JAVA_MAIN uk.co.real_logic.sbe.SbeTool)

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
            "${SBE_SCHEMA_DIR}/merge-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir=${SBE_OUTPUT_TRACE}
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -cp "${SBE_TOOL_CLASSPATH}" ${SBE_JAVA_MAIN}
            "${SBE_SCHEMA_DIR}/tracelink-schema.xml"
        COMMAND "${CMAKE_COMMAND}" -E touch "${SBE_OUTPUT_DIR}/tp_codegen.stamp"
        DEPENDS
            "${SBE_SCHEMA_DIR}/wire-schema.xml"
            "${SBE_SCHEMA_DIR}/driver-schema.xml"
            "${SBE_SCHEMA_DIR}/discovery-schema.xml"
            "${SBE_SCHEMA_DIR}/bridge-schema.xml"
            "${SBE_SCHEMA_DIR}/merge-schema.xml"
            "${SBE_SCHEMA_DIR}/tracelink-schema.xml"
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
)

target_link_libraries(tensor_pool PUBLIC aeron::aeron_static)

enable_testing()

add_executable(tensor_pool_tests
    tests/test_tp_smoke.c
    tests/test_tp_control.c
    tests/test_tp_consumer_registry.c
    tests/test_tp_driver_client.c
    tests/test_tp_discovery_client.c
    tests/test_tp_join_barrier.c
    tests/test_tp_tracelink.c
    tests/test_tp_client_conductor.c
    tests/test_tp_pollers.c
    tests/test_tp_lease_revoked.c
    tests/test_tp_rollover.c
    tests/test_tp_producer_claim.c
    tests/test_tp_shm_security.c
    tests/test_tp_consumer_apply.c)
target_link_libraries(tensor_pool_tests PRIVATE tensor_pool aeron)
add_test(NAME tensor_pool_smoke COMMAND tensor_pool_tests)

add_executable(tp_shm_inspect tools/tp_shm_inspect.c)
target_link_libraries(tp_shm_inspect PRIVATE tensor_pool)

add_executable(tp_example_producer_driver tools/tp_example_producer_driver.c)
add_executable(tp_example_producer_nodriver tools/tp_example_producer_nodriver.c)
add_executable(tp_example_consumer_driver tools/tp_example_consumer_driver.c)
add_executable(tp_example_consumer_nodriver tools/tp_example_consumer_nodriver.c)
add_executable(tp_example_join_barrier tools/tp_example_join_barrier.c)
target_link_libraries(tp_example_producer_driver PRIVATE tensor_pool)
target_link_libraries(tp_example_producer_nodriver PRIVATE tensor_pool)
target_link_libraries(tp_example_consumer_driver PRIVATE tensor_pool)
target_link_libraries(tp_example_consumer_nodriver PRIVATE tensor_pool)
target_link_libraries(tp_example_join_barrier PRIVATE tensor_pool)

add_executable(tp_control_listen tools/tp_control_listen.c)
target_link_libraries(tp_control_listen PRIVATE tensor_pool)

add_executable(tp_example_bgapi tools/tp_example_bgapi.c)
target_link_libraries(tp_example_bgapi PRIVATE tensor_pool)

add_executable(tp_shm_create tools/tp_shm_create.c)
target_link_libraries(tp_shm_create PRIVATE tensor_pool)
