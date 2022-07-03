#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif

#include "doctest.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define STATIC_REQUIRE(...)\
    static_assert(__VA_ARGS__, #__VA_ARGS__);\
    REQUIRE(__VA_ARGS__);

#define STATIC_REQUIRE_FALSE(...)\
    static_assert(!(__VA_ARGS__), "!(" #__VA_ARGS__ ")");\
    REQUIRE(!(__VA_ARGS__));
