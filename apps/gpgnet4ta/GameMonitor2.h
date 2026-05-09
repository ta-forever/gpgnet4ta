#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#ifdef QT_CORE_LIB
#include <QtCore/qvector.h>
#endif

#include "TAPacketParser.h"
#include "tapacket/TPacket.h"

struct Player
{
    Player();

    int side;
    std::string name;           // upto 64
};

struct PlayerData : public Player
{
    PlayerData();
    PlayerData(const Player& player);

    std::ostream& print(std::ostream& s) const;

    std::set<std::uint32_t> allies;
    int battleroomTeamSelection;// not to be confused with teamNumber. teamnumber is our own deduction
    bool isWatcher;
    bool isAI;
    int slotNumber;             // as reported by game. Seems may be suitable for use as armyNumber when reporting to gpgnet.  not sure
    bool isDead;                // advertised that their commander died, or was rejected by a player
    int  maxUnitsSeen;          // peak engine-reported live unit count for this player so far in
                                // this game (across all received PLAYER_STATUS snapshots). Used to
                                // distinguish "never had units yet" from "had units, now zero" when
                                // running the elimination edge-latch.
    std::int64_t eliminationWallMs; // wall-clock ms when isDead first transitioned true (0 if never).
    std::uint32_t tick;         // serial of last 2C packet
    std::uint32_t dplayid;
    int armyNumber;             // assigned based on sorted names so is consistent across all players' instances
    int teamNumber;             // reflects alliances at time of launch, and is consistent across all players' demo recordings
                                // 0:invalid, >0:team number.  The no-team-selected / ffa option is not supported
                                // everyone is on a team regardless if that team only has one player
                                // (Forged Alliance reserves team=1 for no-team-selected)
};

struct GameResult
{
    enum class Status
    {
        NOT_READY = 1,
        VOID_RESULT = 2,        // game is over but result is void (eg mutually agreed draw)
        READY_RESULT = 3        // game is over and result is ready
    };

    struct ArmyResult
    {
        int army;
        int slot;
        std::string alias;
        std::string realName;
        int team;
        int score;
    };

    Status status;
    std::vector<ArmyResult> results;
    std::uint32_t endGameTick; // if >0 indicates game tick at which game will imminently end

    GameResult() : status(Status::NOT_READY), endGameTick(0u) { }

    void print(std::ostream& os) const
    {
        for (const auto& result : results)
        {
            os << std::dec << "army" << result.army << "(" << result.alias << "/slot" << result.slot << "/team" << result.team << "): sore=" << result.score << std::endl;
        }
        os << "endGameTick=" << endGameTick << std::endl;
    }
};

class GameEventHandler
{
public:
    virtual void onGameSettings(const std::string &mapName, std::uint16_t maxUnits, const std::string &hostName, const std::string &localName) = 0;
    virtual void onPlayerStatus(const PlayerData &, const std::set<std::string> & mutualAllies) = 0;
    virtual void onClearSlot(const PlayerData &) = 0;

    // will be called twice. first time with tick < gameStartsAfterTickCount and teamsFrozen=false; 
    // and second time with tick > gameStartsAfterTickCount and teamsFrozen=true.
    // First call indicates game was launched but GpgNet should not yet be informed as teams are still open
    // and unscrupulous players may still try to dgun ghost commander in top-left
    // Second call indicates teams are frozen and GpgNet should be informed of game start
    virtual void onGameStarted(std::uint32_t tick, bool teamsFrozen) = 0;

    virtual void onGameEnded(const GameResult &) = 0;

    virtual void onChat(const std::string& msg, bool isLocalPlayerSource) = 0;
};

