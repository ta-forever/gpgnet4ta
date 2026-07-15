#include "GameMonitor2.h"
#include "TAPacketParser.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#ifdef QT_CORE_LIB
#include "QtCore/qdebug.h"
#include "QtCore/qdatetime.h"
#include "taflib/Watchdog.h"
#define LOG_WARNING(x) qWarning() << x
#define LOG_INFO(x) qInfo() << x
#define LOG_DEBUG(x) qDebug() << x
#define WATCHDOG(name,timeout) taflib::Watchdog wd(name,timeout)
#else
#define LOG_WARNING(x) std::cout << x << std::endl
#define LOG_INFO(x) std::cout << x << std::endl
#define LOG_DEBUG(x) std::cout << x << std::endl
#define WATCHDOG(name,timeout)
#endif

Player::Player():
    side(-1)
{ }

PlayerData::PlayerData():
    battleroomTeamSelection(5),
    isWatcher(false),
    isAI(false),
    slotNumber(-1),
    isDead(false),
    maxUnitsSeen(0),
    eliminationWallMs(0),
    tick(0u),
    dplayid(0u),
    armyNumber(0),
    teamNumber(0)
{ }

PlayerData::PlayerData(const Player &player):
    Player(player),
    battleroomTeamSelection(5),
    isWatcher(false),
    isAI(false),
    slotNumber(-1),
    isDead(false),
    maxUnitsSeen(0),
    eliminationWallMs(0),
    tick(0u),
    dplayid(0u),
    armyNumber(0),
    teamNumber(0)
{ }

std::ostream & PlayerData::print(std::ostream &s) const
{
    s << "'" << name << "': id=" << std::hex << dplayid
        << ", slot=" << slotNumber << ", side=" << int(side) << ", isAI=" << isAI << ", isDead=" << isDead << ", tick=" << tick << ", nrAllies=" << allies.size();
    return s;
}


GameMonitor2::GameMonitor2(GameEventHandler *gameEventHandler, std::uint32_t gameStartsAfterTickCount, std::uint32_t drawGameTicks, bool repairAsymmetricAlliances,
                           bool allowExternalAlliances, bool allowExternalDeaths) :
m_gameStartsAfterTickCount(gameStartsAfterTickCount),
m_drawGameTicks(drawGameTicks),
m_hostDplayId(0u),
m_localDplayId(0u),
m_gameLaunched(false),
m_gameStarted(false),
m_cheatsEnabled(false),
m_suspiciousStatus(false),
m_gameEventHandler(gameEventHandler),
m_repairAsymmetricAlliances(repairAsymmetricAlliances),
m_allowExternalAlliances(allowExternalAlliances),
m_allowExternalDeaths(allowExternalDeaths),
m_externalAlliancesEnabled(false),
m_externalDeathsEnabled(false),
m_lastExternalStatusMs(0),
m_localExiting(false),
m_initialNonWatcherCount(0)
{ }

namespace {
    // talauncher pushes PLAYER_STATUS at ~1 Hz; 5 s tolerates a few missed ticks before
    // we treat the shared-mem feed as gone and fall back to packet inference.
    constexpr std::int64_t EXTERNAL_STATUS_STALENESS_MS = 5000;
}

bool GameMonitor2::isExternalDeathsActive() const
{
    if (!m_externalDeathsEnabled) return false;
    if (m_lastExternalStatusMs == 0) return false;
#ifdef QT_CORE_LIB
    const std::int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    return (nowMs - m_lastExternalStatusMs) < EXTERNAL_STATUS_STALENESS_MS;
#else
    return true;
#endif
}

void GameMonitor2::setHostPlayerName(const std::string &playerName)
{
    m_hostPlayerName = playerName;
}

void GameMonitor2::setLocalPlayerName(const std::string& playerName)
{
    m_localPlayerName = playerName;
}

void GameMonitor2::setPlayerRealName(const std::string &playerName, const std::string &realName)
{
    m_playerRealNames[playerName] = realName;
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
    m_externalAlliancesEnabled = false;
    m_externalDeathsEnabled = false;
    m_lastExternalStatusMs = 0;
    m_localExiting = false;
}

std::set<std::string> GameMonitor2::getPlayerNames(bool queryIsPlayer, bool queryIsWatcher) const
{
    std::set<std::string> playerNames;
    for (const auto& player : m_players)
    {
        if (!player.second.isWatcher && queryIsPlayer || player.second.isWatcher && queryIsWatcher)
        {
            playerNames.insert(player.second.name);
        }
    }
    return playerNames;
}

const PlayerData& GameMonitor2::getPlayerData(const std::string& name) const
{
    return m_players.at(getPlayerDpidByName(name));
}

const PlayerData& GameMonitor2::getPlayerData(std::uint32_t dplayId) const
{
    return m_players.at(dplayId);
}

void GameMonitor2::onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string &name, tapacket::DPAddress *, tapacket::DPAddress *)
{
    WATCHDOG("GameMonitor2::onDplaySuperEnumPlayerReply", 100);
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

void GameMonitor2::onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string &name, tapacket::DPAddress *, tapacket::DPAddress *)
{
    WATCHDOG("GameMonitor2::onDplayCreateOrForwardPlayer", 100);
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
        // cannot notify at this point because we dont' know yet whether or not player as an AI (unless we assume AI names start with "AI:" ...)
        // notifyPlayerStatuses();
    }
}

