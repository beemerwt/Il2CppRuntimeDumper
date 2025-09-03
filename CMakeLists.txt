cmake_minimum_required(VERSION 3.10)
project(Il2CppRuntimeDumper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output target_include_directories
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
file(GLOB_RECURSE SOURCES
    "${SRC_DIR}/*.c"
    "${SRC_DIR}/*.cpp"
)

# Shared lib build
add_library(Il2CppRuntimeDumper SHARED ${SOURCES})
target_compile_definitions(Il2CppRuntimeDumper PRIVATE RD_DYNAMIC)

# Static lib build
# add_library(Il2CppRuntimeDumper STATIC ${SOURCES})
# target_compile_definitions(Il2CppRuntimeDumper PRIVATE RD_STATIC)

target_include_directories(Il2CppRuntimeDumper
    PRIVATE
        "${SRC_DIR}"
        "${CMAKE_SOURCE_DIR}/include"
)

if (MINGW)
  set_target_properties(Il2CppRuntimeDumper PROPERTIES IMPORT_PREFIX "")
endif()

set_target_properties(Il2CppRuntimeDumper PROPERTIES PREFIX "")