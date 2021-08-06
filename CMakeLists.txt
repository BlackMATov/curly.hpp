# 3.11 version is required for `FetchContent`
cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if(NOT DEFINED PROJECT_NAME)
    set(BUILD_AS_STANDALONE ON)
else()
    set(BUILD_AS_STANDALONE OFF)
endif()

project(curly.hpp)

option(USE_STATIC_CRT "Use static C runtime library" OFF)
option(USE_SYSTEM_CURL "Build with cURL from system paths" OFF)
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

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        NOMINMAX
        WIN32_LEAN_AND_MEAN>)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wno-covered-switch-default
        -Wno-disabled-macro-expansion
        -Wno-exit-time-destructors
        -Wno-global-constructors
        -Wno-switch-enum>)

target_compile_options(${PROJECT_NAME}
    PUBLIC
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wno-c++98-compat-pedantic
        -Wno-ctad-maybe-unsupported
        -Wno-padded
        -Wno-unknown-warning-option
        -Wno-weak-vtables>)

target_compile_options(${PROJECT_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4>
    PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:
        -Wall -Wextra -Wpedantic>
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Weverything -Wconversion>)

#
# dependencies
#

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)

if(USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${CURL_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
endif()

if(USE_EMBEDDED_CURL)
    include(FetchContent)
    FetchContent_Declare(
        embedded_curl
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-7_78_0)

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
        set(CMAKE_USE_SCHANNEL ON CACHE BOOL "" FORCE)
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
endif()

#
# unit tests
#

if(BUILD_AS_STANDALONE)
    option(BUILD_WITH_UNTESTS "Build with unit tests" ON)
    if(BUILD_WITH_UNTESTS)
        enable_testing()
        add_subdirectory(untests)
    endif()
endif()
