#include "GameMonitor2.h"
#include "TAPacketParser.h"

#include <algorithm>
#include <iostream>
#include <sstream>

Player::Player() :
side(TADemo::Side::UNKNOWN)
{ }

PlayerData::PlayerData() :
is_dead(false),
tick(0u),
armyNumber(0),
teamNumber(0)
{ }

PlayerData::PlayerData(const Player &player) :
Player(player),
is_dead(false),
tick(0u),
dplayid(0u),
armyNumber(0),
teamNumber(0)
{ }

std::ostream & PlayerData::print(std::ostream &s) const
{
    s << name << ": id=" << std::hex << dplayid << ", side=" << int(side) << ", is_dead=" << is_dead << ", tick=" << tick << ", nrAllies=" << allies.size();
    return s;
}


GameMonitor2::GameMonitor2(GameEventHandler *gameEventHandler, std::uint32_t gameStartsAfterTickCount, std::uint32_t drawGameTicks) :
m_gameStartsAfterTickCount(gameStartsAfterTickCount),
m_drawGameTicks(drawGameTicks),
m_hostDplayId(0u),
m_localDplayId(0u),
m_gameLaunched(false),
m_gameStarted(false),
m_cheatsEnabled(false),
m_suspiciousStatus(false),
m_gameEventHandler(gameEventHandler)
{ }

void GameMonitor2::setHostPlayerName(const std::string &playerName)
{
    m_hostPlayerName = playerName;
}

void GameMonitor2::setLocalPlayerName(const std::string& playerName)
{
    m_localPlayerName = playerName;
}

std::string GameMonitor2::getHostPlayerName()
{
    return m_hostPlayerName;
}

std::string GameMonitor2::getLocalPlayerName()
{
    return m_localPlayerName;
}

std::uint32_t GameMonitor2::getHostDplayId()
{
    return m_hostDplayId;
}

std::uint32_t GameMonitor2::getLocalPlayerDplayId()
{
    return m_localDplayId;
}

bool GameMonitor2::isGameStarted() const
{
    return m_gameStarted;
}

std::string GameMonitor2::getMapName() const
{
    return m_mapName;
}

// returns set of winning players
bool GameMonitor2::isGameOver() const
{
    return !m_gameResult.resultByArmy.empty();
}

const GameResult & GameMonitor2::getGameResult() const
{
    return m_gameResult;
}

void GameMonitor2::reset()
{
    m_gameStarted = false;
    m_cheatsEnabled = false;
    m_suspiciousStatus = false;
    m_mapName.clear();
    m_players.clear();
    m_gameResult.resultByArmy.clear();
    m_gameResult.armyNumbersByPlayerName.clear();
}

std::set<std::string> GameMonitor2::getPlayerNames(bool queryIsPlayer, bool queryIsWatcher) const
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

const PlayerData& GameMonitor2::getPlayerData(const std::string& name) const
{
    return m_players.at(getPlayerByName(name));
}

const PlayerData& GameMonitor2::getPlayerData(std::uint32_t dplayId) const
{
    return m_players.at(dplayId);
}

void GameMonitor2::onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string &name, TADemo::DPAddress *, TADemo::DPAddress *)
{
    if (!m_gameStarted && !name.empty() && dplayId > 0u)
    {
        auto &player = m_players[dplayId];
        player.name = name;
        player.dplayid = dplayId;

        if (m_hostPlayerName.empty())
        {
            throw std::runtime_error("you need to determine and setHostPlayerName() before GameMonitor receives any packets!");
        }
        else if (name == m_hostPlayerName)
        {
            m_hostDplayId = dplayId;
        }

        if (m_localPlayerName.empty())
        {
            throw std::runtime_error("you need to determine and setLocalPlayerName() before GameMonitor receives any packets!");
        }
        else if (name == m_localPlayerName)
        {
            m_localDplayId = dplayId;
        }

        player.print(std::cout) << " / DPLAY SUPER ENUM" << std::endl;
        updatePlayerArmies();
    }
}

