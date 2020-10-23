#include <iostream>
#include <sstream>

#include "GameMonitor.h"
#include "TADemoParser.h"

PlayerData::PlayerData() :
is_dead(false),
tick(0u),
armyNumber(0)
{ }

PlayerData::PlayerData(const TADemo::Player &player) :
TADemo::Player(player),
is_dead(false),
tick(0u),
armyNumber(0)
{ }

std::ostream & PlayerData::print(std::ostream &s)
{
    s << "player " << unsigned(this->number) << "(" << this->name << "), tick=" << tick << " is_dead=" << (is_dead ? "yes" : "no");
    return s;
}


GameMonitor::GameMonitor() :
m_gameStarted(false),
m_cheatsEnabled(false),
m_suspiciousStatus(false)
{ }

bool GameMonitor::isGameStarted() const
{
    return m_gameStarted;
}

std::string GameMonitor::getMapName() const
{
    return m_mapName;
}

// returns set of winning players
bool GameMonitor::isGameOver() const
{
    return (!m_gameResult.resultByArmy.empty() || getActivePlayers().empty());
}

const GameResult & GameMonitor::getGameResult() const
{
    return m_gameResult;
}

void GameMonitor::reset()
{
    m_gameStarted = false;
    m_cheatsEnabled = false;
    m_suspiciousStatus = false;
    m_mapName.clear();
    m_lobbyChat.clear();
    m_players.clear();
    m_gameResult.resultByArmy.clear();
    m_gameResult.armyNumbersByPlayerName.clear();
}

std::set<std::string> GameMonitor::getPlayerNames(bool queryIsPlayer, bool queryIsWatcher) const
{
    std::set<std::string> playerNames;
    for (const auto& player : m_players)
    {
        if (player.second.side != TADemo::Side::WATCH && queryIsPlayer || player.second.side == TADemo::Side::WATCH && queryIsWatcher)
        {
            playerNames.insert(player.second.name);
        }
    }
    return playerNames;
}

const PlayerData& GameMonitor::getPlayerData(const std::string& name) const
{
    return m_players.at(getPlayerByName(name));
}

void GameMonitor::handle(const TADemo::Header &header)
{
    m_mapName = header.mapName;
    m_maxUnits = header.maxUnits;
}

void GameMonitor::handle(const TADemo::ExtraSector &es, int n, int ofTotal)
{
    if (es.sectorType == 0x02)
    {
        m_lobbyChat = (const char*)(es.data.data());
    }
}

void GameMonitor::handle(const TADemo::Player &player, int n, int ofTotal)
{
    m_players[player.number] = player;
    updatePlayerArmies();   // can't just allocate a new army number, need to ensure consistency across intances
}

void GameMonitor::handle(const TADemo::PlayerStatusMessage &msg, std::uint32_t dplayid, int n, int ofTotal)
{
    m_players[msg.number].dplayid = dplayid;
}

void GameMonitor::handle(const TADemo::UnitData &unitData)
{ }

void GameMonitor::handle(const TADemo::Packet& packet, const std::vector<TADemo::bytestring>& unpacked, std::size_t n)
{
    if (!m_lobbyChat.empty())
    {
        std::stringstream test(m_lobbyChat);
        std::string line;
        while (std::getline(test, line, char(0x0d)))
        {
            updateAlliances(0, line);
            updatePlayerArmies();
        }
        m_lobbyChat.clear();
    }

    for (const TADemo::bytestring& s : unpacked)
    {
        switch (s[0])
        {
        case 0x05:  // chat
        {
            std::string chat = (const char*)(&s[1]);
            //updateAlliances(packet.sender, chat);
            //updatePlayerArmies();
            break;
        }
        case 0x0c:  // self dies
        {
            std::uint16_t unitid = *(std::uint16_t*)(&s[1]);
            if (unitid % m_maxUnits == 1)
            {
                m_players[packet.sender].print(std::cout) << ", COMMANDER DIED" << std::endl;
                m_players[packet.sender].is_dead = true;
                checkGameResult();
            }
            break;
        }
        case 0x1b:  // reject other
        {
            std::uint32_t dplayid = *(std::uint32_t*)(&s[1]);
            std::uint8_t rejected = getPlayerByDplayId(dplayid);
            m_players[packet.sender].print(std::cout) << ", REJECTED " << m_players[rejected].name << std::endl;
            m_players[rejected].is_dead = true;
            checkGameResult();
            break;
        }
        case 0x20:  // status
        {
            std::string mapname = (const char*)(&s[1]);
            std::uint16_t maxunits = *(std::uint16_t*)(&s[166]);
            std::uint8_t playerside = s[156] & 0x40 ? 2 : s[150];
            bool cheats = (s[157] & 0x20) != 0;
            if (cheats)
            {
                m_cheatsEnabled = true;
            }

            if (mapname != m_mapName)
            {
                std::cerr << "  mapname mismatch.  header=" << m_mapName << ", status packet=" << mapname << std::endl;
                m_suspiciousStatus = true;
            }
            if (maxunits != m_maxUnits)
            {
                std::cerr << "  Maxunits mismatch.  header=" << m_maxUnits << ", status packet=" << maxunits << std::endl;
                m_suspiciousStatus = true;
            }
            break;
        }
        case 0x2c:
        {
            m_gameStarted = true;
            std::uint32_t tick = *(std::uint32_t*)(&s[3]);
            if (tick >= m_players[packet.sender].tick)
            {
                m_players[packet.sender].tick = tick;
            }
            break;
        }
        };
    }
}

std::uint8_t GameMonitor::getPlayerByName(const std::string &name) const
{
    for (const auto &player: m_players)
    {
        if (player.second.name == name)
        {
            return player.second.number;
        }
    }
    return 0u;
}

