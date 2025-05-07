#include "rwe_string.h"

#include <algorithm>
#include <cctype>

namespace rwe
{
    template <typename It, typename CIt>
    std::vector<std::string> splitInternal(It begin, It end, CIt codePointsBegin, CIt codePointsEnd)
    {
        std::vector<std::string> v;

        while (true)
        {
            auto it = std::find_first_of(begin, end, codePointsBegin, codePointsEnd);
            v.emplace_back(begin, it);
            if (it == end)
            {
                break;
            }

            begin = ++it;
        }

        return v;
    };

    template <typename It, typename Container>
    std::vector<std::string> splitInternal(It begin, It end, const Container& codePoints)
    {
        return splitInternal(begin, end, codePoints.begin(), codePoints.end());
    };

    std::vector<std::string> split(const std::string& str, const std::vector<char>& codePoints)
    {

        auto begin = str.begin();
        auto end = str.end();

        return splitInternal(begin, end, codePoints);
    }

    std::vector<std::string> split(const std::string& str, char codePoint)
    {
        std::vector<char> v{codePoint};
        return split(str, v);
    }

    std::string toUpper(const std::string& str)
    {
        std::string copy(str);
        std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char c) { return std::toupper(c); });
        return copy;
    }

    bool endsWith(const std::string& str, const std::string& end)
    {
        if (str.size() < end.size())
        {
            return false;
        }

        auto it = end.rbegin();
        auto endIt = end.rend();

        auto sIt = str.rbegin();

        while (it != endIt)
        {
            if (*(it++) != *(sIt++))
            {
                return false;
            }
        }

        return true;
    }

    bool startsWith(const std::string& str, const std::string& prefix)
    {
        if (str.size() < prefix.size())
        {
            return false;
        }

        auto it = prefix.begin();
        auto endIt = prefix.end();

        auto sIt = str.begin();

        while (it != endIt)
        {
            if (*(it++) != *(sIt++))
            {
                return false;
            }
        }

        return true;
    }
}