void GameMonitor2::onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string &name, TADemo::DPAddress *, TADemo::DPAddress *)
{
    if (command == 0x0008 && !m_gameStarted && !name.empty() && dplayId>0u) // 0x0008 CREATE PLAYER
    {
        auto &player = m_players[dplayId];
        player.name = name;
        player.dplayid = dplayId;

        if (m_hostPlayerName.empty())
        {
            throw std::runtime_error("you need to determine and setHostName() before GameMonitor receives any packets!");
        }
        else if (name == m_hostPlayerName)
        {
            m_hostDplayId = dplayId;
        }

        player.print(std::cout) << " / DPLAY CREATE" << std::endl;
        updatePlayerArmies();
    }
}

void GameMonitor2::onDplayDeletePlayer(std::uint32_t dplayId)
{
    if (dplayId == 0u || m_players.count(dplayId) == 0)
    {
        return;
    }

    if (!m_gameStarted)
    {
        m_players[dplayId].print(std::cout) << " / DPLAY DELETE " << std::endl;

        m_players.erase(dplayId);
        for (auto &player : m_players)
        {
            player.second.allies.erase(dplayId);
        }
        updatePlayerArmies();
    }
    else
    {
        int winningTeamNumber = checkEndGameCondition();
        if (winningTeamNumber >= 0)
        {
            // better latch the result right now since we may not receive any more game ticks from anyone
            latchEndGameResult(winningTeamNumber);
        }
    }
}

void GameMonitor2::onStatus(std::uint32_t sourceDplayId, const std::string &mapName, std::uint16_t maxUnits, TADemo::Side playerSide, bool cheats)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onStatus] ERROR unexpected dplayid=" << sourceDplayId << std::endl;
    }
    if (!m_gameStarted && !mapName.empty() && maxUnits>0)
    {
        auto &player = m_players[sourceDplayId];
        if (player.side != playerSide)
        {
            player.side = playerSide;
            m_gameEventHandler->onPlayerStatus(player, getMutualAllyNames(player.dplayid));
        }
        if (sourceDplayId == m_hostDplayId)
        {
            if (m_mapName != mapName || m_maxUnits != maxUnits)
            {
                m_mapName = mapName;
                m_maxUnits = maxUnits;
                m_gameEventHandler->onGameSettings(m_mapName, m_maxUnits);
            }
        }
    }
}

void GameMonitor2::onChat(std::uint32_t sourceDplayId, const std::string &chat)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onChat] ERROR unexpected dplayid=" << sourceDplayId << std::endl;
    }

    m_gameEventHandler->onChat(chat, sourceDplayId == m_localDplayId);

    if (!m_gameStarted)
    {
        // server logic requires alliances to be locked at launch, so we don't allow in-game ally
        std::cout << "sourceId=" << sourceDplayId << ", chat=" << chat << std::endl;
        if (updateAlliances(sourceDplayId, chat))
        {
            updatePlayerArmies();
            for (const auto &pair : m_players)
            {
                m_gameEventHandler->onPlayerStatus(pair.second, getMutualAllyNames(pair.first));
            }
        }
    }
}

void GameMonitor2::onUnitDied(std::uint32_t sourceDplayId, std::uint16_t unitId)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onUnitDied] ERROR unexpected dplayid=" << sourceDplayId << std::endl;
    }
    if (unitId % m_maxUnits == 1)
    {
        m_players[sourceDplayId].print(std::cout) << " / COMMANDER DIED" << std::endl;
        m_players[sourceDplayId].is_dead = true;

        int winningTeamNumber = checkEndGameCondition();
        if (winningTeamNumber > 0)
        {
            // should defer the decision since draw is still possible
            latchEndGameTick(getMostRecentGameTick() + m_drawGameTicks);
        }
        else if (winningTeamNumber == 0 && m_gameResult.resultByArmy.empty())
        {
            // can latch the result right now since everyone is dead
            latchEndGameTick(getMostRecentGameTick());
            latchEndGameResult(0);
        }
    }
}

void GameMonitor2::onRejectOther(std::uint32_t sourceDplayId, std::uint32_t rejectedDplayId)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onRejectOther] ERROR unexpected sourceDplayId=" << sourceDplayId << std::endl;
    }
    if (m_players.count(rejectedDplayId) == 0)
    {
        // player left before game started?
        return;
    }
    m_players[sourceDplayId].print(std::cout) << " / REJECTED " << m_players[rejectedDplayId].name << std::endl;
    m_players[rejectedDplayId].is_dead = true;

    int winningTeamNumber = checkEndGameCondition();
    if (winningTeamNumber >= 0)
    {
        // better latch the result right now since we may not receive any more game ticks from anyone
        latchEndGameResult(winningTeamNumber);
    }
}

