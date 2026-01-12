cmake_minimum_required(VERSION 3.24)

project(tensor_pool_c LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TP_ENABLE_SBE_CODEGEN "Enable SBE code generation" ON)

set(AERON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../aeron" CACHE PATH "Path to Aeron source tree")
set(SBE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../simple-binary-encoding" CACHE PATH "Path to simple-binary-encoding source tree")
set(SBE_TOOL_JAR "" CACHE FILEPATH "Path to sbe-tool or sbe-all jar")

if (NOT EXISTS "${AERON_ROOT}/aeron-client/src/main/c/CMakeLists.txt")
    message(FATAL_ERROR "Aeron C client not found at ${AERON_ROOT}/aeron-client/src/main/c")
endif ()

set(AERON_INSTALL_TARGETS OFF CACHE BOOL "Disable Aeron install targets" FORCE)
add_subdirectory("${AERON_ROOT}/aeron-client/src/main/c" "${CMAKE_CURRENT_BINARY_DIR}/aeron-client")

set(TP_PUBLIC_HEADERS
    include/tensor_pool/tp_version.h
)

set(TP_SOURCES
    src/tp_version.c
)

if (TP_ENABLE_SBE_CODEGEN)
    find_package(Java REQUIRED COMPONENTS Runtime)

    if (SBE_TOOL_JAR STREQUAL "")
        file(GLOB SBE_JAR_CANDIDATES
            "${SBE_ROOT}/sbe-all/build/libs/sbe-all-*.jar"
            "${SBE_ROOT}/sbe-tool/build/libs/sbe-tool-*.jar")

        list(LENGTH SBE_JAR_CANDIDATES SBE_JAR_COUNT)
        if (SBE_JAR_COUNT GREATER 0)
            list(GET SBE_JAR_CANDIDATES 0 SBE_TOOL_JAR)
        endif ()
    endif ()

    if (NOT EXISTS "${SBE_TOOL_JAR}")
        message(FATAL_ERROR "SBE tool jar not found. Set -DSBE_TOOL_JAR=/path/to/sbe-all-<ver>.jar")
    endif ()

    set(SBE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(SBE_SCHEMA_DIR "${CMAKE_CURRENT_LIST_DIR}/schemas")
    set(SBE_JAVA_OPTS "--add-opens" "java.base/jdk.internal.misc=ALL-UNNAMED")

    add_custom_command(
        OUTPUT "${SBE_OUTPUT_DIR}/tp_codegen.stamp"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SBE_OUTPUT_DIR}"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir="${SBE_OUTPUT_DIR}"
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -jar "${SBE_TOOL_JAR}"
            "${SBE_SCHEMA_DIR}/wire-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir="${SBE_OUTPUT_DIR}"
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -jar "${SBE_TOOL_JAR}"
            "${SBE_SCHEMA_DIR}/driver-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir="${SBE_OUTPUT_DIR}"
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -jar "${SBE_TOOL_JAR}"
            "${SBE_SCHEMA_DIR}/discovery-schema.xml"
        COMMAND "${Java_JAVA_EXECUTABLE}" ${SBE_JAVA_OPTS}
            -Dsbe.target.language=C
            -Dsbe.output.dir="${SBE_OUTPUT_DIR}"
            -Dsbe.target.namespace=tensor_pool
            -Dsbe.generate.ir=true
            -jar "${SBE_TOOL_JAR}"
            "${SBE_SCHEMA_DIR}/bridge-schema.xml"
        COMMAND "${CMAKE_COMMAND}" -E touch "${SBE_OUTPUT_DIR}/tp_codegen.stamp"
        DEPENDS
            "${SBE_SCHEMA_DIR}/wire-schema.xml"
            "${SBE_SCHEMA_DIR}/driver-schema.xml"
            "${SBE_SCHEMA_DIR}/discovery-schema.xml"
            "${SBE_SCHEMA_DIR}/bridge-schema.xml"
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

add_executable(tensor_pool_tests tests/test_tp_smoke.c)
target_link_libraries(tensor_pool_tests PRIVATE tensor_pool)
add_test(NAME tensor_pool_smoke COMMAND tensor_pool_tests)