void GameMonitor2::onDplayDeletePlayer(std::uint32_t dplayId)
{
    WATCHDOG("GameMonitor2::onDplayDeletePlayer", 100);
    if (dplayId != 0u && dplayId == m_localDplayId && !m_localExiting)
    {
        // Local TA exiting: exit cleanup wipes Players[] in dynmem, so the exporter starts
        // reporting every remote player at 0 units. Latch m_localExiting and let other peers
        // (still observing the live game) ship the result.
        m_localExiting = true;
        LOG_INFO("[GameMonitor2::onDplayDeletePlayer] local DPlay session torn down "
                 "(dplayId=" << dplayId << "); suppressing further result latching");
    }
    if (dplayId == 0u || m_players.count(dplayId) == 0)
    {
        return;
    }
    LOG_INFO("[GameMonitor2::onDplayDeletePlayer] dplayId=" << dplayId);

    if (!m_gameStarted)
    {
        PlayerData& player = m_players.at(dplayId);
        m_gameEventHandler->onClearSlot(player);
        m_players.erase(dplayId);
        for (auto &player : m_players)
        {
            player.second.allies.erase(dplayId);
        }
        updatePlayerArmies();
        notifyPlayerStatuses();
    }
    else
    {
        // A DPlay delete is an authoritative departure (TAF reconnect lives below
        // DirectPlay and never issues one), so mark a departing REMOTE player
        // inactive. This handles a CLEAN quit (DPlay DeletePlayer). A player who
        // drops WITHOUT a delete -- network loss, no clean teardown -- surfaces only
        // as a REJECT cascade; onRejectOther now handles that departure feed-
        // independently too (with a quorum). Both must be feed-independent because a
        // dropped player's abandoned units linger at units>0, so the engine feed
        // keeps reporting them "alive" and never latches their death; the phantom
        // then lingers in getActivePlayers() and checkEndGameCondition can't see
        // their team is gone until teardown zeroes everyone's units, by which point
        // every peer has gone m_localExiting and the result is VOIDed.
        // Never mark the LOCAL player dead: local death is authoritative only via
        // onUnitDied (cf. onRejectOther Change 2), and m_localExiting handles exit.
        if (dplayId != m_localDplayId)
        {
            PlayerData& departed = m_players.at(dplayId);
            if (!departed.isDead)
            {
                departed.isDead = true;
#ifdef QT_CORE_LIB
                if (departed.eliminationWallMs == 0)
                    departed.eliminationWallMs = QDateTime::currentMSecsSinceEpoch();
#endif
            }
        }

        int winningTeamNumber;
        if (checkEndGameCondition(winningTeamNumber))
        {
            // better latch the result right now since we may not receive any more game ticks from anyone
            latchEndGameResult(winningTeamNumber);
        }
    }
}

void GameMonitor2::onTaPacket(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, bool isLocalSource, const char* encrypted, int sizeEncrypted, const std::vector<tapacket::bytestring>& subpaks)
{
    for (const tapacket::bytestring& s : subpaks)
    {
        switch (tapacket::SubPacketCode(s[0]))
        {
        case tapacket::SubPacketCode::PLAYER_INFO_20:
        {
            tapacket::TPlayerInfo playerInfo(s);
            std::string mapName = playerInfo.getMapName(); // (const char*)(&s[1]);
            std::uint16_t maxUnits = playerInfo.maxUnits; // *(std::uint16_t*)(&s[0xa6]);
            bool isAI = playerInfo.isAI(); //  s[0x95] == 2;
            bool isWatcher = playerInfo.isWatcher(); // (s[0x9c] & 0x40) != 0;
            std::int8_t side = playerInfo.getSide(); // s[0x96];
            bool cheats = playerInfo.isCheatsEnabled(); // (s[0x9d] & 0x20) != 0;
            unsigned playerSlotNumber = playerInfo.getSlotNumber(); // s[0x97];
            if (playerSlotNumber < 10)
            {
                onStatus(sourceDplayId, mapName, maxUnits, playerSlotNumber, side, isWatcher, isAI, cheats);
            }
        }
        break;

        case tapacket::SubPacketCode::ALLY_23:
        {
            tapacket::TAlliance alliance(s);
            onAlliance(alliance.dpidFrom, alliance.dpidTo, alliance.alliedFromWithTo);
        }
        break;

        case tapacket::SubPacketCode::TEAM_24:
        {
            tapacket::TTeam team(s);
            onTeamSelection(team.dpidFrom, team.teamNumber);
        }
        break;

        case tapacket::SubPacketCode::CHAT_05:
        {
            std::string chat = (const char*)(&s[1]);
            onChat(sourceDplayId, chat);
        }
        break;

        case tapacket::SubPacketCode::UNIT_KILLED_0C:
        {
            std::uint16_t unitId = *(std::uint16_t*)(&s[1]);
            onUnitDied(sourceDplayId, unitId);
        }
        break;

        case tapacket::SubPacketCode::REJECT_1B:
        {
            std::uint32_t rejectedDplayId = *(std::uint32_t*)(&s[1]);
            onRejectOther(sourceDplayId, rejectedDplayId);
        }
        break;

        case tapacket::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
        {
            std::uint32_t tick = *(std::uint32_t*)(&s[3]);
            onGameTick(sourceDplayId, tick);
        }
        break;
        };
    }
}