class GameMonitor2 : public tapacket::TaPacketHandler
{
    std::string m_hostPlayerName;
    std::string m_localPlayerName;
    const std::uint32_t m_gameStartsAfterTickCount;
    const std::uint32_t m_drawGameTicks;
    std::uint32_t m_hostDplayId;
    std::uint32_t m_localDplayId;
    bool m_gameLaunched;                                // flag to indicate game was launched but still as yet insufficient activity to flag m_gameStarted
    bool m_gameStarted;                                 // flag to indicate sufficient activity to consider an actual game occurred
    bool m_cheatsEnabled;
    bool m_suspiciousStatus;                            // some irregularity was encountered, you may want to invalidate this game for tourney / ranking purposes
    std::string m_mapName;
    std::uint16_t m_maxUnits;
    std::map<std::uint32_t, PlayerData> m_players;      // keyed by PlayerData::dplayid
    std::map<std::uint32_t, PlayerData> m_frozenPlayers;// m_players (in particular the teams and alliances) as is was at time of game start
    std::map<std::string, std::string> m_playerRealNames;// keyed by in-game alias
    GameResult m_gameResult;                            // empty until latched onto the first encountered victory condition
    bool m_repairAsymmetricAlliances;
    bool m_allowExternalAlliances;      // if false, never switch away from dplay-based alliance inference
    bool m_allowExternalDeaths;         // if false, never switch away from dplay-based death detection
    bool m_externalAlliancesEnabled;    // true = currently using onExternalPlayerStatus for alliances
    bool m_externalDeathsEnabled;       // true = currently using onExternalPlayerStatus for death detection
    std::int64_t m_lastExternalStatusMs; // wall-clock ms of last onExternalPlayerStatus; 0 if never received.
                                        // Used to fall back to packet inference when shared-mem stops flowing
                                        // (e.g. TA without tadr-ddraw, or exporter stopped updating).
    bool m_localExiting;                // set true when onDplayDeletePlayer fires for m_localDplayId — i.e.
                                        // the local TA process is tearing down its own DPlay session. After
                                        // this point, the local view of remote players (units, dplay state)
                                        // becomes unreliable due to TA's exit cleanup. Suppresses further
                                        // result latching so that the artifact doesn't produce a bad report
                                        // (other clients are still observing the actual game and will report
                                        // correctly).
    GameEventHandler *m_gameEventHandler;

public:
    static void test(int allianceMethod);

    GameMonitor2(GameEventHandler *gameEventHandler, std::uint32_t gameStartsAfterTickCount, std::uint32_t drawGameTicks, bool repairAsymmetricAlliances,
                 bool allowExternalAlliances = true, bool allowExternalDeaths = true);

    // Unfortunately we need to be informed who is host so we can determine who's status packets (ie mapname and maxunits)
    // to pay attention to.  (or otherwise @todo find a way to determine who is host from the network packets themselves)
    virtual void setHostPlayerName(const std::string &playerName);
    virtual void setLocalPlayerName(const std::string& playerName);
    virtual void setPlayerRealName(const std::string &playerName, const std::string &realName);
    virtual std::string getHostPlayerName();
    virtual std::string getLocalPlayerName();
    virtual std::uint32_t getHostDplayId();
    virtual std::uint32_t getLocalPlayerDplayId();

    virtual std::string getMapName() const;
    virtual bool isGameStarted() const;
    virtual bool isGameOver() const;
    virtual const GameResult & getGameResult() const;
    virtual std::set<std::string> getPlayerNames(bool players=true, bool watchers=false) const;
    virtual const PlayerData& getPlayerData(const std::string& name) const;
    virtual const PlayerData& getPlayerData(std::uint32_t dplayId) const;
    virtual std::uint32_t getMostRecentGameTick() const;
    virtual void reset();

    virtual void onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string &playerName, tapacket::DPAddress *tcp, tapacket::DPAddress *udp);
    virtual void onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string &name, tapacket::DPAddress *tcp, tapacket::DPAddress *udp);
    virtual void onDplayDeletePlayer(std::uint32_t dplayId);

    virtual void onTaPacket(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, bool isLocalSource,
        const char* encrypted, int sizeEncrypted,
        const std::vector<tapacket::bytestring>& subpaks);

    virtual void onStatus(
        std::uint32_t sourceDplayId, const std::string &mapName, std::uint16_t maxUnits,
        unsigned playerSlotNumber, int playerSide, bool isWatcher, bool isAI, bool cheats);
    virtual void onChat(std::uint32_t sourceDplayId, const std::string &chat);
    virtual void onAlliance(std::uint32_t subjectDplayId, std::uint32_t objectDplayId, bool isAllied);
    virtual void onTeamSelection(std::uint32_t fromDplayId, int teamNumber);
    virtual void onUnitDied(std::uint32_t sourceDplayId, std::uint16_t unitId);
    virtual void onRejectOther(std::uint32_t sourceDplayId, std::uint32_t rejectedDplayId);
    virtual void onGameTick(std::uint32_t sourceDplayId, std::uint32_t tick);