std::uint8_t GameMonitor::getPlayerByDplayId(std::uint32_t dplayid) const
{
    for (const auto& player : m_players)
    {
        if (player.second.dplayid == dplayid)
        {
            return player.second.number;
        }
    }
    return 0u;
}

void GameMonitor::checkGameResult()
{
    if (!m_gameResult.resultByArmy.empty())
    {
        // the victory was already flagged and the winners determined
        return;
    }

    std::set<std::uint8_t> candidateWinners = getActivePlayers();
    if (candidateWinners.empty())
    {
        updateGameResult(0);    // indicate no winners only losers
    }
    else
    {
        int winningArmyNumber = m_players.at(*candidateWinners.begin()).armyNumber;
        updateGameResult(winningArmyNumber);
    }
}

// return player numbers who have neither died nor are watchers
std::set<std::uint8_t> GameMonitor::getActivePlayers() const
{
    std::set<std::uint8_t> activePlayers;
    for (const auto & player : m_players)
    {
        if (!player.second.is_dead && player.second.side != TADemo::Side::WATCH)
        {
            activePlayers.insert(player.second.number);
        }
    }
    return activePlayers;
}

// determine whether a set of players are all allied (ie are all on one team)
bool GameMonitor::isAllied(const std::set<std::uint8_t> &playerIds) const
{
    for (std::uint8_t m : playerIds)
    {
        for (std::uint8_t n : playerIds)
        {
            if (m != n && m_players.at(m).allies.count(n) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

std::set<std::uint8_t> GameMonitor::getMutualAllies(std::uint8_t playernum) const
{
    std::set<std::uint8_t> mutualAllies;

    const PlayerData &player = m_players.at(playernum);
    for (std::uint8_t otherplayernum: player.allies)
    {
        const PlayerData& other = m_players.at(otherplayernum);
        if (other.allies.count(player.number) > 0)
        {
            mutualAllies.insert(other.number);
        }
    }

    return mutualAllies;
}


// based on chat messages "<player1>  allied with player2".
// not spoofable in-game, but can be spoofed in lobby :(
// unfortunately, without modifying recorder, I can't see any other way to determine alliances
void GameMonitor::updateAlliances(std::uint8_t sender, const std::string &chat)
{
    if (sender == 0u)
    {
        std::size_t senderStart = chat.find_first_of('<');
        std::size_t senderEnd = chat.find_first_of('>');
        if (senderStart != std::string::npos && senderEnd != std::string::npos && senderStart == 0u && senderEnd > 1u)
        {
            std::string senderName = chat.substr(1, senderEnd - 1);
            sender = getPlayerByName(senderName);
        }
        if (sender == 0u)
        {
            return;
        }
    }

    // look for: 
    //     "<name1>  allied with name2"
    //     "<name1>  broke alliance with name2"
    // where name1 must match the sender
    std::size_t lastWordStart = chat.find_last_of(' ');
    if (lastWordStart != std::string::npos && 1 + lastWordStart < chat.size())
    {
        std::string lastWord = chat.substr(1 + lastWordStart);
        std::uint8_t playernumber2 = getPlayerByName(lastWord);
        if (playernumber2 == 0u || playernumber2 == sender)
        {
            return;
        }

        // I assume the double ' ' between name and text is the magic that prevents player spoofing the alliance ...
        std::ostringstream makeAlliancePrototype, breakAlliancePrototype;
        makeAlliancePrototype << "<" << m_players[sender].name << ">  allied with " << lastWord;
        breakAlliancePrototype << "<" << m_players[sender].name << ">  broke alliance with " << lastWord;

        if (chat == makeAlliancePrototype.str())
        {
            m_players[sender].allies.insert(playernumber2);
            std::cout << "detected change in alliance: " << chat << std::endl;
        }
        else if (chat == breakAlliancePrototype.str())
        {
            m_players[sender].allies.erase(playernumber2);
            std::cout << "detected change in alliance: " << chat << std::endl;
        }
    }
}

void GameMonitor::updatePlayerArmies()
{
    std::vector<PlayerData*> sortedPlayers;
    for (auto& player : m_players)
    {
        sortedPlayers.push_back(&player.second);
        player.second.armyNumber = 0;       // reset any previous determination
    }

    // sort by name
    std::sort(sortedPlayers.begin(), sortedPlayers.end(),
        [](const PlayerData* p1, const PlayerData* p2)
        -> bool { return p1->name < p2->name; });

    // assign army numbers consecutively to each mutually allied set
    int armyCount = 0;
    for (PlayerData* sortedPlayer : sortedPlayers)
    {
        if (sortedPlayer->armyNumber == 0)
        {
            ++armyCount;
            std::set<std::uint8_t> mutualAllies = getMutualAllies(sortedPlayer->number);
            mutualAllies.insert(sortedPlayer->number);
            for (std::uint8_t allynumber : mutualAllies)
            {
                if (m_players.at(allynumber).armyNumber == 0)   // this is arbitrary - ie how to deal with someone who's allies aren't allied?
                {
                    m_players.at(allynumber).armyNumber = armyCount;
                }
            }
        }
    }
}

void GameMonitor::updateGameResult(int winningArmyNumber /* or zero */)
{
    m_gameResult.resultByArmy.clear();
    m_gameResult.armyNumbersByPlayerName.clear();

    for (const auto& player : m_players)
    {
        int nArmy = player.second.armyNumber;
        m_gameResult.resultByArmy[nArmy] = (nArmy == winningArmyNumber) ? +1 : -1;
        m_gameResult.armyNumbersByPlayerName[player.second.name] = nArmy;
    }
}
