#include "DuplicateDetection.h"

using namespace TADemo;

bool DuplicateDetection::isLikelyDuplicate(std::uint32_t sourceId, std::uint32_t destId, const char *data, int len)
{
    unsigned crc(-1);
    m_crc32.PartialCRC(&crc, (unsigned char*)&sourceId, sizeof(sourceId));
    m_crc32.PartialCRC(&crc, (unsigned char*)&destId, sizeof(destId));
    m_crc32.PartialCRC(&crc, (unsigned char*)data, len);
    if (m_seenPacketCRCs.count(crc) > 0)
    {
        return true;
    }
    m_seenPacketCRCs.insert(crc);
    m_seenPacketCRCExpiryQueue.push(crc);
    while (m_seenPacketCRCExpiryQueue.size() > EXPIRY_QUEUE_LENGTH)
    {
        unsigned expiredCRC = m_seenPacketCRCExpiryQueue.front();
        m_seenPacketCRCs.erase(expiredCRC);
        m_seenPacketCRCExpiryQueue.pop();
    }
    return false;
}