#ifdef QT_CORE_LIB
    // allyFlags is 10x10 row-major: allyFlags[i*10+j] != 0 => slot i allied with slot j
    // actives: slot occupancy (1 = occupied, 0 = empty)
    // unitCounts: live unit count per slot, from the engine's local view. Elimination is
    //   derived here via the max-seen-then-zero edge latch on PlayerData::maxUnitsSeen.
    // propertyMasks: WATCH=0x40, HUMANPLAYER=0x80, PLAYERCHEATING=0x2000
    //
    // NOTE: per-slot arrays are indexed by TA's local Players[0..9] order — each peer puts
    // itself at slot 0. Resolve players by dplayIds[xslot], not by lobby slot.
    virtual void onExternalPlayerStatus(const QVector<int>& allyFlags, const QVector<int>& actives,
                                        const QVector<int>& unitCounts, const QVector<int>& allyTeams,
                                        const QVector<int>& propertyMasks, const QVector<int>& dplayIds);
#endif

protected:

    // True iff external (shared-mem) death detection is currently load-bearing.
    // Returns false when external was never received OR when the most recent external
    // update is older than EXTERNAL_STATUS_STALENESS_MS — the packet-inference path then
    // resumes so vanilla TA without tadr-ddraw still works.
    virtual bool isExternalDeathsActive() const;

    // return true iff a game ending condition is detected
    // sets winningTeamNumber to the winning team number, or zero if forced draw, or -1 if mutual draw
    virtual bool checkEndGameCondition(int &winningTeamNumber);

    // return <0 if players are not all on same team
    // return winningTeam>0 if players are all on same team
    virtual bool isPlayersAllAllied(const std::set<std::uint32_t> & playerIds, const std::map<std::uint32_t, PlayerData>& playerData, int &teamNumber);

    // set the EndGame tick after which the game will be considered over.
    // can only be set once.  endGameTick=0u will be quietly incremented to endGameTick=1u.
    // return the latched endGameTick
    virtual std::uint32_t latchEndGameTick(std::uint32_t endGameTick);

    // set the game result according to current survivors.  can only be set once
    // fires off m_gameEventHandler->onGameEnded on first call only - ie when result is latched in.
    virtual const GameResult& latchEndGameResult(int winningTeamNumber);

    // returns 0u if not found
    virtual std::uint32_t getPlayerDpidByName(const std::string &name) const;

    // return dplayids who have neither died nor are watchers
    virtual std::set<std::uint32_t> getActivePlayers() const;

    // determine whether a set of players are all allied (ie are all on one team)
    virtual bool isAllied(const std::set<std::uint32_t> &playernums) const;

    // get all players for which alliance is mutal (whether alive or dead)
    virtual std::set<std::uint32_t> getMutualAllies(std::uint32_t playernum, const std::map<std::uint32_t, PlayerData> &playerData) const;
    virtual std::set<std::string> getMutualAllyNames(std::uint32_t playernum, const std::map<std::uint32_t, PlayerData> & playerData) const;

    // based on chat messages "<player1>  allied with player2".
    // not spoofable in-game, but can be spoofed in lobby :(
    // unfortunately, without modifying recorder, I can't see any other way to determine alliances
    // return true if alliances were updated
    virtual bool updateAlliances(std::uint32_t sender, const std::string &chat);

    // works out mutual alliances and assigns team and army numbers to each player
    // in a way that is consistent across all players' demo recordings.
    // The designations will change completely everytime alliances change.
    // we could either lock the alliances and designations at the time the victory condition is detected
    // or we could lock the alliances and designations at launch.
    // Since FAF server logic requires latter, so thats what we'll do
    virtual void updatePlayerArmies();
    virtual void notifyPlayerStatuses();

};
