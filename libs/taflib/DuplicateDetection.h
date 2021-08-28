#pragma once

#include <cinttypes>
#include <queue>
#include <set>
#include "nswfl_crc32.h"

namespace taflib
{
    class DuplicateDetection
    {
        CRC32 m_crc32;
        std::vector< std::set<std::uint32_t> > m_seenPacketCRCs; // smaller hash goes in hash map.  full hash goes in set
        std::queue<unsigned> m_seenPacketCRCExpiryQueue;
        static const unsigned EXPIRY_QUEUE_LENGTH = 128u;

    public:
        DuplicateDetection();
        bool isLikelyDuplicate(std::uint32_t sourceId, std::uint32_t destId, const char *data, int len);

    private:
        bool insert(std::uint32_t crc); // returns true if already inserted
        void erase(std::uint32_t crc);
    };
}
