#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "TADemoParser.h"


struct GameResult
{
    std::map<int /* armynumber */, int /* score */> resultByArmy;
    std::map < std::string, int > armyNumbersByPlayerName;

    void print(std::ostream& os) const
    {
        for (const auto& player : armyNumbersByPlayerName)
        {
            os << player.first << ": army=" << player.second << ", score=" << resultByArmy.at(player.second) << std::endl;
        }
    }
};

struct PlayerData : public TADemo::Player
{
    PlayerData();

    PlayerData(const TADemo::Player& player);

    std::ostream& print(std::ostream& s);

    std::set<std::uint8_t> allies;
    bool is_dead;                                   // advertised that their commander died, or was rejected by a player
    std::uint32_t tick;                             // serial of last 2C packet
    std::uint32_t dplayid;                          // REJECT commands refer to dplayid so we need to remember this for each player
    int armyNumber;                                 // for reporting which team this player is allied with
};

class GameMonitor : public TADemo::Parser
{
    bool m_gameStarted;                                 // flag to indicate sufficient activity to consider an actual game occurred
    bool m_cheatsEnabled;
    bool m_suspiciousStatus;                            // some irregularity was encountered, you may want to invalidate this game for tourney / ranking purposes
    std::string m_mapName;
    std::uint16_t m_maxUnits;
    std::string m_lobbyChat;                            // cached here from ExtraSegment for deferred alliance processing once all players are known
    std::map<std::uint8_t, PlayerData> m_players;       // keyed by PlayerData::number
    GameResult m_gameResult;                            // empty until latched onto the first encountered victory condition

public:
    GameMonitor();

    virtual std::string getMapName() const;
    virtual bool isGameStarted() const;
    virtual bool isGameOver() const;
    virtual const GameResult & getGameResult() const;
    virtual std::set<std::string> getPlayerNames(bool players=true, bool watchers=false) const;
    virtual const PlayerData& getPlayerData(const std::string& name) const;
    virtual void reset();

    virtual void handle(const TADemo::Header &header);
    virtual void handle(const TADemo::ExtraSector &es, int n, int ofTotal);
    virtual void handle(const TADemo::Player &player, int n, int ofTotal);
    virtual void handle(const TADemo::PlayerStatusMessage &msg, std::uint32_t dplayid, int n, int ofTotal);
    virtual void handle(const TADemo::UnitData &unitData);
    virtual void handle(const TADemo::Packet &packet, const std::vector<TADemo::bytestring> &unpacked, std::size_t n);

private:

    // returns 0u if not found
    std::uint8_t getPlayerByName(const std::string &name) const;

    // returns 0u if not found
    std::uint8_t getPlayerByDplayId(std::uint32_t dplayid) const;

    // check for victory condition and if so, latch on m_gameResult
    virtual void checkGameResult();

    // return player numbers who have neither died nor are watchers
    virtual std::set<std::uint8_t> getActivePlayers() const;

    // determine whether a set of players are all allied (ie are all on one team)
    virtual bool isAllied(const std::set<std::uint8_t> &playernums) const;

    // get all players for which alliance is mutal (whether alive or dead)
    virtual std::set<std::uint8_t> getMutualAllies(std::uint8_t playernum) const;

    // based on chat messages "<player1>  allied with player2".
    // not spoofable in-game, but can be spoofed in lobby :(
    // unfortunately, without modifying recorder, I can't see any other way to determine alliances
    virtual void updateAlliances(std::uint8_t sender, const std::string &chat);

    // works out mutual alliances and assigns team (army) numbers to each player in a way thats independent of player number
    // so that it will work the same across all players' instances.
    // army determinations will change completely everytime alliances change
    // so just read it out at the time that victory/defeat is detected.
    virtual void updatePlayerArmies();

    virtual void updateGameResult(int winningArmyNumber /* or zero */);

};
