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

file(GLOB_RECURSE UNTESTS_SOURCES "*.cpp" "*.hpp")
add_executable(${PROJECT_NAME} ${UNTESTS_SOURCES})
target_link_libraries(${PROJECT_NAME} curly.hpp)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wno-zero-as-null-pointer-constant>)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /WX /W4>
    PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:
        -Werror -Wall -Wextra -Wpedantic>
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Werror -Weverything -Wconversion>)

add_test(${PROJECT_NAME} ${PROJECT_NAME})

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
    GIT_REPOSITORY https://github.com/blackmatov/promise.hpp
    GIT_TAG origin/main)

FetchContent_GetProperties(blackmatov_promise_hpp)
if(NOT blackmatov_promise_hpp_POPULATED)
    FetchContent_Populate(blackmatov_promise_hpp)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${blackmatov_promise_hpp_SOURCE_DIR}/headers)
endif()
