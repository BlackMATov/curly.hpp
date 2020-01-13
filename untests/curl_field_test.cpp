/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include <catch2/catch.hpp>

#include <curly.hpp/curly.hpp>
#include <rapidjson/document.h>

namespace net = curly_hpp;

SCENARIO("Multi part form")
{
    GIVEN("Fields and data")
    {
        WHEN("Posting to a service that returns the posted multi part form data")
        {
            THEN("The data matches")
            {
                net::performer p;

                auto req = net::request_builder(net::http_method::MULTIPART_FORM, "http://httpbin.org/anything");
                req.field("awesome key", "awesome value");
                req.field("awesome key2", "awesome value2");
                req.fields({{"a", "b"}, {"c", "d"}});
                req.request_timeout(std::chrono::milliseconds(2000));
                auto request = req.send();
                auto res = request.take();

                auto data = res.content.as_string_copy();

                rapidjson::Document d;
                REQUIRE_FALSE(d.Parse(data.c_str(), data.size()).HasParseError());

                REQUIRE(d["form"]["awesome key"] == "awesome value");
                REQUIRE(d["form"]["awesome key2"] == "awesome value2");
                REQUIRE(d["form"]["a"] == "b");
                REQUIRE(d["form"]["c"] == "d");
            }
        }
    }
}