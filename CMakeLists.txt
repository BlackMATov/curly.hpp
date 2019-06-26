# 3.8 version is required for `cxx_std_17`
cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

if(NOT DEFINED PROJECT_NAME)
    set(BUILD_AS_STANDALONE ON)
endif()

project(curly.hpp)

find_package(Threads REQUIRED)

add_library(${PROJECT_NAME} STATIC
    headers/curly.hpp/curly.hpp
    sources/curly.cpp)

target_include_directories(${PROJECT_NAME}
    PUBLIC headers)

target_compile_features(${PROJECT_NAME}
    PUBLIC cxx_std_17)

target_link_libraries(${PROJECT_NAME}
    PUBLIC curl
    PUBLIC Threads::Threads)

if(BUILD_AS_STANDALONE)
    option(BUILD_WITH_UNTESTS "Build with unit tests" ON)
    if(BUILD_WITH_UNTESTS)
        enable_testing()
        add_subdirectory(untests)
    endif()
endif()
