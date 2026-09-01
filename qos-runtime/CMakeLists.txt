cmake_minimum_required(VERSION 3.20)
project(qos_runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(QOS_BUILD_SHARED "Build qos as a shared library (DLL/.so) with the C ABI exported" ON)
option(QOS_BUILD_EXAMPLES "Build example programs" ON)

# ---------------------------------------------------------------------------
# ONNX Runtime. Point ONNXRUNTIME_DIR at an extracted prebuilt package from
# https://github.com/microsoft/onnxruntime/releases (e.g. onnxruntime-win-x64,
# onnxruntime-linux-x64), which contains include/ and lib/.
# ---------------------------------------------------------------------------
set(ONNXRUNTIME_DIR "" CACHE PATH "Path to an extracted ONNX Runtime prebuilt package")
if(NOT ONNXRUNTIME_DIR)
    message(FATAL_ERROR
        "ONNXRUNTIME_DIR is required.\n"
        "Download a prebuilt ONNX Runtime release, extract it, then configure with:\n"
        "  cmake -B build -DONNXRUNTIME_DIR=/path/to/onnxruntime-<platform>")
endif()

find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    PATHS "${ONNXRUNTIME_DIR}/lib"
    NO_DEFAULT_PATH REQUIRED)

if(QOS_BUILD_SHARED)
    add_library(qos SHARED src/qos.cpp src/qos_c.cpp)
    target_compile_definitions(qos PUBLIC QOS_SHARED)
else()
    add_library(qos STATIC src/qos.cpp src/qos_c.cpp)
endif()

target_include_directories(qos
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE "${ONNXRUNTIME_DIR}/include")
target_link_libraries(qos PRIVATE ${ONNXRUNTIME_LIB})

if(QOS_BUILD_EXAMPLES)
    add_executable(qos_minimal examples/minimal.cpp)
    target_link_libraries(qos_minimal PRIVATE qos)

    # Copy the onnxruntime shared library next to the example on Windows.
    if(WIN32)
        file(GLOB ORT_DLLS "${ONNXRUNTIME_DIR}/lib/*.dll")
        foreach(dll ${ORT_DLLS})
            add_custom_command(TARGET qos_minimal POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${dll}" "$<TARGET_FILE_DIR:qos_minimal>")
        endforeach()
    endif()
endif()
