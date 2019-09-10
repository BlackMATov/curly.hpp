# 3.11 version is required for `FetchContent`
cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

project(curly.hpp.untests)

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
target_link_libraries(${PROJECT_NAME} curly.hpp)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4>
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wall -Wextra -Wpedantic>)

# When compiling on Ubuntu 19:04 with gcc 8.3.0, there is an issue with the libraries:
# https://stackoverflow.com/questions/56615841/passing-stdfilesystempath-to-a-function-segfaults/56864842#56864842
# so we link explicitly against stdc++fs
if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "8.3.0")
    target_link_libraries(
            ${PROJECT_NAME}
            $<$<CXX_COMPILER_ID:GNU>:-lstdc++fs>
    )
endif()

add_test(${PROJECT_NAME} ${PROJECT_NAME})

#
# catchorg/catch2
#

include(FetchContent)
FetchContent_Declare(
    catchorg_catch2
    GIT_REPOSITORY https://github.com/catchorg/catch2)

FetchContent_GetProperties(catchorg_catch2)
if(NOT catchorg_catch2_POPULATED)
    FetchContent_Populate(catchorg_catch2)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${catchorg_catch2_SOURCE_DIR}/single_include)
endif()

#
# tencent/rapidjson
#

include(FetchContent)
FetchContent_Declare(
    tencent_rapidjson
    GIT_REPOSITORY https://github.com/tencent/rapidjson)

FetchContent_GetProperties(tencent_rapidjson)
if(NOT tencent_rapidjson_POPULATED)
    FetchContent_Populate(tencent_rapidjson)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${tencent_rapidjson_SOURCE_DIR}/include)
endif()

#
# blackmatov/promise.hpp
#

include(FetchContent)
FetchContent_Declare(
    blackmatov_promise_hpp
    GIT_REPOSITORY https://github.com/blackmatov/promise.hpp)

FetchContent_GetProperties(blackmatov_promise_hpp)
if(NOT blackmatov_promise_hpp_POPULATED)
    FetchContent_Populate(blackmatov_promise_hpp)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${blackmatov_promise_hpp_SOURCE_DIR}/headers)
endif()
