#include <cctype>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <fstream>
#include <sstream>

#include "tademo/TADemoParser.h"
#include "tademo/HexDump.h"
#include "GameMonitor2.h"

class TADemoBridge : public TADemo::Parser
{
    GameMonitor2 *m_gameMonitor;

    std::string m_mapName;
    std::uint16_t m_maxUnits;
    std::string m_chat;
    std::map < std::uint8_t, TADemo::Player > m_players;
    std::map < std::uint8_t, std::uint32_t > m_dplayIds;
    std::uint32_t m_tick = 0u;

public:
    TADemoBridge(GameMonitor2 *gameMonitor) :
        m_gameMonitor(gameMonitor)
    { }

    virtual void handle(const TADemo::Header &header)
    {
        m_mapName = header.mapName;
        m_maxUnits = header.maxUnits;
    }

    virtual void handle(const TADemo::Player &player, int n, int ofTotal)
    {
        if (n == 0)
        {
            m_gameMonitor->setHostPlayerName(player.name);
            m_gameMonitor->setLocalPlayerName(player.name);
        }
        m_players[player.number].name = player.name;
        m_players[player.number].side = player.side;
    }

    virtual void handle(const TADemo::ExtraSector &es, int n, int ofTotal)
    {
        if (es.sectorType == 0x02)
        {
            m_chat = (const char*)(es.data.data());
        }
    }

    virtual void handle(const TADemo::PlayerStatusMessage &msg, std::uint32_t dplayid, int n, int ofTotal)
    {
        m_dplayIds[msg.number] = dplayid;
    }

    virtual void handle(const TADemo::UnitData &unitData)
    { }

    virtual void handle(const TADemo::Packet &packet, const std::vector<TADemo::bytestring> &unpaked, std::size_t n)
    {
        if (m_tick == 0u)
        {
            for (auto &p : m_players)
            {
                std::uint32_t dplayId = m_dplayIds[p.first];
                m_gameMonitor->onDplayCreateOrForwardPlayer(0x0008, dplayId, p.second.name, NULL, NULL);
                m_gameMonitor->onStatus(dplayId, m_mapName, m_maxUnits, p.first-1, p.second.side, false, false);
            }

            if (!m_chat.empty())
            {
                std::stringstream chat(m_chat);
                std::string line;
                while (std::getline(chat, line, char(0x0d)))
                {
                    std::size_t pos1 = line.find_first_of('<');
                    std::size_t pos2 = line.find_first_of('>');
                    if (pos1 == 0 && pos2 != std::string::npos && pos1 + 1 < line.size() && pos1 + 1 < pos2)
                    {
                        std::string name = line.substr(pos1 + 1, pos2 - pos1 - 1);
                        for (auto &p : m_players)
                        {
                            if (p.second.name == name)
                            {
                                std::uint8_t slot = p.first;
                                std::uint32_t dplayId = m_dplayIds[slot];
                                m_gameMonitor->onChat(dplayId, line);
                            }
                        }
                    }
                }
            }
            m_tick = 1u;
        }

        std::uint32_t senderDplayId = m_dplayIds[packet.sender];
        for (const TADemo::bytestring& s : unpaked)
        {
            switch (s[0])
            {
            case 0x05:  // chat
            {
                std::string chat = (const char*)(&s[1]);
                std::cout << "tick=" << m_tick << ' ';
                m_gameMonitor->onChat(senderDplayId, chat);
                break;
            }
            case 0x0c:  // self dies
            {
                std::uint16_t unitid = *(std::uint16_t*)(&s[1]);
                m_gameMonitor->onUnitDied(senderDplayId, unitid);
                break;
            }
            case 0x1b:  // reject other
            {
                std::uint32_t rejectedDplayId = *(std::uint32_t*)(&s[1]);
                m_gameMonitor->onRejectOther(senderDplayId, rejectedDplayId);
                break;
            }
            case 0x20: // status
            {
                //std::cout << "status" << std::endl;
                //TADemo::HexDump(s.data(), s.size(), std::cout);
                break;
            }
            case 0x2c: // tick
            {
                m_tick = *(std::uint32_t*)(&s[3]);
                m_gameMonitor->onGameTick(senderDplayId, m_tick);
                break;
            }
            default:
                //std::cout << unsigned(s[0]) << ' ';
                break;
            };
        }
    }
};

class GameEventPrinter : public GameEventHandler
{
public:
    virtual void onGameSettings(const std::string &mapName, std::uint16_t maxUnits, const std::string &, const std::string &)
    {
        std::cout << "[GameEventPrinter::onGameSettings] mapName=" << mapName << ", maxUnits=" << maxUnits << std::endl;
    }

    virtual void onPlayerStatus(const PlayerData &player, const std::set<std::string> & mutualAllies)
    {
        std::cout << "[GameEventPrinter::onPlayerStatus] ";
        player.print(std::cout);
        std::cout << ", allies=";
        for (const std::string &ally : mutualAllies)
        {
            std::cout << ally << ',';
        }
        std::cout << std::endl;
    }

    virtual void onClearSlot(const PlayerData& player)
    {
        std::cout << "[GameEventPrinter::onClearSlot] ";
        player.print(std::cout) << std::endl;
    }

    virtual void onGameStarted(std::uint32_t tick, bool teamsFrozen)
    {
        std::cout << "[GameEventPrinter::onGameStarted] tick=" << tick << ", teamFrozen=" << teamsFrozen << std::endl;
    }

    virtual void onGameEnded(const GameResult &gameResult)
    {
        std::cout << "[GameEventPrinter::onGameEnded]\n";
        gameResult.print(std::cout);
    }

    virtual void onChat(const std::string& msg, bool isLocalPlayerSource)
    {
        std::cout << "[GameEventPrinter::onChat] '" << msg << "'" << (isLocalPlayerSource ? "local player\n" : "remote player\n");
    }
};

int main(int argc, char **argv)
{
    for (int narg = 1; narg < argc; ++narg)
    {
        const char *fn = argv[narg];

        std::cout << "---------------" << std::endl;
        std::cout << fn << std::endl;
        GameEventPrinter printer;
        GameMonitor2 monitor(&printer, 1800, 10);
        TADemoBridge demoparser(&monitor);

        std::ifstream fs(fn, std::ios::in | std::ios::binary);
        while (!fs.eof())
        {
            try
            {
                demoparser.parse(&fs);
            }
            catch (std::runtime_error &e)
            {
                std::cerr << e.what();
            }
        }
    }
}
