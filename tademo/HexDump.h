#pragma once

#include <iostream>

namespace TADemo
{
    void HexDump(const void* _buff, std::size_t size, std::ostream& s);
    void StrHexDump(const void* _buff, std::size_t size, std::ostream& s);
}