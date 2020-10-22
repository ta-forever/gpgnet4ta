#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "TADemoParser.h"


class GameMonitor : public TADemo::Parser
{
    struct PlayerData : public TADemo::Player
    {
        PlayerData();

        PlayerData(const TADemo::Player &player);

        std::ostream & print(std::ostream &s);

        std::set<std::uint8_t> allies;
        bool is_dead;                                   // advertised that their commander died, or was rejected by a player
        std::uint32_t tick;                             // serial of last 2C packet
        std::uint32_t dplayid;                          // REJECT commands refer to dplayid so we need to remember this for each player
    };

    bool m_gameStarted;                                 // flag to indicate sufficient activity to consider an actual game occurred
    bool m_cheatsEnabled;
    bool m_suspiciousStatus;                            // some irregularity was encountered, you may want to invalidate this game for tourney / ranking purposes
    std::string m_mapName;
    std::uint16_t m_maxUnits;
    std::string m_lobbyChat;                            // cached here from ExtraSegment for deferred alliance processing once all players are known
    std::map<std::uint8_t, PlayerData> m_players;
    std::set<std::uint8_t> m_lastTeamStanding;          // we'll update this every time someone dies until a winning team is found

public:
    GameMonitor();

    virtual std::string getMapName() const;
    virtual bool isGameStarted() const;
    virtual bool isGameOver() const;
    virtual std::set<std::string> getLastTeamStanding() const;

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

    // check for victory condition and if so, latch m_lastTeamStanding to the winning players
    virtual void checkLastTeamStanding();

    // return player numbers who have neither died nor are watchers
    virtual std::set<std::uint8_t> getActivePlayers() const;

    // determine whether a set of players are all allied (ie are all on one team)
    virtual bool isAllied(const std::set<std::uint8_t> &playerIds) const;

    // based on chat messages "<player1>  allied with player2".
    // not spoofable in-game, but can be spoofed in lobby :(
    // unfortunately, without modifying recorder, I can't see any other way to determine alliances
    virtual void updateAlliances(std::uint8_t sender, const std::string &chat);
};
