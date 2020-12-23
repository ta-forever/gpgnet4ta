#pragma once

#include <cinttypes>
#include <queue>
#include <set>
#include "nswfl_crc32.h"

namespace TADemo
{
    class DuplicateDetection
    {
        NSWFL::Hashing::CRC32 m_crc32;
        std::set<unsigned> m_seenPacketCRCs;
        std::queue<unsigned> m_seenPacketCRCExpiryQueue;
        static const unsigned EXPIRY_QUEUE_LENGTH = 128u;

    public:
        bool isLikelyDuplicate(std::uint32_t sourceId, std::uint32_t destId, const char *data, int len);
    };
}
