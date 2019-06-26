# 3.8 version is required for `cxx_std_17`
cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

if(NOT DEFINED PROJECT_NAME)
    set(BUILD_AS_STANDALONE ON)
endif()

project(curly.hpp)

option(CURLY_USE_SYSTEM_CURL "Link with cURL from system paths" ON)

add_library(${PROJECT_NAME} STATIC
    headers/curly.hpp/curly.hpp
    sources/curly.cpp)

target_include_directories(${PROJECT_NAME}
    PUBLIC headers)

target_compile_features(${PROJECT_NAME}
    PUBLIC cxx_std_17)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4>
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wall -Wextra -Wpedantic>)

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

if(CURLY_USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${CURL_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
endif()

if(BUILD_AS_STANDALONE)
    option(BUILD_WITH_UNTESTS "Build with unit tests" ON)
    if(BUILD_WITH_UNTESTS)
        enable_testing()
        add_subdirectory(untests)
    endif()
endif()
