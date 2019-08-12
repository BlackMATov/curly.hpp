/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/curly.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
* Copyright (C) 2019, by Matvey Cherevko (blackmatov@gmail.com)
 * Copyright (C) 2019, by Per Malmberg (https://github.com/PerMalmberg)
 ******************************************************************************/

#include <curly.hpp/response_code.h>

namespace curly_hpp
{
    std::string as_string(response_code code)
    {
        try
        {
            return response_code_to_text.at(code);
        }
        catch (...)
        {
            return "Invalid response code";
        }
    }
}

std::ostream& operator<<(std::ostream& stream, curly_hpp::response_code c)
{
    stream << static_cast<int>(c);
    return stream;
}