void GameMonitor2::onStatus(
    std::uint32_t sourceDplayId, const std::string &mapName, std::uint16_t maxUnits,
    unsigned playerSlotNumber, int playerSide, bool isWatcher, bool isAI, bool cheats)
{
    WATCHDOG("GameMonitor2::onStatus", 100);
    if (m_players.count(sourceDplayId) == 0)
    {
        LOG_WARNING("[GameMonitor2::onStatus] ERROR unexpected dplayid=" << sourceDplayId);
        return;
    }
    if (!m_gameStarted)
    {
        auto &player = m_players[sourceDplayId];
        player.isAI = isAI;
        player.slotNumber = playerSlotNumber;
        if (player.side < 0 || player.isWatcher != isWatcher || player.side != playerSide)
        {
            player.side = playerSide;
            player.isWatcher = isWatcher;
            updatePlayerArmies();
            notifyPlayerStatuses();
        }

        if (sourceDplayId == m_hostDplayId && !mapName.empty() && maxUnits > 0)
        {
            if (m_mapName != mapName)
            {
                m_mapName = mapName;
                m_maxUnits = maxUnits;
                if (m_gameEventHandler) m_gameEventHandler->onGameSettings(m_mapName, m_maxUnits, m_hostPlayerName, m_localPlayerName);
            }
        }
    }
}

