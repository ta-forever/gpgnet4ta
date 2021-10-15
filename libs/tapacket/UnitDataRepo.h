#pragma once

#include "TPacket.h"

#include <cinttypes>
#include <functional>
#include <map>

namespace tapacket
{

    class UnitDataRepo
    {
    public:
        typedef std::pair <std::uint8_t, std::uint32_t> SubAndId;

        UnitDataRepo();
        void clear();
        void add(const bytestring& packetData);

        const std::map<SubAndId, bytestring>& get() const;
        void hash(std::function<void(std::uint32_t)> f) const;

    private:
        std::map<SubAndId, bytestring> m_unitData;      // keyed by sub,id

        void add(const bytestring& pd, const TUnitData& unitData);
    };

}