void GameMonitor2::onGameTick(std::uint32_t sourceDplayId, std::uint32_t tick)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onGameTick] ERROR unexpected sourceDplayId=" << sourceDplayId << std::endl;
    }

    if (!m_gameLaunched && tick > 1)
    {
        m_gameLaunched = true;
        m_gameEventHandler->onGameStarted(tick, false);
    }

    if (!m_gameStarted && tick > m_gameStartsAfterTickCount)
    {
        m_gameStarted = true;
        m_gameEventHandler->onGameStarted(tick, true);
    }

    if (std::int32_t(tick - m_players[sourceDplayId].tick) > 0)
    {
        m_players[sourceDplayId].tick = tick;
    }

    if (m_gameResult.endGameTick > 0u && 
        std::int32_t(getMostRecentGameTick() - m_gameResult.endGameTick) >= 0 &&
        m_gameResult.resultByArmy.empty())
    {
        int winningTeamNumber = checkEndGameCondition();
        if (winningTeamNumber < 0)
        {
            throw std::runtime_error("it should not be possible for a game to become unfinished once it is finished!");
        }
        latchEndGameResult(winningTeamNumber);
    }
}

std::uint32_t GameMonitor2::getPlayerByName(const std::string &name) const
{
    for (const auto &player: m_players)
    {
        if (player.second.name == name)
        {
            return player.second.dplayid;
        }
    }
    return 0u;
}

// return player numbers who have neither died nor are watchers
std::set<std::uint32_t> GameMonitor2::getActivePlayers() const
{
    std::set<std::uint32_t> activePlayers;
    for (const auto & player : m_players)
    {
        if (!player.second.is_dead && player.second.side != TADemo::Side::WATCH)
        {
            activePlayers.insert(player.second.dplayid);
        }
    }
    return activePlayers;
}

std::uint32_t GameMonitor2::getMostRecentGameTick() const
{
    std::uint32_t tick = 0u;
    for (const auto & player : m_players)
    {
        if (player.second.tick > tick)
        {
            tick = player.second.tick;
        }
    }
    return tick;
}


// determine whether a set of players are all allied (ie are all on one team)
bool GameMonitor2::isAllied(const std::set<std::uint32_t> &playerIds) const
{
    for (std::uint32_t m : playerIds)
    {
        for (std::uint32_t n : playerIds)
        {
            if (m != n && m_players.at(m).allies.count(n) == 0)
            {
                return false;
            }
        }
    }
    return true;
}

std::set<std::uint32_t> GameMonitor2::getMutualAllies(std::uint32_t playerId) const
{
    std::set<std::uint32_t> mutualAllies;

    const PlayerData &player = m_players.at(playerId);
    for (std::uint32_t otherId: player.allies)
    {
        const PlayerData& other = m_players.at(otherId);
        if (other.allies.count(player.dplayid) > 0)
        {
            mutualAllies.insert(other.dplayid);
        }
    }

    return mutualAllies;
}


std::set<std::string> GameMonitor2::getMutualAllyNames(std::uint32_t playerId) const
{
    std::set<std::string> mutualAllyNames;
    for (std::uint32_t id : getMutualAllies(playerId))
    {
        mutualAllyNames.insert(m_players.at(id).name);
    }
    return mutualAllyNames;
}


// based on chat messages "<player1>  allied with player2".
// not spoofable in-game, but can be spoofed in lobby :(
// unfortunately, without modifying recorder, I can't see any other way to determine alliances
// anyway alliance has to be created mutually to have any effect ...
bool GameMonitor2::updateAlliances(std::uint32_t sender, const std::string &chat)
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
            return false;
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
        std::uint32_t playernumber2 = getPlayerByName(lastWord);
        if (playernumber2 == 0u || playernumber2 == sender)
        {
            return false;
        }

        // the double ' ' between name and text is the magic that prevents player spoofing the alliance ... ?
        // actually it can be spoofed in lobby, but not in game
        std::ostringstream makeAlliancePrototype, breakAlliancePrototype;
        makeAlliancePrototype << "<" << m_players[sender].name << ">  allied with " << lastWord;
        breakAlliancePrototype << "<" << m_players[sender].name << ">  broke alliance with " << lastWord;

        if (chat == makeAlliancePrototype.str())
        {
            m_players[sender].allies.insert(playernumber2);
            std::cout << "detected change in alliance: " << chat << std::endl;
            return true;
        }
        else if (chat == breakAlliancePrototype.str())
        {
            m_players[sender].allies.erase(playernumber2);
            std::cout << "detected change in alliance: " << chat << std::endl;
            return true;
        }
    }
    return false;
}

