# 3.11 version is required for `FetchContent`
cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

project(curly.hpp.untests)

include(FetchContent)

#
# nlohmann/json
#

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json)

FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    FetchContent_Populate(nlohmann_json)
    add_subdirectory(${nlohmann_json_SOURCE_DIR} ${nlohmann_json_BINARY_DIR})
endif()

#
# catchorg/catch2
#

set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    catchorg_catch2
    GIT_REPOSITORY https://github.com/catchorg/catch2)

FetchContent_GetProperties(catchorg_catch2)
if(NOT catchorg_catch2_POPULATED)
    FetchContent_Populate(catchorg_catch2)
    add_subdirectory(${catchorg_catch2_SOURCE_DIR} ${catchorg_catch2_BINARY_DIR})
endif()

#
# coverage
#

option(BUILD_WITH_COVERAGE "Build with coverage" OFF)
if(BUILD_WITH_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    set(COVERAGE_FLAGS "--coverage")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${COVERAGE_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${COVERAGE_FLAGS}")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} ${COVERAGE_FLAGS}")
endif()

#
# executable
#

file(GLOB UNTESTS_SOURCES "*.cpp" "*.hpp")
add_executable(${PROJECT_NAME} ${UNTESTS_SOURCES})
target_link_libraries(${PROJECT_NAME}
    Catch2
    nlohmann_json
    curly.hpp)
target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4>
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wall -Wextra -Wpedantic>)
add_test(${PROJECT_NAME} ${PROJECT_NAME})
