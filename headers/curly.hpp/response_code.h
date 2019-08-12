/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 * Copyright (C) 2019, by Per Malmberg (https://github.com/PerMalmberg)
 ******************************************************************************/

#pragma once
#include <ostream>
#include <unordered_map>

namespace curly_hpp
{
    enum class response_code
    {
        // Informational = 1××
        Continue = 100,
        SwitchingProtocols = 101,
        Processing = 102,
        
        //_Success = 2××
        OK = 200,
        Created = 201,
        Accepted = 202,
        Non_authoritativeInformation = 203,
        No_Content = 204,
        Reset_Content = 205,
        Partial_Content = 206,
        Multi_Status = 207,
        Already_Reported = 208,
        IM_Used = 226,
        
        //_Redirection = 3××
        Multiple_Choices = 300,
        Moved_Permanently = 301,
        Found = 302,
        See_Other = 303,
        Not_Modified = 304,
        Use_Proxy = 305,
        Temporary_Redirect = 307,
        Permanent_Redirect = 308,
        
        //_Client_Error = 4××
        Bad_Request = 400,
        Unauthorized = 401,
        Payment_Required = 402,
        Forbidden = 403,
        Not_Found = 404,
        Method_Not_Allowed = 405,
        Not_Acceptable = 406,
        Proxy_Authentication_Required = 407,
        Request_Timeout = 408,
        Conflict = 409,
        Gone = 410,
        Length_Required = 411,
        Precondition_Failed = 412,
        Payload_Too_Large = 413,
        Request_URI_Too_Long = 414,
        Unsupported_Media_Type = 415,
        Requested_Range_Not_Satisfiable = 416,
        Expectation_Failed = 417,
        Im_a_teapot = 418,
        Misdirected_Request = 421,
        Unprocessable_Entity = 422,
        Locked = 423,
        Failed_Dependency = 424,
        Upgrade_Required = 426,
        Precondition_Required = 428,
        Too_Many_Requests = 429,
        Request_Header_Fields_Too_Large = 431,
        Connection_Closed_Without_Response = 444,
        Unavailable_For_Legal_Reasons = 451,
        Client_Closed_Request = 499,
        
        //_Server_Error = 5××
        Internal_Server_Error = 500,
        Not_Implemented = 501,
        Bad_Gateway = 502,
        Service_Unavailable = 503,
        Gateway_Timeout = 504,
        HTTP_Version_Not_Supported = 505,
        Variant_Also_Negotiates = 506,
        Insufficient_Storage = 507,
        Loop_Detected = 508,
        Not_Extended = 510,
        Network_Authentication_Required = 511,
        Network_Connect_Timeout_Error = 599
    };

    const std::unordered_map<response_code, const char*> response_code_to_text = {
            {response_code::Continue,                           "Continue"},
            {response_code::SwitchingProtocols,                 "Switching Protocols"},
            {response_code::Processing,                         "Processing"},
            {response_code::OK,                                 "OK"},
            {response_code::Created,                            "Created"},
            {response_code::Accepted,                           "Accepted"},
            {response_code::Non_authoritativeInformation,       "Non-Authoritative Information"},
            {response_code::No_Content,                         "No Content"},
            {response_code::Reset_Content,                      "Reset Content"},
            {response_code::Partial_Content,                    "Partial Content"},
            {response_code::Multi_Status,                       "Multi Status"},
            {response_code::Already_Reported,                   "Already Reported"},
            {response_code::IM_Used,                            "IM Used"},
            {response_code::Multiple_Choices,                   "Multiple Choices"},
            {response_code::Moved_Permanently,                  "Moved Permanently"},
            {response_code::Found,                              "Found"},
            {response_code::See_Other,                          "See Other"},
            {response_code::Not_Modified,                       "Not Modified"},
            {response_code::Use_Proxy,                          "Use Proxy"},
            {response_code::Temporary_Redirect,                 "Temporary Redirect"},
            {response_code::Permanent_Redirect,                 "Permanent Redirect"},
            {response_code::Bad_Request,                        "Bad Request"},
            {response_code::Unauthorized,                       "Unauthorized"},
            {response_code::Payment_Required,                   "Payment Required"},
            {response_code::Forbidden,                          "Forbidden"},
            {response_code::Not_Found,                          "Not Found"},
            {response_code::Method_Not_Allowed,                 "Method Not Allowed"},
            {response_code::Not_Acceptable,                     "Not Acceptable"},
            {response_code::Proxy_Authentication_Required,      "Proxy Authentication Required"},
            {response_code::Request_Timeout,                    "Request Timeout"},
            {response_code::Conflict,                           "Conflict"},
            {response_code::Gone,                               "Gone"},
            {response_code::Length_Required,                    "Length Required"},
            {response_code::Precondition_Failed,                "Precondition Failed"},
            {response_code::Payload_Too_Large,                  "Payload Too Large"},
            {response_code::Request_URI_Too_Long,               "Request URI Too Long"},
            {response_code::Unsupported_Media_Type,             "Unsupported Media Type"},
            {response_code::Requested_Range_Not_Satisfiable,    "Requested Range Not Satisfiable"},
            {response_code::Expectation_Failed,                 "Expectation Failed"},
            {response_code::Im_a_teapot,                        "I'm a teapot"},
            {response_code::Misdirected_Request,                "Misdirected Request"},
            {response_code::Unprocessable_Entity,               "Unprocessable Entity"},
            {response_code::Locked,                             "Locked"},
            {response_code::Failed_Dependency,                  "Failed Dependency"},
            {response_code::Upgrade_Required,                   "Upgrade Required"},
            {response_code::Precondition_Required,              "Precondition Required"},
            {response_code::Too_Many_Requests,                  "Too Many Requests"},
            {response_code::Request_Header_Fields_Too_Large,    "Request Header Fields Too Large"},
            {response_code::Connection_Closed_Without_Response, "Connection Closed Without Response"},
            {response_code::Unavailable_For_Legal_Reasons,      "Unavailable For Legal Reasons"},
            {response_code::Client_Closed_Request,              "Client Closed Request"},
            {response_code::Internal_Server_Error,              "Internal Server Error"},
            {response_code::Not_Implemented,                    "Not Implemented"},
            {response_code::Bad_Gateway,                        "Bad Gateway"},
            {response_code::Service_Unavailable,                "Service Unavailable"},
            {response_code::Gateway_Timeout,                    "Gateway Timeout"},
            {response_code::HTTP_Version_Not_Supported,         "HTTP Version Not Supported"},
            {response_code::Variant_Also_Negotiates,            "Variant Also Negotiates"},
            {response_code::Insufficient_Storage,               "Insufficient Storage"},
            {response_code::Loop_Detected,                      "Loop Detected"},
            {response_code::Not_Extended,                       "Not Extended"},
            {response_code::Network_Authentication_Required,    "Network Authentication Required"},
            {response_code::Network_Connect_Timeout_Error,      "Network Connect Timeout Error"}
    };

    std::string as_string(response_code code);
}

std::ostream& operator<<(std::ostream& stream, curly_hpp::response_code c);