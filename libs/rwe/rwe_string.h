#pragma once

#include <string>
#include <vector>

namespace rwe
{
    std::vector<std::string> split(const std::string& str, const std::vector<char>& codePoints);
    std::vector<std::string> split(const std::string& str, char codePoint);
    std::string toUpper(const std::string& str);
    bool startsWith(const std::string& str, const std::string& prefix);
    bool endsWith(const std::string& str, const std::string& end);
    bool endsWithUtf8(const std::string& str, const std::string& end);
}
