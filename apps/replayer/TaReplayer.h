#pragma once

#include "tapacket/TADemoParser.h"
#include "QtCore/qobject.h"
#include <queue>

namespace jdplay {
    class JDPlay;
}

class Replayer : public QObject, public tapacket::DemoParser
{
    Q_OBJECT

    enum class ReplayState
    {
        LOADING_DEMO_PLAYERS,
        LOBBY,
        LAUNCH,
        LOAD,
        PLAY,
        DONE
    };

    struct DemoPlayer : public tapacket::Player
    {
        DemoPlayer(const tapacket::Player& demoPlayer);
        int ordinalId;              // 0 to 9
        std::uint32_t dpId;
        std::uint32_t originalDpId;
        tapacket::bytestring statusPacket;
        std::uint32_t ticks;
    };

    enum class DpPlayerState
    {
        START_SYNC = 0,
        SEND_UNITS = 1,
        WAIT_RECEIVE_UNITS = 2,
        CHECK_ERRORS = 3,
        SEND_ACKS = 4,
        WAIT_ACKS = 5,
        END_SYNC = 6,
        WAIT_GO = 7,
        LOADING = 8,
        PLAYING = 9
    };

    struct DpPlayer
    {
        DpPlayer(std::uint32_t dpid);
        int ordinalId;              // 0 to 9
        std::uint32_t dpid;
        std::uint32_t ticks;
        tapacket::bytestring statusPacket;
        DpPlayerState state;
        int unitSyncSendCount;      //Number of $1a sent to this player.
        int unitSyncNextUnit;       //What unit we should send.
        int unitSyncErrorCount;     //Number of faulty units
        int unitSyncAckCount;       //Number of units the client responded to with crc. 
                                    //(alternatively: responded with crc on) 
        int unitSyncReceiveCount;   //Number of $1a it claims to have recieved 
        bool hasTaken;              //True if this player has taken control over someone.
    };

    struct UnitInfo
    {
        UnitInfo();
        std::uint32_t id;
        std::uint32_t crc;
        std::uint16_t limit;
        bool inUse;
    };

    const std::uint32_t SY_UNIT_ID = 0x92549357;
    const double WALL_TO_GAME_TICK_RATIO = 1.0; // wall clock 33ms, game clock 33ms
    const unsigned NUM_PAKS_TO_PRELOAD = 100u;
    const int UNITS_SYNC_PER_TICK = 100;
    const unsigned AUTO_PAUSE_HOLDOFF_TICKS = 90u;  // 3 secs

    int m_timerId;
    std::shared_ptr<jdplay::JDPlay> m_jdPlay;
    std::uint32_t m_tcpSeq;
    std::uint32_t m_wallClockTicks;
    std::uint32_t m_demoTicks;
    std::uint32_t m_targetTicks;
    double m_targetTicksFractional;
    unsigned m_playBackSpeed;
    ReplayState m_state;
    bool m_isPaused;
    std::uint32_t m_tickLastUserPauseEvent;
    std::istream* m_demoDataStream;
    bool m_launch;

    // multiplier for m_playBackSpeed
    // is reduced in response to depleted m_pendingGamePackets buffer
    // keeps the replay running smoothly when replay catches up to live
    double m_rateControl;

    std::vector<std::uint32_t> m_dpIdsPrealloc;
    tapacket::Header m_header;
    std::vector<std::shared_ptr<DemoPlayer> > m_demoPlayers;
    std::map<std::uint32_t, std::shared_ptr<DemoPlayer> > m_demoPlayersById;// keyed by dpid  @todo do we need this?
    std::map<std::uint32_t, std::shared_ptr<DpPlayer> > m_dpPlayers;  // keyed by dpid
    std::map<std::uint32_t, UnitInfo> m_demoUnitInfo;               // keyed by unit id
    std::vector<const UnitInfo*> m_demoUnitInfoLinear;              // pointers into m_demoUnitInfo
    std::uint32_t m_demoUnitsCrc;
    std::queue<std::pair<tapacket::Packet, std::vector<tapacket::bytestring> > > m_pendingGamePackets; // source player number (NOT dpid) and subpak

public:
    Replayer(std::istream*);

    void hostGame(QString _guid, QString _player, QString _ipaddr);

    virtual void handle(const tapacket::Header& header);
    virtual void handle(const tapacket::Player& player, int n, int ofTotal);
    virtual void handle(const tapacket::ExtraSector& es, int n, int ofTotal) {}
    virtual void handle(const tapacket::PlayerStatusMessage& msg, std::uint32_t dplayid, int n, int ofTotal);
    virtual void handle(const tapacket::UnitData& unitData);
    virtual void handle(const tapacket::Packet& packet, const std::vector<tapacket::bytestring>& unpaked, std::size_t n);

signals:
    void readyToJoin();

private:
    void timerEvent(QTimerEvent* event) override;
    void onLobbySystemMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);
    void onLobbyTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);
    void onLoadingTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);
    void onPlayingTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);

    void send(std::uint32_t fromId, std::uint32_t toId, const tapacket::bytestring& subpak);
    void sendUdp(std::uint32_t fromId, std::uint32_t toId, const tapacket::bytestring& subpak);
    void say(std::uint32_t fromId, const std::string& text);
    void createSonar(std::uint32_t receivingDpId, unsigned number);
    std::shared_ptr<DemoPlayer> getDemoPlayerByOriginalDpId(std::uint32_t originalDpId);

    void onCompletedLoadingDemoPlayers();
    bool doLobby();
    void doLaunch();
    bool doLoad();
    bool doPlay();
    int doSyncWatcher(DpPlayer& dpPlayer);
    void sendPlayerInfos();
    void processUnitData(const tapacket::TUnitData& unitData);
};