void GameMonitor2::updatePlayerArmies()
{
    std::vector<PlayerData*> sortedPlayers;
    for (auto& player : m_players)
    {
        sortedPlayers.push_back(&player.second);
        player.second.armyNumber = 0;       // reset any previous determination
        player.second.teamNumber = 0;       // reset any previous determination
    }

    // sort by name to ensure consistent across all players' instances
    std::sort(sortedPlayers.begin(), sortedPlayers.end(),
        [](const PlayerData* p1, const PlayerData* p2)
        -> bool { return p1->name < p2->name; });

    int teamCount = 1;  // assign team numbers consecutively to each mutually allied set
    int armyCount = 0;  // assign army number by consecutive sortedPlayer
    for (PlayerData* sortedPlayer : sortedPlayers)
    {
        if (sortedPlayer->side == TADemo::Side::WATCH)
        {
            continue;
        }
        sortedPlayer->armyNumber = ++armyCount;
        if (sortedPlayer->teamNumber == 0)
        {
            ++teamCount;
            std::set<std::uint32_t> mutualAllies = getMutualAllies(sortedPlayer->dplayid);
            mutualAllies.insert(sortedPlayer->dplayid);
            for (std::uint32_t allynumber : mutualAllies)
            {
                if (m_players.at(allynumber).teamNumber == 0 &&   // this is arbitrary - ie how to deal with someone who's allies aren't allied?
                    m_players.at(allynumber).side != TADemo::Side::WATCH)
                {
                    m_players.at(allynumber).teamNumber = teamCount;
                }
            }
        }
    }
}

int GameMonitor2::checkEndGameCondition()
{
    if (!m_gameStarted)
    {
        // can't have a result if the game hasn't started
        return -1;
    }

    std::set<std::uint32_t> activePlayers = getActivePlayers();
    if (activePlayers.empty())
    {
        // indicate no winners only losers
        return 0;
    }
    else
    {
        int firstActiveTeam = m_players.at(*activePlayers.begin()).teamNumber;
        for (std::uint32_t id : activePlayers)
        {
            if (m_players.at(id).teamNumber != firstActiveTeam)
            {
                // no winning team as yet
                return -1;
            }
        }
        // active players are all on the same team. ie they win.
        return firstActiveTeam;
    }
}

std::uint32_t GameMonitor2::latchEndGameTick(std::uint32_t endGameTick)
{
    if (m_gameResult.endGameTick == 0)
    {
        if (endGameTick == 0u)
        {
            ++endGameTick;
        }
        m_gameResult.endGameTick = endGameTick;

    }
    return m_gameResult.endGameTick;
}

const GameResult & GameMonitor2::latchEndGameResult(int winningTeamNumber /* or zero */)
{
    if (winningTeamNumber < 0)
    {
        throw std::runtime_error("Please don't try to latch a game result until game result has been decided");
    }

    if (!m_gameResult.resultByArmy.empty())
    {
        return m_gameResult;
    }

    m_gameResult.resultByArmy.clear();
    m_gameResult.armyNumbersByPlayerName.clear();

    for (const auto& player : m_players)
    {
        int nArmy = player.second.armyNumber;
        int nTeam = player.second.teamNumber;
        if (nArmy == 0 || nTeam == 0)
        {
            // either updatePlayerArmies hasn't been called (a bug), or player is a watcher (is normal)
            continue;
        }
        m_gameResult.armyNumbersByPlayerName[player.second.name] = nArmy;
        if (winningTeamNumber > 0)
        {
            m_gameResult.resultByArmy[nArmy] = (nTeam == winningTeamNumber) ? +1 : -1;
        }
        else
        {
            m_gameResult.resultByArmy[nArmy] = 0;
        }
    }
    m_gameEventHandler->onGameEnded(m_gameResult);
    return m_gameResult;
}
