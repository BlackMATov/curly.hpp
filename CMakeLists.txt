# 3.8 version is required for `cxx_std_17`
cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

if(NOT DEFINED PROJECT_NAME)
    set(BUILD_AS_STANDALONE ON)
endif()

project(curly.hpp)

option(USE_STATIC_CRT "Use static C runtime library" OFF)
option(USE_CONAN_CURL "Build with cURL from Conan" OFF)
option(USE_SYSTEM_CURL "Build with cURL from system paths" OFF)
option(BUILD_PIE "Build Position Independed Code" OFF)
option(USE_EMBEDDED_CURL "Build with embedded cURL library" ON)

#
# linking
#

if(MSVC AND USE_STATIC_CRT)
    foreach(flag CMAKE_C_FLAGS
                 CMAKE_C_FLAGS_DEBUG
                 CMAKE_C_FLAGS_RELEASE
                 CMAKE_C_FLAGS_MINSIZEREL
                 CMAKE_C_FLAGS_RELWITHDEBINFO
                 CMAKE_CXX_FLAGS
                 CMAKE_CXX_FLAGS_DEBUG
                 CMAKE_CXX_FLAGS_RELEASE
                 CMAKE_CXX_FLAGS_MINSIZEREL
                 CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${flag} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag} "${${flag}}")
        endif()
        if(${flag} MATCHES "/MDd")
            string(REGEX REPLACE "/MDd" "/MTd" ${flag} "${${flag}}")
        endif()
    endforeach()
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
# library
#

file(GLOB_RECURSE CURLY_HPP_HEADERS
    headers/curly.hpp/*.hpp
    headers/curly.hpp/*.inl)

file(GLOB_RECURSE CURLY_HPP_SOURCES
    sources/curly.hpp/*.cpp
    sources/curly.hpp/*.hpp
    sources/curly.hpp/*.inl)

add_library(${PROJECT_NAME} STATIC
    ${CURLY_HPP_HEADERS}
    ${CURLY_HPP_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${CURLY_HPP_HEADERS}
    ${CURLY_HPP_SOURCES})

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

#
# dependencies
#

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

if(USE_CONAN_CURL)
    if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
        include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    else()
        message(FATAL_ERROR "The file conanbuildinfo.cmake doesn't exist, run conan install from ${CMAKE_BINARY_DIR} first")
    endif()
    conan_basic_setup(TARGETS NO_OUTPUT_DIRS)
    target_link_libraries(${PROJECT_NAME} PUBLIC CONAN_PKG::libcurl)
endif(USE_CONAN_CURL)

if(UNIX AND NOT APPLE)
    if(BUILD_PIE)
        set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -pie")
    else()
        set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -no-pie")
    endif(BUILD_PIE)
endif()

if(USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC CURL::libcurl)
endif(USE_SYSTEM_CURL)

if(USE_EMBEDDED_CURL)
    include(FetchContent)
    FetchContent_Declare(
        embedded_curl
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-7_67_0)

    FetchContent_GetProperties(embedded_curl)
    if(NOT embedded_curl_POPULATED)
        FetchContent_Populate(embedded_curl)
    endif()

    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(ENABLE_MANUAL OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    if(MSVC AND USE_STATIC_CRT)
        set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)
    endif()

    if(WIN32)
        set(CMAKE_USE_WINSSL ON CACHE BOOL "" FORCE)
        set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
        set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
    endif()

    if(APPLE)
        set(CMAKE_USE_SECTRANSP ON CACHE BOOL "" FORCE)
        set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
        set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
    endif()

    if(UNIX AND NOT APPLE)
        set(CMAKE_USE_OPENSSL ON CACHE BOOL "" FORCE)
        set(CURL_CA_PATH "auto" CACHE STRING "" FORCE)
        set(CURL_CA_BUNDLE "auto" CACHE STRING "" FORCE)
    endif()

    add_subdirectory(${embedded_curl_SOURCE_DIR} ${embedded_curl_BINARY_DIR})
    target_link_libraries(${PROJECT_NAME} PUBLIC libcurl)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_SOURCE_DIR}/include)
endif(USE_EMBEDDED_CURL)

#
# unit tests
#

if(BUILD_AS_STANDALONE)
    option(BUILD_WITH_UNTESTS "Build with unit tests" ON)
    if(BUILD_WITH_UNTESTS)
        enable_testing()
        add_subdirectory(untests)
    endif()
endif(BUILD_AS_STANDALONE)
