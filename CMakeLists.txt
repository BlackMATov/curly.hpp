cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(curly.hpp
    VERSION "0.0.1"
    DESCRIPTION "Simple cURL C++17 wrapper"
    HOMEPAGE_URL "https://github.com/blackmatov/curly.hpp")

option(USE_STATIC_CRT "Use static C runtime library" OFF)
option(USE_SYSTEM_CURL "Build with cURL from system paths" OFF)
option(USE_EMBEDDED_CURL "Build with embedded cURL library" ON)

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

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS
    ${PROJECT_NAME})

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
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-covered-switch-default
        -Wno-disabled-macro-expansion
        -Wno-documentation-unknown-command
        -Wno-exit-time-destructors
        -Wno-global-constructors
        -Wno-padded
        -Wno-switch-enum
        -Wno-unknown-warning-option
        -Wno-weak-vtables
        >)

if(USE_STATIC_CRT)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
        GIT_TAG curl-7_84_0)

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
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
        set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
        set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
    endif()

    if(APPLE)
        set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
        set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
        set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
    endif()

    if(UNIX AND NOT APPLE)
        set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
        set(CURL_CA_PATH "auto" CACHE STRING "" FORCE)
        set(CURL_CA_BUNDLE "auto" CACHE STRING "" FORCE)
    endif()

    add_subdirectory(${embedded_curl_SOURCE_DIR} ${embedded_curl_BINARY_DIR})
    target_link_libraries(${PROJECT_NAME} PUBLIC libcurl)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_SOURCE_DIR}/include)
endif()

#
# DEVELOPER
#

if(PROJECT_IS_TOP_LEVEL)
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

    enable_testing()
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

    include(EnableASan)
    include(EnableGCov)
    include(EnableUBSan)

    add_subdirectory(untests)
endif()
