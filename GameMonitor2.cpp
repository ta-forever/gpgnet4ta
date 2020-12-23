#include "GameMonitor2.h"
#include "TAPacketParser.h"

#include <algorithm>
#include <iostream>
#include <sstream>

Player::Player() :
side(TADemo::Side::UNKNOWN)
{ }

PlayerData::PlayerData() :
is_AI(false),
is_dead(false),
tick(0u),
armyNumber(0),
teamNumber(0)
{ }

PlayerData::PlayerData(const Player &player) :
Player(player),
is_AI(false),
is_dead(false),
tick(0u),
dplayid(0u),
armyNumber(0),
teamNumber(0)
{ }

std::ostream & PlayerData::print(std::ostream &s) const
{
    s << "'" << name << "': id=" << std::hex << dplayid
        << ", slot=" << slotNumber << ", side=" << int(side) << ", is_ai=" << is_AI << ", is_dead=" << is_dead << ", tick=" << tick << ", nrAllies=" << allies.size();
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
    return m_gameResult.status != GameResult::Status::NOT_READY;
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
    m_gameResult = GameResult();
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

        if (m_localPlayerName.empty())
        {
            throw std::runtime_error("you need to determine and setLocalPlayerName() before GameMonitor receives any packets!");
        }
        else if (name == m_localPlayerName)
        {
            m_localDplayId = dplayId;
        }

        updatePlayerArmies();
    }
}

void GameMonitor2::onDplayDeletePlayer(std::uint32_t dplayId)
{
    if (dplayId == 0u || m_players.count(dplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onDplayDeletePlayer] ERROR unexpected dplayid=" << dplayId << std::endl;
        return;
    }

    if (!m_gameStarted)
    {
        {
            PlayerData& player = m_players.at(dplayId);
            if (!m_gameLaunched)
            {
                m_gameEventHandler->onClearSlot(player);
            }
        }

        m_players.erase(dplayId);
        for (auto &player : m_players)
        {
            player.second.allies.erase(dplayId);
        }
        updatePlayerArmies();
    }
    else
    {
        int winningTeamNumber;
        if (checkEndGameCondition(winningTeamNumber))
        {
            // better latch the result right now since we may not receive any more game ticks from anyone
            latchEndGameResult(winningTeamNumber);
        }
    }
}

void GameMonitor2::onStatus(
    std::uint32_t sourceDplayId, const std::string &mapName, std::uint16_t maxUnits,
    unsigned playerSlotNumber, TADemo::Side playerSide, bool isAI, bool cheats)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onStatus] ERROR unexpected dplayid=" << sourceDplayId << std::endl;
        return;
    }
    if (!m_gameStarted)
    {
        auto &player = m_players[sourceDplayId];
        player.is_AI = isAI;
        player.slotNumber = playerSlotNumber;
        if (player.side != playerSide)
        {
            player.side = playerSide;
            if (m_gameEventHandler) m_gameEventHandler->onPlayerStatus(player, getMutualAllyNames(player.dplayid, m_players));
        }
        if (sourceDplayId == m_hostDplayId && !mapName.empty() && maxUnits > 0)
        {
            if (m_mapName != mapName)
            {
                m_mapName = mapName;
                m_maxUnits = maxUnits;
                if (m_gameEventHandler) m_gameEventHandler->onGameSettings(m_mapName, m_maxUnits);
            }
        }
    }
}

void GameMonitor2::onChat(std::uint32_t sourceDplayId, const std::string &chat)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onChat] ERROR unexpected dplayid=" << sourceDplayId << std::endl;
        return;
    }

    if (m_gameEventHandler) m_gameEventHandler->onChat(chat, sourceDplayId == m_localDplayId);

    if (updateAlliances(sourceDplayId, chat))
    {
        updatePlayerArmies();
        if (!m_gameStarted)
        {
            for (const auto& pair : m_players)
            {
                if (m_gameEventHandler) m_gameEventHandler->onPlayerStatus(pair.second, getMutualAllyNames(pair.first, m_players));
            }
        }

        // teams are frozen on game start, but players can still cause a mutual draw by allying after start
        int winningTeamNumber;
        if (checkEndGameCondition(winningTeamNumber) && winningTeamNumber < 0)
        {
            // we can latch a mutual draw straight away
            latchEndGameResult(winningTeamNumber);
        }
    }
}