void GameMonitor2::onChat(std::uint32_t sourceDplayId, const std::string &chat)
{
    WATCHDOG("GameMonitor2::onChat", 100);
    if (m_players.count(sourceDplayId) == 0)
    {
        LOG_WARNING("[GameMonitor2::onChat] ERROR unexpected dplayid=" << sourceDplayId);
        return;
    }

    if (m_gameEventHandler) m_gameEventHandler->onChat(chat, sourceDplayId == m_localDplayId);

    if (updateAlliances(sourceDplayId, chat))
    {
        updatePlayerArmies();
        if (!m_gameStarted)
        {
            notifyPlayerStatuses();
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

void GameMonitor2::onAlliance(std::uint32_t subjectDpid, std::uint32_t objectDpid, bool isAllied)
{
    WATCHDOG("GameMonitor2::onAlliance", 100);
    auto itSubject = m_players.find(subjectDpid);
    if (itSubject == m_players.end())
    {
        LOG_WARNING("[GameMonitor2::onAlliance] ERROR unexpected subjectDpid=" << subjectDpid);
        return;
    }
    auto itObject = m_players.find(objectDpid);
    if (itObject == m_players.end())
    {
        LOG_WARNING("[GameMonitor2::onAlliance] ERROR unexpected objectDpid=" << objectDpid);
        return;
    }

    if (m_externalAlliancesEnabled)
    {
        return;
    }

    const bool wasAllied = itSubject->second.allies.count(objectDpid);
    LOG_INFO("[GameMonitor2::onAlliance] subject=" << itSubject->second.name.c_str() << "object=" << itObject->second.name.c_str() << "wasAllied=" << wasAllied << "isAllied=" << isAllied);

    if (isAllied)
    {
        itSubject->second.allies.insert(objectDpid);
    }
    else
    {
        itSubject->second.allies.erase(objectDpid);
    }

    if (wasAllied != isAllied)
    {

        updatePlayerArmies();
        if (!m_gameStarted)
        {
            notifyPlayerStatuses();
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

void GameMonitor2::onTeamSelection(std::uint32_t fromDplayId, int teamNumber)
{
    WATCHDOG("GameMonitor2::onTeamSelection", 100);
    auto itSubject = m_players.find(fromDplayId);
    if (itSubject == m_players.end())
    {
        LOG_WARNING("[GameMonitor2::onTeamSelection] ERROR unexpected subjectDpid=" << fromDplayId);
        return;
    }

    if (m_externalAlliancesEnabled)
    {
        return;
    }

    LOG_INFO("[GameMonitor2::onTeamSelection] subject=" << itSubject->second.name.c_str() << "brTeamNumber=" << teamNumber);
    itSubject->second.battleroomTeamSelection = teamNumber;

    bool anyAllianceChange = false;
    for (auto itObject = m_players.begin(); itObject != m_players.end(); ++itObject)
    {
        if (itObject->second.dplayid != fromDplayId)
        {
            const bool wasAlliedAB = itSubject->second.allies.count(itObject->second.dplayid);
            const bool wasAlliedBA = itObject->second.allies.count(itSubject->second.dplayid);
            const bool nowAllied = (teamNumber < 5) && (teamNumber == itObject->second.battleroomTeamSelection);

            if (nowAllied)
            {
                itSubject->second.allies.insert(itObject->second.dplayid);
                itObject->second.allies.insert(itSubject->second.dplayid);
                anyAllianceChange = anyAllianceChange || !wasAlliedAB || !wasAlliedBA;
            }
            else
            {
                itSubject->second.allies.erase(itObject->second.dplayid);
                itObject->second.allies.erase(itSubject->second.dplayid);
                anyAllianceChange = anyAllianceChange || wasAlliedAB || wasAlliedBA;
            }
            LOG_INFO("[GameMonitor2::onTeamSelection] object:" << itObject->second.name.c_str() << "brTeam:" << itObject->second.battleroomTeamSelection << "wasAlliedAB:" << wasAlliedAB << "wasAlliedBA:" << wasAlliedBA << "nowAllied:" << nowAllied << "anyAllianceChange:" << anyAllianceChange);
        }
    }
    if (anyAllianceChange)
    {
        updatePlayerArmies();
        if (!m_gameStarted)
        {
            notifyPlayerStatuses();
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
    WATCHDOG("GameMonitor2::onUnitDied", 100);
    if (m_players.count(sourceDplayId) == 0)
    {
        LOG_WARNING("[GameMonitor2::onUnitDied] ERROR unexpected dplayid=" << sourceDplayId);
        return;
    }

    if (unitId % m_maxUnits == 1 && !isExternalDeathsActive())
    {
        LOG_INFO("[GameMonitor2::onUnitDied] sourcedplayId=" << sourceDplayId << " tick=" << getMostRecentGameTick() << " unitId=" << unitId << "(commander), maxUnits=" << m_maxUnits);
        std::ostringstream ss;
        m_players[sourceDplayId].print(ss);
        LOG_INFO(ss.str().c_str());

        m_players[sourceDplayId].isDead = true;
#ifdef QT_CORE_LIB
        if (m_players[sourceDplayId].eliminationWallMs == 0)
            m_players[sourceDplayId].eliminationWallMs = QDateTime::currentMSecsSinceEpoch();
#endif

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

#ifdef QT_CORE_LIB
void GameMonitor2::onExternalPlayerStatus(const QVector<int>& allyFlags, const QVector<int>& actives,
                                          const QVector<int>& unitCounts, const QVector<int>& allyTeams,
                                          const QVector<int>& propertyMasks, const QVector<int>& dplayIds)
{
    WATCHDOG("GameMonitor2::onExternalPlayerStatus", 100);

    if (m_allowExternalAlliances) m_externalAlliancesEnabled = true;
    if (m_allowExternalDeaths)    m_externalDeathsEnabled    = true;
    m_lastExternalStatusMs = QDateTime::currentMSecsSinceEpoch();

    bool deathChange    = false;
    bool allianceChange = false;

    // Exporter arrays are indexed by TA's local Players[0..9] order (local at slot 0), so
    // the same dplayId lands at different indices on each peer. Resolve players by dplayId.
    for (int xslot = 0; xslot < 10; xslot++)
    {
        if (actives[xslot] == 0) continue;
        const std::uint32_t dpid = static_cast<std::uint32_t>(dplayIds[xslot]);
        auto playerIt = m_players.find(dpid);
        if (playerIt == m_players.end()) continue;
        PlayerData& p = playerIt->second;

        bool isWatcher = (propertyMasks[xslot] & 0x40) != 0;
        if (m_externalAlliancesEnabled && p.isWatcher != isWatcher)
        {
            p.isWatcher = isWatcher;
            allianceChange = true;
            LOG_INFO("[GameMonitor2::onExternalPlayerStatus] xslot " << xslot
                     << " player '" << p.name.c_str() << "' isWatcher=" << isWatcher);
        }

        // Max-seen-then-zero edge latch on engine unit count. Deliberately not gated on
        // !isWatcher: TA flips eliminated players to watcher mode automatically, so a
        // (WATCH, units=0) snapshot would otherwise be silently ignored. The maxUnitsSeen
        // guard is what filters out pre-game "never had units yet" snapshots.
        if (m_externalDeathsEnabled && unitCounts[xslot] > p.maxUnitsSeen)
        {
            p.maxUnitsSeen = unitCounts[xslot];
        }
        if (m_externalDeathsEnabled && p.maxUnitsSeen > 0 && unitCounts[xslot] == 0 && !p.isDead)
        {
            p.isDead = true;
            p.eliminationWallMs = QDateTime::currentMSecsSinceEpoch();
            deathChange = true;
            LOG_INFO("[GameMonitor2::onExternalPlayerStatus] xslot " << xslot
                     << " player '" << p.name.c_str() << "' eliminated"
                     << " (peak units=" << p.maxUnitsSeen
                     << ", elimWallMs=" << p.eliminationWallMs << ")");
        }
    }

    // Pre-game only — teams freeze at game start.
    if (m_externalAlliancesEnabled && !m_gameStarted)
    {
        for (int xslot = 0; xslot < 10; xslot++)
        {
            if (actives[xslot] == 0) continue;
            const std::uint32_t dpid = static_cast<std::uint32_t>(dplayIds[xslot]);
            auto playerIt = m_players.find(dpid);
            if (playerIt == m_players.end()) continue;
            PlayerData& p = playerIt->second;

            std::set<std::uint32_t> newAllies;
            for (int j = 0; j < 10; j++)
            {
                if (j == xslot) continue;
                if (allyFlags[xslot*10 + j] != 0)
                {
                    if (actives[j] == 0) continue;
                    const std::uint32_t allyDpid = static_cast<std::uint32_t>(dplayIds[j]);
                    if (m_players.count(allyDpid))
                    {
                        newAllies.insert(allyDpid);
                    }
                }
            }
            if (p.allies != newAllies)
            {
                p.allies = newAllies;
                allianceChange = true;
            }

            if (p.battleroomTeamSelection != allyTeams[xslot])
            {
                p.battleroomTeamSelection = allyTeams[xslot];
                allianceChange = true;
            }
        }
    }

    if (allianceChange)
    {
        updatePlayerArmies();
        notifyPlayerStatuses();

        int winningTeam;
        if (checkEndGameCondition(winningTeam) && winningTeam < 0)
            latchEndGameResult(winningTeam);
    }

    if (deathChange)
    {
        int winningTeam;
        if (checkEndGameCondition(winningTeam))
        {
            if (winningTeam > 0)
                latchEndGameTick(getMostRecentGameTick() + m_drawGameTicks);
            else
                latchEndGameResult(winningTeam);
        }
    }
}

#endif

void GameMonitor2::onRejectOther(std::uint32_t sourceDplayId, std::uint32_t rejectedDplayId)
{
    WATCHDOG("GameMonitor2::onRejectOther", 100);
    if (m_players.count(sourceDplayId) == 0)
    {
        LOG_WARNING("[GameMonitor2::onRejectOther] ERROR unexpected sourceDplayId=" << sourceDplayId);
        return;
    }
    if (m_players.count(rejectedDplayId) == 0)
    {
        // player left before game started?
        return;
    }

    LOG_INFO("[GameMonitor2::onRejectOther] sourceDplayId=" << sourceDplayId << " rejectedDplayId=" << rejectedDplayId);
    {
        std::ostringstream ss;
        m_players[sourceDplayId].print(ss);
        LOG_INFO(ss.str().c_str());
    }
    {
        std::ostringstream ss;
        m_players[rejectedDplayId].print(ss);
        LOG_INFO(ss.str().c_str());
    }

    if (!m_gameStarted)
    {
        PlayerData& player = m_players.at(rejectedDplayId);
        m_gameEventHandler->onClearSlot(player);
        m_players.erase(rejectedDplayId);
        for (auto &player : m_players)
        {
            player.second.allies.erase(rejectedDplayId);
        }
        m_rejecters.erase(rejectedDplayId);
        updatePlayerArmies();
        notifyPlayerStatuses();
    }
    else
    {
        // NOT gated on isExternalDeathsActive(): a reject is a DirectPlay departure,
        // and the engine feed is blind to departures. onExternalPlayerStatus only
        // reports ACTIVE slots, and a dropped player's abandoned units linger in the
        // sim at units>0, so the feed keeps reporting them "alive" and never latches
        // their death (game 181825: Chao_Storm dropped, 7-peer reject cascade, feed
        // never zeroed him -> phantom active player jammed the endgame for ~6 min).
        // Departure authority sits below the sim, same as the DPlay-delete path in
        // onDplayDeletePlayer, which is likewise feed-independent. The quorum below is
        // what separates a real departure from one flaky per-link timeout.

        // Change 2: never declare the LOCAL player dead from a remote reject. The
        // local TA is authoritative for the local player; if we were actually dead,
        // onUnitDied for our commander would have fired. A peer's REJECT just means
        // their TA gave up trying to reach us, which is a network event.
        if (rejectedDplayId == m_localDplayId)
        {
            LOG_INFO("[GameMonitor2::onRejectOther] ignoring reject targeting LOCAL player " << rejectedDplayId);
            return;
        }

        // Change 1: require a quorum of distinct rejecters before treating the
        // rejected peer as dead. One peer asserting "I can't reach X" is a
        // per-link network timeout, not a game-state signal. 1v1 games (initial
        // non-watcher count <= 2) keep today's behaviour (quorum=1) because the
        // lone opponent is the only possible rejecter.
        m_rejecters[rejectedDplayId].insert(sourceDplayId);
        const std::size_t REJECT_DEATH_QUORUM = 2;
        const std::size_t requiredQuorum =
            (m_initialNonWatcherCount <= 2) ? 1 : REJECT_DEATH_QUORUM;
        if (m_rejecters[rejectedDplayId].size() < requiredQuorum)
        {
            LOG_INFO("[GameMonitor2::onRejectOther] reject quorum "
                     << m_rejecters[rejectedDplayId].size() << "/"
                     << requiredQuorum << " for " << rejectedDplayId
                     << "; deferring death flag");
            return;
        }

        m_players[rejectedDplayId].isDead = true;
#ifdef QT_CORE_LIB
        if (m_players[rejectedDplayId].eliminationWallMs == 0)
            m_players[rejectedDplayId].eliminationWallMs = QDateTime::currentMSecsSinceEpoch();
#endif

        int winningTeamNumber;
        if (checkEndGameCondition(winningTeamNumber))
        {
            // Immediate latch (no deferral). After a reject, sim ticks may stop coming
            // (post-game disconnect, both players on result screen), so the tick-driven
            // deferred latch in onGameTick wouldn't fire and we'd never report at all.
            latchEndGameResult(winningTeamNumber);
        }
    }
}

static void repairAsymmetricAlliances(std::map<std::uint32_t, PlayerData>& players)
{
    bool anyRepair = true;
    while (anyRepair)
    {
        anyRepair = false;
        for (const std::pair<std::uint32_t, PlayerData> & p : players)
        {
            std:uint32_t dpid = p.first;
            auto& player = p.second;
            if (player.isWatcher || player.isDead)
            {
                continue;
            }
            bool hasMutualAlly = false;
            for (std::uint32_t allyId : player.allies)
            {
                if (players.count(allyId) && players.at(allyId).allies.count(dpid))
                {
                    hasMutualAlly = true;
                    break;
                }
            }
            if (hasMutualAlly)
            {
                continue;
            }
            for (const std::pair<std::uint32_t, PlayerData>& pOther: players)
            {
                std::uint32_t otherDpid = pOther.first;
                auto& other = pOther.second;
                if (otherDpid == dpid || other.isWatcher || other.isDead)
                {
                    continue;
                }
                if (other.allies.count(dpid) && !player.allies.count(otherDpid))
                {
                    LOG_INFO("[repairAsymmetricAlliances] repairing asymmetric alliance: '"
                        << player.name.c_str() << "' -> '" << other.name.c_str() << "'");
                    players.at(dpid).allies.insert(otherDpid);
                    anyRepair = true;
                }
            }
        }
    }
}

void GameMonitor2::onGameTick(std::uint32_t sourceDplayId, std::uint32_t tick)
{
    WATCHDOG("GameMonitor2::onGameTick", 100);
    if (m_players.count(sourceDplayId) == 0)
    {
        LOG_WARNING("[GameMonitor2::onGameTick] ERROR unexpected sourceDplayId=" << sourceDplayId);
        return;
    }

    if (!m_gameLaunched)
    {
        m_gameLaunched = true;
        if (m_gameEventHandler) m_gameEventHandler->onGameStarted(tick, false);
    }

    if (!m_gameStarted && tick > m_gameStartsAfterTickCount)
    {
        if (m_repairAsymmetricAlliances)
        {
            repairAsymmetricAlliances(m_players);
            updatePlayerArmies();
        }

        // server logic requires alliances to be locked at launch so we require teams to be set before game starts (tick > m_gameStartsAfterTickCount)
        // so here we grab the player status (in particular the alliances) at time of game start
        m_frozenPlayers = m_players;

        // Snapshot initial roster size (non-watcher, non-AI) so onRejectOther can
        // scale its quorum: 1v1 games keep today's single-reject latch, larger
        // games require >=2 distinct rejecters before treating a peer as dead.
        m_initialNonWatcherCount = 0;
        for (const auto& kv : m_frozenPlayers)
        {
            if (!kv.second.isWatcher && !kv.second.isAI)
            {
                ++m_initialNonWatcherCount;
            }
        }

        m_gameStarted = true;
        notifyPlayerStatuses();
        if (m_gameEventHandler) m_gameEventHandler->onGameStarted(tick, true);

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
            LOG_WARNING("[GameMonitor2::onGameTick] it should not be possible for a game to become unfinished once it is finished!");
            return;
        }
        latchEndGameResult(winningTeamNumber);
    }
}

std::uint32_t GameMonitor2::getPlayerDpidByName(const std::string &name) const
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
        if (!player.second.isDead && !player.second.isWatcher)
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
    if (m_externalAlliancesEnabled)
    {
        return false;
    }

    if (sender == 0u)
    {
        std::size_t senderStart = chat.find_first_of('<');
        std::size_t senderEnd = chat.find_first_of('>');
        if (senderStart != std::string::npos && senderEnd != std::string::npos && senderStart == 0u && senderEnd > 1u)
        {
            std::string senderName = chat.substr(1, senderEnd - 1);
            sender = getPlayerDpidByName(senderName);
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
        std::uint32_t playernumber2 = getPlayerDpidByName(lastWord);
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

    // sort by dplayid to ensure consistent across all players' instances
    std::sort(sortedPlayers.begin(), sortedPlayers.end(),
        [](const PlayerData* p1, const PlayerData* p2)
        -> bool { return p1->dplayid < p2->dplayid; });

    int teamCount = 1;  // assign team numbers consecutively to each mutually allied set
    int armyCount = 0;  // assign army number by consecutive sortedPlayer
    for (PlayerData* sortedPlayer : sortedPlayers)
    {
        if (sortedPlayer->isWatcher || sortedPlayer->isDead)
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
                    !m_players.at(allynumber).isWatcher &&
                    !m_players.at(allynumber).isDead)
                {
                    m_players.at(allynumber).teamNumber = teamCount;
                }
            }
        }
    }
}

void GameMonitor2::notifyPlayerStatuses()
{
    if (!m_gameEventHandler)
    {
        return;
    }

    std::vector<bool> isSlotUsed(10, false);
    for (const auto &p : m_players)
    {
        if (p.second.side >= 0)
        {
            // Report frozen (launch-time) army/team after game start. updatePlayerArmies
            // zeros these for dead/watcher players, and propagating the zeros via
            // sendPlayerOption("Army", 0) corrupts the server's game.armies and trips a
            // vacuous-true bug in is_mutually_agreed_draw.
            PlayerData reportPlayer = p.second;
            if (m_gameStarted)
            {
                auto frozenIt = m_frozenPlayers.find(p.first);
                if (frozenIt != m_frozenPlayers.end())
                {
                    reportPlayer.armyNumber = frozenIt->second.armyNumber;
                    reportPlayer.teamNumber = frozenIt->second.teamNumber;
                }
            }
            m_gameEventHandler->onPlayerStatus(reportPlayer, getMutualAllyNames(p.first, m_players));
            // NB UNKNOWN side means the slot number is invalid too
            if (unsigned(p.second.slotNumber) < isSlotUsed.size())
            {
                isSlotUsed[p.second.slotNumber] = true;
            }
        }
    }

    for (std::size_t n = 0; n < isSlotUsed.size(); ++n)
    {
        if (!isSlotUsed[n])
        {
            PlayerData pd;
            pd.slotNumber = n;
            pd.dplayid = 0;
            m_gameEventHandler->onClearSlot(pd);
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
        if (m_gameResult.endGameTick == 0)
        {
            LOG_INFO("[GameMonitor2::checkEndGameCondition] end game at tick " << getMostRecentGameTick() << " because no active players");
        }
        return true;
    }

    int lastTeamStanding;
    if (isPlayersAllAllied(activePlayers, m_frozenPlayers, lastTeamStanding))
    {
        // game over with one team prevailing
        winningTeamNumber = lastTeamStanding;
        if (m_gameResult.endGameTick == 0)
        {
            LOG_INFO("[GameMonitor2::checkEndGameCondition] end game at tick " << getMostRecentGameTick() << " because all (" << activePlayers.size() << ") active players belong to same (frozen) team:" << lastTeamStanding);
        }
        return true;
    }

    if (isPlayersAllAllied(activePlayers, m_players, lastTeamStanding))
    {
        // game over with mutually agreed draw
        winningTeamNumber = -1;
        if (m_gameResult.endGameTick == 0)
        {
            LOG_INFO("[GameMonitor2::checkEndGameCondition] end game at tick " << getMostRecentGameTick() << " because all (" << activePlayers.size() << ") active players belong to same (dynamic) team:" << lastTeamStanding);
        }
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
        LOG_INFO("[GameMonitor2::latchEndGameTick] tick=" << endGameTick);
    }
    return m_gameResult.endGameTick;
}

const GameResult & GameMonitor2::latchEndGameResult(int winningTeamNumber /* or zero for forced draw, -1 for mutual draw */)
{
    if (m_gameResult.status != GameResult::Status::NOT_READY)
    {
        return m_gameResult;
    }

    if (m_localExiting)
    {
        // See m_localExiting in the header — local TA tearing down, results unreliable.
        LOG_INFO("[GameMonitor2::latchEndGameResult] suppressed (local player exiting)");
        m_gameResult.status = GameResult::Status::VOID_RESULT;
        return m_gameResult;
    }

    m_gameResult.results.clear();
    m_gameResult.endGameTick = getMostRecentGameTick();
    LOG_INFO("[GameMonitor2::latchEndGameResult] tick=" << m_gameResult.endGameTick << " winningTeamNumber=" << winningTeamNumber);

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
        if (nArmy == 0 || nTeam == 0 || player.second.isWatcher)
        {
            // either updatePlayerArmies hasn't been called (a bug), or player is a watcher (is normal)
            continue;
        }

        GameResult::ArmyResult res;
        res.army = nArmy;
        res.slot = player.second.slotNumber;
        res.alias = player.second.name;
        res.realName = m_playerRealNames[player.second.name];
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

static void SetTestAlliance(int method, GameMonitor2& gm, int p1, int p2, bool isAllied)
{
    std::ostringstream p1ss;
    if (isAllied)
    {
        p1ss << "<player" << p1 << ">" << "  allied with player" << p2;
    }
    else
    {
        p1ss << "<player" << p1 << ">" << "  broke alliance with player" << p2;
    }

    switch (method)
    {
    case 0:
        gm.onChat(p1, p1ss.str());
        break;

    case 1:
        gm.onAlliance(p1, p2, isAllied);
        break;

    case 2:
        gm.onChat(p1, p1ss.str());
        gm.onAlliance(p1, p2, isAllied);
        break;

    case 3:
        if (p1 % 2 == 0)
        {
            gm.onChat(p1, p1ss.str());
        }
        else
        {
            gm.onAlliance(p1, p2, isAllied);
        }
        break;

    case 4:
        if (p1 % 2 == 0)
        {
            gm.onChat(p1, p1ss.str());
        }
        else
        {
            gm.onChat(p1, p1ss.str());
            gm.onAlliance(p1, p2, isAllied);
        }
        break;

    case 5:
        if (p1 % 3 == 0)
        {
            gm.onChat(p1, p1ss.str());
        }
        else
        {
            gm.onAlliance(p1, p2, isAllied);
        }
        break;

    case 6:
        if (p1 % 3 == 0)
        {
            gm.onChat(p1, p1ss.str());
        }
        else
        {
            gm.onChat(p1, p1ss.str());
            gm.onAlliance(p1, p2, isAllied);
        }
        break;
    };
}

void GameMonitor2::test(int allianceMethod)
{
    qInfo() << "[GameMonitor2::test] =========== allianceMethod:" << allianceMethod;
    {
        // normal 1v1 player1 wins
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, true, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, true, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, true, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player1");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, true, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, true, false, false);
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
        // normal 1v1 player2 disconnects — observed from player 1 (the rejecter)
        // perspective. With initial non-watcher count == 2, the quorum drops to 1
        // and the single reject is sufficient to latch.
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player1");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
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
        // Local-player guard (Change 2): a reject targeting the LOCAL player must
        // NOT mark the local as dead — local TA is authoritative for local death.
        // Observed from player2's (the rejected player's) perspective.
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        // Remote peer claims local player is rejected — must be ignored.
        gm.onRejectOther(1, 2);
        TESTASSERT(!gm.isGameOver());
    }

    {
        // Quorum-of-2 guard (Change 1): a single reject is insufficient in games
        // with >2 initial players. Specifically: 3-player scenario where only
        // player1 rejects player3; player3 stays alive in m_players, game continues.
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player1");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Comet Catcher", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Comet Catcher", 1500, 3, 0, false, false, false);
        // Make player1 and player2 allies; player3 is the lone opponent.
        gm.onAlliance(1, 2, true);
        gm.onAlliance(2, 1, true);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        TESTASSERT(gm.isGameStarted());
        // Solo reject of player3 by player1 — quorum=2, only 1 source → no latch.
        gm.onRejectOther(1, 3);
        TESTASSERT(!gm.isGameOver());
        // Second peer also rejects — quorum reached, game ends with allied team win.
        gm.onRejectOther(2, 3);
        TESTASSERT(gm.isGameOver());
    }

    {
        // 1v1 forced draw
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());
        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::VOID_RESULT);
        TESTASSERT(gr.results.size() == 0);
    }

    {
        // 1v1 agreed draw before start
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onGameTick(1, 50);
        gm.onGameTick(2, 50);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // normal pregame alliances
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
        SetTestAlliance(allianceMethod, gm, 3, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 3, true);
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
        SetTestAlliance(allianceMethod, gm, 3, 4, false);
        SetTestAlliance(allianceMethod, gm, 4, 3, false);
        SetTestAlliance(allianceMethod, gm, 1, 3, true);
        SetTestAlliance(allianceMethod, gm, 2, 3, true);
        SetTestAlliance(allianceMethod, gm, 3, 1, true);
        SetTestAlliance(allianceMethod, gm, 3, 2, true);
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
        // Regression: a player who QUITS (DPlay delete) while still alive must drop
        // out of the active set immediately, so their team's elimination is seen and
        // the result latches with no further game ticks. Teams 1+2 vs 3+4; team 1
        // loses (p1 commander dies in-sim, p2 quits while alive); local player3 (a
        // winner) is still in-game so it can report. Without the fix p2 lingers
        // "alive" and the game never ends here. (Reproduced via the non-Qt API; the
        // external-deaths suppression that exposed it is Qt-only, but the fix is
        // mode-independent.)
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player3");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
        SetTestAlliance(allianceMethod, gm, 3, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 3, true);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        gm.onGameTick(4, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // team 1's first member dies in-sim; team 1 still has p2 active -> not over
        gm.onUnitDied(1, 1);
        TESTASSERT(!gm.isGameOver());

        // team 1's last member QUITS while alive. No further game ticks follow.
        // The result must latch right here. (Without the fix the game stays open.)
        gm.onDplayDeletePlayer(2);
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
        // Regression (game 181825): a player who DROPS while still alive WITHOUT a
        // clean DPlay delete -- pure network loss -- surfaces only as a REJECT
        // cascade, never an onDplayDeletePlayer. That departure must still drop them
        // from the active set so their team's elimination is seen and the result
        // latches, even though the engine feed keeps reporting their abandoned units
        // as "alive" (why the reject path is no longer gated on isExternalDeathsActive;
        // that suppression is Qt-only, but the un-gated behaviour is mode-independent).
        // The quorum is RETAINED: a lone reject is one flaky per-link timeout and must
        // NOT end the game -- only a quorum (>=2 for a team game) does. Teams 1+2 vs
        // 3+4; team 1 loses (p1 commander dies in-sim, p2 drops); local player3 (a
        // winner) is still in-game so it can report.
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player3");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
        SetTestAlliance(allianceMethod, gm, 3, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 3, true);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        gm.onGameTick(4, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // team 1's first member dies in-sim; team 1 still has p2 active -> not over
        gm.onUnitDied(1, 1);
        TESTASSERT(!gm.isGameOver());

        // p2 drops (no DPlay delete). A single reject must NOT end the game: the
        // quorum (2 for a 4-player game) guards a lone flaky per-link timeout.
        gm.onRejectOther(3, 2);
        TESTASSERT(!gm.isGameOver());

        // second distinct rejecter meets quorum -> p2 is a confirmed departure ->
        // team 1 is gone -> result latches with no further game ticks.
        gm.onRejectOther(4, 2);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // normal pregame alliances
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
        SetTestAlliance(allianceMethod, gm, 3, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 3, true);
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
        gm.onGameTick(1, 2101);
        gm.onGameTick(2, 2101);
        gm.onGameTick(3, 2101);
        gm.onGameTick(4, 2101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        SetTestAlliance(allianceMethod, gm, 2, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 2, true);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(gm.isGameOver());

        const auto& gr = gm.getGameResult();
        TESTASSERT(gr.status == GameResult::Status::VOID_RESULT);
        TESTASSERT(gr.results.size() == 0);
    }

    {
        // normal 2v2 but one player missing mutual alliance
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // normal pregame alliances
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, false);   // but 2 forgets to reciprocate
        SetTestAlliance(allianceMethod, gm, 3, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 3, true);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        gm.onGameTick(4, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // team 3+4 dies
        gm.onUnitDied(3, 3001);
        gm.onUnitDied(4, 4501);
        gm.onGameTick(1, 2101);
        gm.onGameTick(2, 2101);
        gm.onGameTick(3, 2101);
        gm.onGameTick(4, 2101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());
    }

    {
        // normal 2v2 but one player missing mutual alliance, and we turn on the fix
        GameMonitor2 gm(NULL, 100, 10, true /* fix on */);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "player3", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "player4", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Canal Crossing", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        gm.onGameTick(1, 10);
        gm.onGameTick(2, 10);
        gm.onGameTick(3, 10);
        gm.onGameTick(4, 10);
        TESTASSERT(!gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // normal pregame alliances
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, false);   // but 2 forgets to reciprocate
        SetTestAlliance(allianceMethod, gm, 3, 4, true);
        SetTestAlliance(allianceMethod, gm, 4, 3, true);
        gm.onGameTick(1, 101);
        gm.onGameTick(2, 101);
        gm.onGameTick(3, 101);
        gm.onGameTick(4, 101);
        TESTASSERT(gm.isGameStarted());
        TESTASSERT(!gm.isGameOver());

        // team 3+4 dies
        gm.onUnitDied(3, 3001);
        gm.onUnitDied(4, 4501);
        gm.onGameTick(1, 2101);
        gm.onGameTick(2, 2101);
        gm.onGameTick(3, 2101);
        gm.onGameTick(4, 2101);
        TESTASSERT(gm.isGameStarted());
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
        // compstomp host's AIs humans win
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "AI:player1 1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "AI:player1 2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Comet Catcher", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
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
        GameMonitor2 gm(NULL, 100, 10, false);
        gm.setHostPlayerName("player1");
        gm.setLocalPlayerName("player2");
        gm.onDplayCreateOrForwardPlayer(0x0008, 1, "player1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 2, "player2", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 3, "AI:player2 1", NULL, NULL);
        gm.onDplayCreateOrForwardPlayer(0x0008, 4, "AI:player2 2", NULL, NULL);
        gm.onStatus(1, "Comet Catcher", 1500, 1, 0, false, false, false);
        gm.onStatus(2, "Canal Crossing", 1500, 2, 0, false, false, false);
        gm.onStatus(3, "Comet Catcher", 1500, 3, 0, false, false, false);
        gm.onStatus(4, "Canal Crossing", 1500, 4, 0, false, false, false);
        SetTestAlliance(allianceMethod, gm, 1, 2, true);
        SetTestAlliance(allianceMethod, gm, 2, 1, true);
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
