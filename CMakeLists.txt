# 3.8 version is required for `cxx_std_17`
cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

if(NOT DEFINED PROJECT_NAME)
    set(BUILD_AS_STANDALONE ON)
endif()

project(curly.hpp)

option(CURLY_USE_SYSTEM_CURL "Build with cURL from system paths" OFF)
option(CURLY_USE_EMBEDDED_CURL "Build with embedded cURL library" ON)

# set(CURLY_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
# set(CURLY_USE_EMBEDDED_CURL ON CACHE BOOL "" FORCE)

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

if(CURLY_USE_EMBEDDED_CURL)
    include(FetchContent)
    FetchContent_Declare(
        embedded_curl
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-7_65_1)

    FetchContent_GetProperties(embedded_curl)
    if(NOT embedded_curl_POPULATED)
        FetchContent_Populate(embedded_curl)
    endif()

    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

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
endif()

if(BUILD_AS_STANDALONE)
    option(BUILD_WITH_UNTESTS "Build with unit tests" ON)
    if(BUILD_WITH_UNTESTS)
        enable_testing()
        add_subdirectory(untests)
    endif()
endif()