void GameMonitor2::onUnitDied(std::uint32_t sourceDplayId, std::uint16_t unitId)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onUnitDied] ERROR unexpected dplayid=" << sourceDplayId << std::endl;
        return;
    }
    if (unitId % m_maxUnits == 1)
    {
        m_players[sourceDplayId].is_dead = true;

        int winningTeamNumber;;
        if (checkEndGameCondition(winningTeamNumber))
        {
            if (winningTeamNumber > 0)
            {
                // should defer the decision since draw is still possible
                latchEndGameTick(getMostRecentGameTick() + m_drawGameTicks);
            }
            else
            {
                // can latch the result right now since its draw (most likely forced but could possibly be mutual)
                latchEndGameResult(winningTeamNumber);
            }
        }
    }
}

void GameMonitor2::onRejectOther(std::uint32_t sourceDplayId, std::uint32_t rejectedDplayId)
{
    if (m_players.count(sourceDplayId) == 0)
    {
        std::cerr << "[GameMonitor2::onRejectOther] ERROR unexpected sourceDplayId=" << sourceDplayId << std::endl;
        return;
    }
    if (m_players.count(rejectedDplayId) == 0)
    {
        // player left before game started?
        return;
    }

    m_players[rejectedDplayId].is_dead = true;

    int winningTeamNumber;
    if (checkEndGameCondition(winningTeamNumber))
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
        return;
    }

    if (!m_gameLaunched && tick > m_gameStartsAfterTickCount/20)
    {
        m_gameLaunched = true;
        if (m_gameEventHandler) m_gameEventHandler->onGameStarted(tick, false);
    }

    if (!m_gameStarted && tick > m_gameStartsAfterTickCount)
    {
        m_gameStarted = true;
        if (m_gameEventHandler) m_gameEventHandler->onGameStarted(tick, true);

        // server logic requires alliances to be locked at launch so we require teams to be set before game starts (tick > m_gameStartsAfterTickCount)
        // so here we grab the player status (in particular the alliances) at time of game start
        m_frozenPlayers = m_players;

        int winningTeamNumber;
        if (checkEndGameCondition(winningTeamNumber))
        {
            // if game ended before start we'll assume its a mutually agreed draw
            latchEndGameResult(-1);
        }
    }

    if (std::int32_t(tick - m_players[sourceDplayId].tick) > 0)
    {
        m_players[sourceDplayId].tick = tick;
    }

    if (m_gameResult.endGameTick > 0u && 
        std::int32_t(getMostRecentGameTick() - m_gameResult.endGameTick) >= 0)
    {
        int winningTeamNumber;
        if (!checkEndGameCondition(winningTeamNumber))
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

std::set<std::uint32_t> GameMonitor2::getMutualAllies(std::uint32_t playerId, const std::map<std::uint32_t, PlayerData>& playerData) const
{
    std::set<std::uint32_t> mutualAllies;

    const PlayerData &player = playerData.at(playerId);
    for (std::uint32_t otherId: player.allies)
    {
        const PlayerData& other = playerData.at(otherId);
        if (other.allies.count(player.dplayid) > 0)
        {
            mutualAllies.insert(other.dplayid);
        }
    }

    return mutualAllies;
}


std::set<std::string> GameMonitor2::getMutualAllyNames(std::uint32_t playerId, const std::map<std::uint32_t, PlayerData>& playerData) const
{
    std::set<std::string> mutualAllyNames;
    for (std::uint32_t id : getMutualAllies(playerId, playerData))
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
            return true;
        }
        else if (chat == breakAlliancePrototype.str())
        {
            m_players[sender].allies.erase(playernumber2);
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

    // sort by slotNumber to ensure consistent across all players' instances
    std::sort(sortedPlayers.begin(), sortedPlayers.end(),
        [](const PlayerData* p1, const PlayerData* p2)
        -> bool { return p1->slotNumber < p2->slotNumber; });

    int teamCount = 1;  // assign team numbers consecutively to each mutually allied set
    int armyCount = 0;  // assign army number by consecutive sortedPlayer
    for (PlayerData* sortedPlayer : sortedPlayers)
    {
        if (sortedPlayer->side == TADemo::Side::WATCH || sortedPlayer->is_dead)
        {
            continue;
        }
        sortedPlayer->armyNumber = ++armyCount;
        if (sortedPlayer->teamNumber == 0)
        {
            ++teamCount;
            std::set<std::uint32_t> mutualAllies = getMutualAllies(sortedPlayer->dplayid, m_players);
            mutualAllies.insert(sortedPlayer->dplayid);
            for (std::uint32_t allynumber : mutualAllies)
            {
                if (m_players.at(allynumber).teamNumber == 0 &&   // this is arbitrary - ie how to deal with someone who's allies aren't allied?
                    m_players.at(allynumber).side != TADemo::Side::WATCH &&
                    !m_players.at(allynumber).is_dead)
                {
                    m_players.at(allynumber).teamNumber = teamCount;
                }
            }
        }
    }
}

bool GameMonitor2::checkEndGameCondition(int &winningTeamNumber)
{
    if (!m_gameStarted)
    {
        // game hasn't started so there can't be any result
        return false;
    }

    std::set<std::uint32_t> activePlayers = getActivePlayers();
    if (activePlayers.empty())
    {
        // game over with forced draw (everyone is dead)
        winningTeamNumber = 0;
        return true;
    }

    int lastTeamStanding;
    if (isPlayersAllAllied(activePlayers, m_frozenPlayers, lastTeamStanding))
    {
        // game over with one team prevailing
        winningTeamNumber = lastTeamStanding;
        return true;
    }

    if (isPlayersAllAllied(activePlayers, m_players, lastTeamStanding))
    {
        // game over with mutually agreed draw
        winningTeamNumber = -1;
        return true;
    }

    // no result as yet
    return false;
}

bool GameMonitor2::isPlayersAllAllied(const std::set<std::uint32_t> & playerIds, const std::map<std::uint32_t, PlayerData>& playerData, int &teamNumber)
{
    teamNumber = playerData.at(*playerIds.begin()).teamNumber;
    for (std::uint32_t id : playerIds)
    {
        if (playerData.at(id).teamNumber != teamNumber)
        {
            // not allied
            return false;
        }
    }
    // yes and they are all on firstTeam
    return true;
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

const GameResult & GameMonitor2::latchEndGameResult(int winningTeamNumber /* or zero for forced draw, -1 for mutual draw */)
{
    if (m_gameResult.status != GameResult::Status::NOT_READY)
    {
        return m_gameResult;
    }

    m_gameResult.results.clear();

    if (winningTeamNumber < 0)
    {
        m_gameResult.status = GameResult::Status::VOID_RESULT;
        return m_gameResult;
    }

    m_gameResult.status = GameResult::Status::VOID_RESULT;
    for (const auto& player : m_frozenPlayers)
    {
        int nArmy = player.second.armyNumber;
        int nTeam = player.second.teamNumber;
        if (nArmy == 0 || nTeam == 0 || player.second.side == TADemo::Side::WATCH)
        {
            // either updatePlayerArmies hasn't been called (a bug), or player is a watcher (is normal)
            continue;
        }

        GameResult::ArmyResult res;
        res.army = nArmy;
        res.slot = player.second.slotNumber;
        res.name = player.second.name;
        res.team = nTeam;
        if (winningTeamNumber > 0)
        {
            res.score = (nTeam == winningTeamNumber) ? +1 : -1;
        }
        else
        {
            res.score = 0;
        }
        m_gameResult.results.push_back(res);
        m_gameResult.status = GameResult::Status::READY_RESULT;
    }
    if (m_gameEventHandler) m_gameEventHandler->onGameEnded(m_gameResult);
    return m_gameResult;
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)
#define TESTASSERT(x) if (!(x)) { throw std::runtime_error(AT); }

static void TestGameResult(const std::vector<GameResult::ArmyResult>& results, int slot, int expectedScore)
{
    for (const GameResult::ArmyResult& res : results)
    {
        if (res.slot == slot)
        {
            TESTASSERT(res.score == expectedScore);
            return;
        }
    }
    TESTASSERT(false);
}

void GameMonitor2::test()
{
    {
        // normal 1v1 player1 wins
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(1, 1);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        {
            const auto& gr = gm.getGameResult();
            TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
            TESTASSERT(gr.results.size() == 2);
            TestGameResult(gr.results, 1, -1);
            TestGameResult(gr.results, 2, +1);
        }

        {
            // no change despite player2 game shutting down
            gm.onDplayDeletePlayer(2);
            const auto& gr = gm.getGameResult();
            TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
            TESTASSERT(gr.results.size() == 2);
            TestGameResult(gr.results, 1, -1);
            TestGameResult(gr.results, 2, +1);
        }
    }

    {
        // normal 1v1 with host as watcher player3 wins
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::WATCH, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(2, 1501);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 2);
        TestGameResult(gr.results, 2, -1);
        TestGameResult(gr.results, 3, +1);
    }

    {
        // normal 1v1 with local non-host player as watcher player3 wins
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::WATCH, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(1, 1);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 2);
        TestGameResult(gr.results, 1, -1);
        TestGameResult(gr.results, 3, +1);
    }

    {
        // normal 1v1 with remote non-host player as watcher player1 wins
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::WATCH, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(2, 1501);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 2);
        TestGameResult(gr.results, 1, +1);
        TestGameResult(gr.results, 2, -1);
    }

    {
        // normal 1v1 with local host player as watcher player3 wins
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player1");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::WATCH, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(2, 1501);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 2);
        TestGameResult(gr.results, 2, -1);
        TestGameResult(gr.results, 3, +1);
    }

    {
        // normal 1v1 remote watcher leaves
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::WATCH, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onDplayDeletePlayer(3);
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(1, 1);
        gm.onGameTick(1, 141);
        gm.onGameTick(2, 141);
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 2);
        TestGameResult(gr.results, 1, -1);
        TestGameResult(gr.results, 2, +1);
    }

    {
        // normal 1v1 player2 disconnects
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onRejectOther(1, 2);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        {
            const auto& gr = gm.getGameResult();
            TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
            TESTASSERT(gr.results.size() == 2);
            TestGameResult(gr.results, 1, +1);
            TestGameResult(gr.results, 2, -1);
        }
        {
            // no change despite player1 game shutting down
            gm.onDplayDeletePlayer(1);
            const auto& gr = gm.getGameResult();
            TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
            TESTASSERT(gr.results.size() == 2);
            TestGameResult(gr.results, 1, +1);
            TestGameResult(gr.results, 2, -1);
        }
    }

    {
        // 1v1 forced draw
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(1, 1);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 105);
        gm.onGameTick(2, 105);
        gm.onUnitDied(2, 1501);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 2);
        TestGameResult(gr.results, 1, 0);
        TestGameResult(gr.results, 2, 0);
    }

    {
        // 1v1 agreed draw after start
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onChat(1, "<player1>  allied with player2");
        gm.onChat(2, "<player2>  allied with player1");
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::VOID_RESULT);
        TESTASSERT(gr.results.size() == 0);
    }

    {
        // 1v1 agreed draw before start
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 50);
        gm.onGameTick(2, 50);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onChat(1, "<player1>  allied with player2");
        gm.onChat(2, "<player2>  allied with player1");
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::VOID_RESULT);
        TESTASSERT(gr.results.size() == 0);
    }

    {
        // 1v1 players bail before start
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 50);
        gm.onGameTick(2, 50);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(1, 1);
        gm.onUnitDied(1, 1501);
        gm.onGameTick(1, 51);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::NOT_READY);
        TESTASSERT(gr.results.size() == 0);
    }

    {
        // normal 2v2 players3,4 win
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // normal pregame alliances
        gm.onChat(1, "<player1>  allied with player2");
        gm.onChat(2, "<player2>  allied with player1");
        gm.onChat(3, "<player3>  allied with player4");
        gm.onChat(4, "<player4>  allied with player3");
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        gm.onGameTick(4, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        TESTASSERT(*gm.getMutualAllies(1, gm.m_frozenPlayers).begin() == 2);
        TESTASSERT(*gm.getMutualAllies(2, gm.m_frozenPlayers).begin() == 1);
        TESTASSERT(*gm.getMutualAllies(3, gm.m_frozenPlayers).begin() == 4);
        TESTASSERT(*gm.getMutualAllies(4, gm.m_frozenPlayers).begin() == 3);

        // game started already, changes in alliance should be ignored
        gm.onChat(3, "<player3>  broke alliance with player4");
        gm.onChat(4, "<player4>  broke alliance with player3");
        gm.onChat(1, "<player1>  allied with player3");
        gm.onChat(2, "<player2>  allied with player3");
        gm.onChat(3, "<player3>  allied with player1");
        gm.onChat(3, "<player3>  allied with player2");
        gm.onGameTick(1, 102);
        gm.onGameTick(2, 102);
        gm.onGameTick(3, 102);
        gm.onGameTick(4, 102);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        TESTASSERT(*gm.getMutualAllies(1, gm.m_frozenPlayers).begin() == 2);
        TESTASSERT(*gm.getMutualAllies(2, gm.m_frozenPlayers).begin() == 1);
        TESTASSERT(*gm.getMutualAllies(3, gm.m_frozenPlayers).begin() == 4);
        TESTASSERT(*gm.getMutualAllies(4, gm.m_frozenPlayers).begin() == 3);

        // original teams 1+2 vs 3+4 still in effect
        gm.onUnitDied(1, 1);
        gm.onUnitDied(2, 1501);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(4, 122);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 4);
        TestGameResult(gr.results, 1, -1);
        TestGameResult(gr.results, 2, -1);
        TestGameResult(gr.results, 3, 1);
        TestGameResult(gr.results, 4, 1);
    }

    {
        // normal 2v2 agreed draw after 1 and 3 die
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, TADemo::Side::ARM, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // normal pregame alliances
        gm.onChat(1, "<player1>  allied with player2");
        gm.onChat(2, "<player2>  allied with player1");
        gm.onChat(3, "<player3>  allied with player4");
        gm.onChat(4, "<player4>  allied with player3");
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        gm.onGameTick(4, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        TESTASSERT(*gm.getMutualAllies(1, gm.m_frozenPlayers).begin() == 2);
        TESTASSERT(*gm.getMutualAllies(2, gm.m_frozenPlayers).begin() == 1);
        TESTASSERT(*gm.getMutualAllies(3, gm.m_frozenPlayers).begin() == 4);
        TESTASSERT(*gm.getMutualAllies(4, gm.m_frozenPlayers).begin() == 3);

        // original teams 1+2 vs 3+4 still in effect
        gm.onUnitDied(1, 1);
        gm.onUnitDied(3, 3001);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        gm.onChat(2, "<player2>  allied with player4");
        gm.onChat(4, "<player4>  allied with player2");
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());

        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::VOID_RESULT);
        TESTASSERT(gr.results.size() == 0);
    }

    {
        // compstomp host's AIs humans win
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "AI:player1 1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "AI:player1 2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Comet Catcher", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, TADemo::Side::ARM, false, false);
        gm.onChat(1, "<player1>  allied with player2");
        gm.onChat(2, "<player2>  allied with player1");
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(3, 3001);
        gm.onUnitDied(4, 4501);
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 4);
        TestGameResult(gr.results, 1, 1);
        TestGameResult(gr.results, 2, 1);
        TestGameResult(gr.results, 3, -1);
        TestGameResult(gr.results, 4, -1);
    }

    {
        // compstomp remote's AIs humans win
        GameMonitor2 gm(NULL, 100, 10);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "AI:player2 1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "AI:player2 2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, TADemo::Side::ARM, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, TADemo::Side::ARM, false, false);
        gm.onStatus(3, "Comet Catcher", 1500, 3, TADemo::Side::ARM, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, TADemo::Side::ARM, false, false);
        gm.onChat(1, "<player1>  allied with player2");
        gm.onChat(2, "<player2>  allied with player1");
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        gm.onUnitDied(3, 3001);
        gm.onUnitDied(4, 4501);
        TESTASSERT(!gm.isGameOver());
        gm.onGameTick(1, 121);
        gm.onGameTick(2, 121);
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::READY_RESULT);
        TESTASSERT(gr.results.size() == 4);
        TestGameResult(gr.results, 1, 1);
        TestGameResult(gr.results, 2, 1);
        TestGameResult(gr.results, 3, -1);
        TestGameResult(gr.results, 4, -1);
    }
}
