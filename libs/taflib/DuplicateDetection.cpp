#include "DuplicateDetection.h"

using namespace taflib;

DuplicateDetection::DuplicateDetection():
m_seenPacketCRCs(0x100)
{ }

bool DuplicateDetection::isLikelyDuplicate(std::uint32_t sourceId, std::uint32_t destId, const char *data, int len)
{
    unsigned crc(-1);
    m_crc32.PartialCRC(&crc, (unsigned char*)&sourceId, sizeof(sourceId));
    m_crc32.PartialCRC(&crc, (unsigned char*)&destId, sizeof(destId));
    m_crc32.PartialCRC(&crc, (unsigned char*)data, len);

    if (insert(crc))
    {
        return true;
    }

    m_seenPacketCRCExpiryQueue.push(crc);
    while (m_seenPacketCRCExpiryQueue.size() > EXPIRY_QUEUE_LENGTH)
    {
        unsigned expiredCRC = m_seenPacketCRCExpiryQueue.front();
        erase(expiredCRC);
        m_seenPacketCRCExpiryQueue.pop();
    }
    return false;
}

bool DuplicateDetection::insert(std::uint32_t crc)
{
    const std::uint8_t hash = crc & 0xff;
    std::set<std::uint32_t> & crcSet = m_seenPacketCRCs[hash];
    bool found = crcSet.count(crc) > 0u;
    if (!found)
    {
        crcSet.insert(crc);
    }
    return found;
}

void DuplicateDetection::erase(std::uint32_t crc)
{
    const std::uint8_t hash = crc & 0xff;
    std::set<std::uint32_t> & crcSet = m_seenPacketCRCs[hash];
    crcSet.erase(crc);
}
