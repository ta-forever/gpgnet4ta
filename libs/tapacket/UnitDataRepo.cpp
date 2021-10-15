#include "UnitDataRepo.h"

using namespace tapacket;

UnitDataRepo::UnitDataRepo()
{
}

void UnitDataRepo::clear()
{
    m_unitData.clear();
}

const std::map<UnitDataRepo::SubAndId, bytestring>& UnitDataRepo::get() const
{
    return m_unitData;
}

void UnitDataRepo::add(const bytestring& packetData)
{
    if (packetData.empty() || packetData[0] != std::uint8_t(SubPacketCode::UNIT_DATA_1A))
    {
        return;
    }
    if (packetData.size() != TPacket::getExpectedSubPacketSize(packetData))
    {
        return;
    }

    add(packetData, TUnitData(packetData));
}

void UnitDataRepo::add(const bytestring& pd, const TUnitData& ud)
{
    if (ud.sub == 2 || ud.sub == 3 || ud.sub == 9)
    {
        auto it = m_unitData.find(SubAndId(ud.sub, ud.id));
        if (3 == ud.sub && it != m_unitData.end())
        {
            // hack to be removed once clients are on >= 0.14.3
            bytestring& bs = it->second;
            tapacket::TUnitData oldUd(bs);
            if (oldUd.u.statusAndLimit[0] != 0x0101)
            {
                bs = pd;
            }
        }
        else
        {
            m_unitData[SubAndId(ud.sub, ud.id)] = pd;
        }
    }
    else if (ud.sub == 0)
    {
        clear();
    }
}

void UnitDataRepo::hash(std::function<void(std::uint32_t)> f) const
{
    const std::uint32_t SY_UNIT_ID = 0x92549357;
    for (auto it = m_unitData.begin(); it != m_unitData.end(); ++it)
    {
        TUnitData ud(it->second);
        auto it02 = m_unitData.find(SubAndId(0x02, ud.id));
        if (0x03 == ud.sub &&
            0x0101 == ud.u.statusAndLimit[0] &&
            SY_UNIT_ID != ud.id &&
            it02 != m_unitData.end())
        {
            TUnitData ud02(it02->second);
            f(ud02.id + ud02.u.crc);
        }
    }
}

