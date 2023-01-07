project(curly.hpp.untests)

file(GLOB_RECURSE UNTESTS_SOURCES "*.cpp" "*.hpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${UNTESTS_SOURCES})

add_executable(${PROJECT_NAME} ${UNTESTS_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE curly.hpp::curly.hpp)

#
# setup defines
#

function(setup_defines_for_target TARGET)
    target_compile_definitions(${TARGET} PRIVATE
        DOCTEST_CONFIG_INCLUDE_TYPE_TRAITS
        DOCTEST_CONFIG_USE_STD_HEADERS)
endfunction()

setup_defines_for_target(${PROJECT_NAME})

#
# setup libraries
#

function(setup_libraries_for_target TARGET)
    target_link_libraries(${TARGET} PRIVATE doctest::doctest_with_main)

    if(${BUILD_WITH_COVERAGE})
        target_link_libraries(${TARGET} PRIVATE curly.hpp::enable_gcov)
    endif()

    if(${BUILD_WITH_SANITIZERS})
        target_link_libraries(${TARGET} PRIVATE curly.hpp::enable_asan curly.hpp::enable_ubsan)
    endif()
endfunction()

setup_libraries_for_target(${PROJECT_NAME})

#
# setup warnings
#

function(setup_warnings_for_target TARGET)
    target_compile_options(${TARGET}
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:
            /WX /W4>
        PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:
            -Werror -Wall -Wextra -Wpedantic>
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
            -Werror -Weverything -Wconversion
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
            -Wno-ctad-maybe-unsupported
            -Wno-padded
            -Wno-unknown-warning-option
            -Wno-weak-vtables
            -Wno-zero-as-null-pointer-constant
            >)
endfunction()

setup_warnings_for_target(${PROJECT_NAME})

#
# add tests
#

add_test(${PROJECT_NAME} ${PROJECT_NAME})

#
# doctest/doctest
#

include(FetchContent)
FetchContent_Declare(
    doctest_doctest
    GIT_REPOSITORY https://github.com/doctest/doctest)

FetchContent_GetProperties(doctest_doctest)
if(NOT doctest_doctest_POPULATED)
    FetchContent_Populate(doctest_doctest)
    add_subdirectory(${doctest_doctest_SOURCE_DIR} ${doctest_doctest_BINARY_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE doctest::doctest_with_main)
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
    GIT_REPOSITORY https://github.com/blackmatov/promise.hpp
    GIT_TAG origin/main)

FetchContent_GetProperties(blackmatov_promise_hpp)
if(NOT blackmatov_promise_hpp_POPULATED)
    FetchContent_Populate(blackmatov_promise_hpp)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${blackmatov_promise_hpp_SOURCE_DIR}/headers)
endif()
