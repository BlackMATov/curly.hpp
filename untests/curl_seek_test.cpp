
/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 ******************************************************************************/

#include <catch2/catch.hpp>

#include <curly.hpp/curly.hpp>
#include <rapidjson/document.h>

namespace net = curly_hpp;

SCENARIO("Seeking upload stream by redirect from http to https")
{
    GIVEN("Data to post")
    {
        WHEN("Posting to a service redirects with from http to https with 308")
        {
            THEN("Redirect is functional")
            {
                net::performer p;

                auto req = net::request_builder(net::http_method::MULTIPART_FORM, "http://httpbin.org/redirect-to");
                req.field("url", "https://httpbin.org/anything");
                req.field("status_code", "308");

                auto request = req.send();
                auto res = request.take();
                const auto data = res.content.as_string_view();

                rapidjson::Document d;
                REQUIRE_FALSE(d.Parse(data.data(), data.size()).HasParseError());

                REQUIRE(d["url"] == "https://httpbin.org/anything");
                REQUIRE(res.http_code() == net::response_code::OK);
            }
        }
    }
}