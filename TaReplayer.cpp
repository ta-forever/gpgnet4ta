#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qdir.h>
#include <QtCore/qtemporaryfile.h>
#include <QtCore/qthread.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>
#include <set>
#include <queue>
#include <algorithm>

#include "tademo/TADemoParser.h"
#include "tademo/HexDump.h"
#include "jdplay/JDPlay.h"
#include "Logger.h"

#include <QtCore/qobject.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

class Replayer : public QObject, public TADemo::Parser
{
    enum class ReplayState
    {
        LOADING_DEMO_PLAYERS,
        LOBBY,
        LAUNCH,
        LOAD,
        PLAY,
        DONE
    };

    struct DemoPlayer : public TADemo::Player
    {
        DemoPlayer(const TADemo::Player& demoPlayer);
        int ordinalId;              // 0 to 9
        std::uint32_t dpId;
        std::uint32_t originalDpId;
        TADemo::bytestring statusPacket;
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
        TADemo::bytestring statusPacket;
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
    const double WALL_TO_GAME_TICK_RATIO = 1.0; // wall clock 100ms, game clock 33ms
    const int TIMING_SMOOTHING_FACTOR = 3;
    const unsigned NUM_PAKS_TO_PRELOAD = 100u;
    const int UNITS_SYNC_PER_TICK = 100;

    int m_timerId;
    std::shared_ptr<JDPlay> m_jdPlay;
    std::uint32_t m_tcpSeq;
    std::uint32_t m_wallClockTicks;
    std::uint32_t m_demoTicks;
    std::uint32_t m_targetTicks;
    double m_targetTicksFractional;
    unsigned m_playBackSpeed;
    ReplayState m_state;
    bool m_isPaused;
    std::istream *m_demoDataStream;

    std::vector<std::uint32_t> m_dpIdsPrealloc;
    TADemo::Header m_header;
    std::vector<std::shared_ptr<DemoPlayer> > m_demoPlayers;
    std::map<std::uint32_t, std::shared_ptr<DemoPlayer> > m_demoPlayersById;// keyed by dpid  @todo do we need this?
    std::map<std::uint32_t, std::shared_ptr<DpPlayer> > m_dpPlayers;  // keyed by dpid
    std::map<std::uint32_t, UnitInfo> m_demoUnitInfo;               // keyed by unit id
    std::vector<const UnitInfo*> m_demoUnitInfoLinear;              // pointers into m_demoUnitInfo
    std::uint32_t m_demoUnitsCrc;
    std::queue<std::pair<TADemo::Packet, std::vector<TADemo::bytestring> > > m_pendingGamePackets; // source player number (NOT dpid) and subpak

public:
    Replayer(std::istream*);

    void hostGame(QString _guid, QString _player, QString _ipaddr);

    virtual void handle(const TADemo::Header& header);
    virtual void handle(const TADemo::Player& player, int n, int ofTotal);
    virtual void handle(const TADemo::ExtraSector& es, int n, int ofTotal) {}
    virtual void handle(const TADemo::PlayerStatusMessage& msg, std::uint32_t dplayid, int n, int ofTotal);
    virtual void handle(const TADemo::UnitData& unitData);
    virtual void handle(const TADemo::Packet& packet, const std::vector<TADemo::bytestring>& unpaked, std::size_t n);

private:
    void timerEvent(QTimerEvent* event) override;
    void onLobbySystemMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);
    void onLobbyTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);
    void onLoadingTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);
    void onPlayingTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize);


    void send(std::uint32_t fromId, std::uint32_t toId, TADemo::bytestring& subpak);
    void sendUdp(std::uint32_t fromId, std::uint32_t toId, TADemo::bytestring& subpak);
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
    void processUnitData(const TADemo::TUnitData& unitData);
};

Replayer::Replayer(std::istream *demoDataStream) :
    m_timerId(startTimer(100, Qt::TimerType::PreciseTimer)),
    m_tcpSeq(0xfffffffe),
    m_wallClockTicks(0u),
    m_demoTicks(0u),
    m_targetTicks(0u),
    m_targetTicksFractional(0.0),
    m_playBackSpeed(10u),
    m_state(ReplayState::LOADING_DEMO_PLAYERS),
    m_isPaused(false),
    m_demoDataStream(demoDataStream)
{ 
    this->parse(demoDataStream, NUM_PAKS_TO_PRELOAD);
}

Replayer::DemoPlayer::DemoPlayer(const TADemo::Player& player):
    TADemo::Player(player),
    dpId(0u),
    originalDpId(0u),
    ticks(0u)
{ }

Replayer::DpPlayer::DpPlayer(std::uint32_t dpid) :
    dpid(dpid),
    ticks(0u),
    state(DpPlayerState::START_SYNC),
    unitSyncSendCount(0),
    unitSyncNextUnit(0),
    unitSyncErrorCount(0),
    unitSyncAckCount(0),
    unitSyncReceiveCount(0),
    hasTaken(false)
{ }

Replayer::UnitInfo::UnitInfo():
    id(0u),
    crc(0u),
    limit(0u),
    inUse(false)
{ }

void Replayer::send(std::uint32_t fromId, std::uint32_t toId, TADemo::bytestring& subpak)
{
    TADemo::bytestring bs = TADemo::TPacket::trivialSmartpak(subpak, toId == 0 ? m_tcpSeq-- : 0xffffffff);
    //TADemo::HexDump(bs.data(), bs.size(), std::cout);
    //bs = TADemo::TPacket::compress(bs);
    TADemo::TPacket::encrypt(bs);
    m_jdPlay->dpSend(fromId, toId, 1, (void*)bs.data(), bs.size());
}

void Replayer::sendUdp(std::uint32_t fromId, std::uint32_t toId, TADemo::bytestring& subpak)
{
    TADemo::bytestring bs = TADemo::TPacket::trivialSmartpak(subpak, 0);
    //TADemo::HexDump(bs.data(), bs.size(), std::cout);
    //bs = TADemo::TPacket::compress(bs);
    TADemo::TPacket::encrypt(bs);
    m_jdPlay->dpSend(fromId, toId, 0, (void*)bs.data(), bs.size());
}

void Replayer::say(std::uint32_t fromId, const std::string& text)
{
    this->sendUdp(fromId, 0, TADemo::TPacket::createChatSubpacket(text));
}

std::shared_ptr<Replayer::DemoPlayer> Replayer::getDemoPlayerByOriginalDpId(std::uint32_t originalDpId)
{
    for (auto demoPlayer : m_demoPlayers)
    {
        if (demoPlayer->originalDpId == originalDpId)
        {
            return demoPlayer;
        }
    }
    return std::shared_ptr<DemoPlayer>();
}

void Replayer::hostGame(QString _guid, QString _player, QString _ipaddr)
{
    qInfo() << "[Replayer::handle] player=" << _player;
    std::string guid = _guid.toStdString();
    std::string player = _player.toStdString();
    std::string ipaddr = _ipaddr.toStdString();

    m_jdPlay.reset(new JDPlay(player.c_str(), 3, false));
    if (!m_jdPlay->initialize(guid.c_str(), ipaddr.c_str(), true, 10))
    {
        qWarning() << "[Replayer::hostGame] jdplay failed to initialise!";
        m_jdPlay.reset();
        return;
    }

    for (int m = 0; m < 100; ++m)
    {
        qInfo() << "[Replayer::hostGame] opening session";
        // 1325465696 - 0 - 786442 - 16974074 = Yes, 0, No, Blk, 1200 1000 1 / 10 Open
        if (!m_jdPlay->dpOpen(100, "TAF Replayer", "SHERWOOD", 1753284736, 4, 655370, 16974324))
        {
            qWarning() << "[Replayer::hostGame] jdplay failed to open!";
            m_jdPlay.reset();
            return;
        }

        for (int n = 0; n < 20; ++n)
        {
            std::ostringstream ss;
            ss << "dpIdPrealloc" << n;
            std::uint32_t dpid = m_jdPlay->dpCreatePlayer(ss.str().c_str());
            m_dpIdsPrealloc.push_back(dpid);
            qInfo() << ss.str().c_str() << '/' << dpid;
        }

        if (m_dpIdsPrealloc.front() > m_dpIdsPrealloc.back())
        {
            qWarning() << "[Replayer::hostGame] bad dpIdPrealloc!";
            while (!m_dpIdsPrealloc.empty())
            {
                m_jdPlay->dpDestroyPlayer(m_dpIdsPrealloc.back());
                m_dpIdsPrealloc.pop_back();
            }
            m_jdPlay->dpClose();
        }
        if (!m_dpIdsPrealloc.empty())
        {
            break;
        }
    }
}

void Replayer::handle(const TADemo::Header& header)
{
    m_header = header;
}

void Replayer::handle(const TADemo::Player& _player, int n, int ofTotal)
{
    std::shared_ptr<DemoPlayer> demoPlayer(new DemoPlayer(_player));
    m_demoPlayers.push_back(demoPlayer);
}

void Replayer::handle(const TADemo::PlayerStatusMessage& msg, std::uint32_t dplayid, int n, int ofTotal)
{
    m_demoPlayers[n]->ordinalId = n;
    m_demoPlayers[n]->originalDpId = dplayid;
    m_demoPlayers[n]->statusPacket = msg.statusMessage;
}

void Replayer::handle(const TADemo::UnitData& unitData)
{
    const std::uint8_t* ptr = unitData.unitData.data();
    const std::uint8_t* end = ptr + unitData.unitData.size();
    unsigned subpakLen = TADemo::TPacket::getExpectedSubPacketSize(ptr, end - ptr);
    while (ptr < end && subpakLen > 0u)
    {
        TADemo::bytestring bs(ptr, subpakLen);
        processUnitData(TADemo::TUnitData(bs));
        ptr += subpakLen;
    }

    std::vector<std::uint32_t> unitsNotInUse;
    for (auto it = m_demoUnitInfo.begin(); it != m_demoUnitInfo.end(); ++it)
    {
        if (!it->second.inUse)
        {
            unitsNotInUse.push_back(it->first);
        }
    }
    for (auto it = unitsNotInUse.begin(); it != unitsNotInUse.end(); ++it)
    {
        m_demoUnitInfo.erase(*it);
    }
    
    UnitInfo& syUnit = m_demoUnitInfo[SY_UNIT_ID];
    syUnit.id = SY_UNIT_ID;
    syUnit.crc = 0u;
    syUnit.limit = 100u;
    syUnit.inUse = true;

    if (false)
    {
        std::ofstream ofs("c:\\temp\\taf_units.txt", std::ios::out);
        ofs << "id,crc,limit,inuse" << std::endl;
        for (auto it = m_demoUnitInfo.begin(); it != m_demoUnitInfo.end(); ++it)
        {
            ofs << it->second.id << ',' << it->second.crc << ',' << it->second.limit << ',' << (it->second.inUse ? "TRUE" : "FALSE") << std::endl;
        }
    }

    m_demoUnitInfoLinear.reserve(m_demoUnitInfo.size());
    for (const auto& unitInfo : m_demoUnitInfo)
    {
        m_demoUnitInfoLinear.push_back(&unitInfo.second);
    }
}

void Replayer::processUnitData(const TADemo::TUnitData& unitData)
{
    if (unitData.pktid == TADemo::SubPacketCode::GIVE_UNIT_14 || unitData.pktid == TADemo::SubPacketCode::TEAM_24)
    {
        // nop
    }

    else if (unitData.sub == 0x03)
    {
        UnitInfo& unitInfo = m_demoUnitInfo[unitData.id];
        unitInfo.id = unitData.id;
        unitInfo.limit = unitData.u.statusAndLimit[1];
        unitInfo.inUse = unitData.u.statusAndLimit[0] == 0x0101;
    }

    else if (unitData.sub == 0x02)
    {
        UnitInfo& unitInfo = m_demoUnitInfo[unitData.id];
        unitInfo.id = unitData.id;
        unitInfo.crc = unitData.u.crc;
    }

    else if (unitData.id == 0xffffffff)
    {
        m_demoUnitsCrc = unitData.fill;
    }
}

void Replayer::handle(const TADemo::Packet& packet, const std::vector<TADemo::bytestring>& unpaked, std::size_t n)
{
    //TADemo::HexDump(packet.data.data(), packet.data.size(), std::cout);
    this->m_pendingGamePackets.push(std::make_pair(packet, unpaked));
}

void Replayer::timerEvent(QTimerEvent* event)
{
    ++m_wallClockTicks;

    switch (m_state) {
    case ReplayState::LOADING_DEMO_PLAYERS:
        qInfo() << "LOADING_DEMO_PLAYERS";
        if (m_demoPlayers.size() > 0u && m_demoPlayers.back()->originalDpId != 0u)
        {
            onCompletedLoadingDemoPlayers();
            m_state = ReplayState::LOBBY;
            qInfo() << "LOBBY";
        }
        break;

    case ReplayState::LOBBY:
        if (doLobby())
        {
            m_state = ReplayState::LAUNCH;
            qInfo() << "LAUNCH";
        }
        break;

    case ReplayState::LAUNCH:
        doLaunch();
        m_state = ReplayState::LOAD;
        qInfo() << "LOAD";
        m_wallClockTicks = 0u;   // we're going to use it to indicate load progress
        //break;

    case ReplayState::LOAD:
        if (doLoad())
        {
            m_state = ReplayState::PLAY;
            qInfo() << "PLAY";
            m_wallClockTicks = 0u;
            killTimer(m_timerId);
            m_timerId = startTimer(33, Qt::TimerType::PreciseTimer);
        }
        break;

    case ReplayState::PLAY:
        if (doPlay())
        {
            m_state = ReplayState::DONE;
            qInfo() << "DONE";
        }
        break;

    case ReplayState::DONE:
        break;
    };
}

bool Replayer::doLobby()
{
    std::uint8_t buffer[10000];
    std::uint32_t fromId, toId;
    std::uint32_t bytesReceived = sizeof(buffer);

    while (m_jdPlay->dpReceive(buffer, bytesReceived, fromId, toId))
    {
        if (fromId == DPID_SYSMSG && toId == m_demoPlayers.front()->dpId)
        {
            onLobbySystemMessage(fromId, toId, buffer, bytesReceived);
            return false;
        }

        else {
            onLobbyTaMessage(fromId, toId, buffer, bytesReceived);
        }
    }

    int goCount = 0;
    if (m_wallClockTicks % 10u == 0u)
    {
        m_wallClockTicks = 0u;
        sendPlayerInfos();
        for (auto& dpPlayer : m_dpPlayers)
        {
            goCount += doSyncWatcher(*dpPlayer.second);
        }
    }

    bool launch = !m_dpPlayers.empty() && goCount == m_dpPlayers.size();
    if (launch)
    {
        for (const auto& demoPlayer : m_demoPlayers)
        {
            TADemo::TPlayerInfo demoPlayerStatus(demoPlayer->statusPacket);
            demoPlayerStatus.setDpId(demoPlayer->dpId);
            demoPlayerStatus.setAllowWatch(true);
            demoPlayerStatus.setPermLos(true);
            demoPlayerStatus.setCheat(true);
            TADemo::bytestring bs = demoPlayerStatus.asSubPacket();
            //std::cout << "[replayer::doSyncWatcher] clicked go. demoplayer=" << demoPlayer->dpId << std::endl;
            //TADemo::HexDump(demoPlayer->statusPacket.data(), demoPlayer->statusPacket.size(), std::cout);
            //TADemo::HexDump(bs.data(), bs.size(), std::cout);
            this->send(demoPlayer->dpId, 0, demoPlayerStatus.asSubPacket());
        }
    }
    return launch;
}

void Replayer::sendPlayerInfos()
{
    TADemo::TIdent2 id2;
    for (std::size_t n = 0u; n < m_demoPlayers.size(); ++n)
    {
        id2.dpids[n] = m_demoPlayers[n]->dpId;
    }
    TADemo::bytestring bs = id2.asSubPacket();
    //std::cout << std::dec << "[Replayer::sendPlayerInfos] TIdent2: drones=["
    //    << m_demoPlayers[0]->dpId << ','
    //    << m_demoPlayers[1]->dpId << ','
    //    << m_demoPlayers[2]->dpId << ','
    //    << m_demoPlayers[3]->dpId << ']' << std::endl;
    //TADemo::HexDump(bs.data(), bs.size(), std::cout);
    this->send(m_demoPlayers[0]->dpId, 0, bs);

    int ordinalId = 0;
    for (const auto& demoPlayer : m_demoPlayers)
    {
        demoPlayer->ordinalId = ordinalId++;
        TADemo::bytestring bs = TADemo::TIdent3(demoPlayer->dpId, demoPlayer->ordinalId).asSubPacket();
        //std::cout << std::dec << "[Replayer::sendPlayerInfos] TIdent3: drone ordinal,dpid,num=" << demoPlayer->ordinalId << ',' << demoPlayer->dpId << ',' << demoPlayer->number << std::endl;
        //TADemo::HexDump(bs.data(), bs.size(), std::cout);
        this->send(m_demoPlayers[0]->dpId, 0, bs);
    }
    for (const auto& dpPlayer : m_dpPlayers)
    {
        dpPlayer.second->ordinalId = ordinalId++;
        TADemo::bytestring bs = TADemo::TIdent3(dpPlayer.first, dpPlayer.second->ordinalId).asSubPacket();
        //std::cout << std::dec << "[Replayer::sendPlayerInfos] TIdent3: player key,ordinal,dpid=" << dpPlayer.first << ',' << dpPlayer.second->ordinalId << ',' << dpPlayer.second->dpid << std::endl;
        //TADemo::HexDump(bs.data(), bs.size(), std::cout);
        this->send(m_demoPlayers[0]->dpId, 0, bs);
    }
    for (const auto& demoPlayer : m_demoPlayers)
    {
        TADemo::TPlayerInfo playerInfo(demoPlayer->statusPacket);
        playerInfo.setDpId(demoPlayer->dpId);
        playerInfo.setInternalVersion(0u);
        playerInfo.setAllowWatch(true);
        playerInfo.setCheat(true);
        TADemo::bytestring bs = playerInfo.asSubPacket();
        //std::cout << std::dec << "[Replayer::sendPlayerInfos] TPlayerInfo: drone dpid:" << demoPlayer->dpId << std::endl;
        //TADemo::HexDump(bs.data(), bs.size(), std::cout);
        this->send(m_demoPlayers[0]->dpId, 0, bs);
    }
    //std::cout << "[Replayer::sendPlayerInfos] -------------" << std::endl;
}

void Replayer::onCompletedLoadingDemoPlayers()
{
    std::sort(m_dpIdsPrealloc.begin(), m_dpIdsPrealloc.end());
    std::sort(m_demoPlayers.begin(), m_demoPlayers.end(), [](std::shared_ptr<DemoPlayer> a, std::shared_ptr<DemoPlayer> b)
    {
        return a->originalDpId < b->originalDpId;
    });

    for (std::size_t n = 0u; n < m_demoPlayers.size(); ++n)
    {
        std::uint32_t dpid = m_demoPlayers[n]->dpId = m_dpIdsPrealloc[n];
        m_demoPlayersById[dpid] = m_demoPlayers[n];
        m_jdPlay->dpSetPlayerName(dpid, m_demoPlayers[n]->name.c_str());
        qInfo() << "[Replayer] name:" << m_demoPlayers[n]->name.c_str() << "originalDpId:" << m_demoPlayers[n]->originalDpId << "dpId:" << dpid;
    }

    std::sort(m_demoPlayers.begin(), m_demoPlayers.end(), [](std::shared_ptr<DemoPlayer> a, std::shared_ptr<DemoPlayer> b)
    {
        return a->ordinalId < b->ordinalId;
    });

    while (m_dpIdsPrealloc.size() > m_demoPlayers.size())
    {
        m_jdPlay->dpDestroyPlayer(m_dpIdsPrealloc.back());
        m_dpIdsPrealloc.pop_back();
    }

    std::cout << "\ndemoplayers:\n";
    for (const auto &demoPlayer: m_demoPlayers)
    {
        std::cout << demoPlayer->name << ":\tnumber=" << int(demoPlayer->number) << ", oid=" << demoPlayer->originalDpId << ", dpid=" << demoPlayer->dpId << std::endl;
    }
    m_jdPlay->dpEnumPlayers();
}


void Replayer::onLobbySystemMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* payload, int payloadSize)
{
    const DPMSG_GENERIC* dpMsgGeneric = (const DPMSG_GENERIC*)payload;
    switch (dpMsgGeneric->dwType) {
    case DPSYS_CREATEPLAYERORGROUP:
    {
        const DPMSG_CREATEPLAYERORGROUP* dpMsgCreatePlayerOrGroup = (const DPMSG_CREATEPLAYERORGROUP*)payload;
        if (!m_demoPlayersById.count(dpMsgCreatePlayerOrGroup->dpId))
        {
            qInfo() << "[Replayer::timerEvent] DPMSG dpPlayer joined:" << dpMsgCreatePlayerOrGroup->dpnName.lpszShortNameA << '/' << dpMsgCreatePlayerOrGroup->dpId;
            m_dpPlayers[dpMsgCreatePlayerOrGroup->dpId].reset(new Replayer::DpPlayer(dpMsgCreatePlayerOrGroup->dpId));
        }
        break;
    }
    case DPSYS_DESTROYPLAYERORGROUP:
    {
        const DPMSG_DESTROYPLAYERORGROUP* dpMsgDestroyPlayerOrGroup = (const DPMSG_DESTROYPLAYERORGROUP*)payload;
        if (!m_demoPlayersById.count(dpMsgDestroyPlayerOrGroup->dpId))
        {
            qInfo() << "[Replayer::timerEvent] DPMSG dpPlayer left:" << dpMsgDestroyPlayerOrGroup->dpnName.lpszShortNameA << '/' << dpMsgDestroyPlayerOrGroup->dpId;
            m_dpPlayers.erase(dpMsgDestroyPlayerOrGroup->dpId);
        }
        break;
    }
    case DPSYS_SETSESSIONDESC:
    {
        qInfo() << "[Replayer::timerEvent] DPMSG session descrioption";
        break;
    }
    default:
        qWarning() << "[Replayer::timerEvent] DPMSG unhandled type";
    };
}

void Replayer::onLobbyTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize)
{
    TADemo::bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        std::uint16_t checksum[2];
        TADemo::TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            // not for us
            return;
        }
    }

    if (TADemo::PacketCode(payload[0]) == TADemo::PacketCode::COMPRESSED)
    {
        payload = TADemo::TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[Replayer::onLobbyTaMessage] decompression ran out of bytes!";
        }
    }

    std::vector<TADemo::bytestring> subpaks = TADemo::TPacket::unsmartpak(payload, true, true);
    for (const TADemo::bytestring& s : subpaks)
    {
        unsigned expectedSize = TADemo::TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[Replayer::onLobbyTaMessage] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16);
            std::ostringstream ss;
            ss << "  _payload:\n";
            TADemo::HexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            continue;
        }

        switch (TADemo::SubPacketCode(s[0]))
        {
        case TADemo::SubPacketCode::PING_02:
        {
            TADemo::TPing ping(s);
            //qInfo() << "[Replayer::parseTaPacket] ping. from=" << ping.from << "id=" << ping.id << "value=" << ping.value;
            ping.value = 1000000u + std::uint32_t(rand()) % 1000000u;
            this->send(otherDplayId, sourceDplayId, ping.asSubPacket());
            break;
        }
        case TADemo::SubPacketCode::UNIT_DATA_1A:
        {
            //qInfo() << "[Replayer::onLobbyTaMessage] unit types sync";
            auto itWatcher = m_dpPlayers.find(sourceDplayId);
            if (itWatcher != m_dpPlayers.end())
            {
                TADemo::TUnitData unitData(s);
                std::uint32_t unitInfoCrc = 0u;
                if (unitData.sub == 0x02)
                {
                    auto itUnitInfo = m_demoUnitInfo.find(unitData.id);
                    if (itUnitInfo != m_demoUnitInfo.end())
                    {
                        unitInfoCrc = itUnitInfo->second.crc;
                        if (itUnitInfo->second.crc != unitData.u.crc && unitData.id != SY_UNIT_ID)
                        {
                            ++itWatcher->second->unitSyncErrorCount;
                        }
                        else
                        {
                            ++itWatcher->second->unitSyncAckCount;
                        }
                    }
                }
                else if (unitData.sub == 0x04)
                {
                    itWatcher->second->unitSyncReceiveCount = unitData.u.statusAndLimit[0];
                }

                qInfo() << "[Replayer::onLobbyTaMessage] unitData: id,sub,crc,crc,status=" << unitData.id << ',' << (int)unitData.sub << ',' << unitData.u.crc << ',' << unitInfoCrc << ',' << unitData.u.statusAndLimit[0];
            }
            break;
        }
        case TADemo::SubPacketCode::PLAYER_INFO_20:
        {
            auto itWatcher = m_dpPlayers.find(sourceDplayId);
            if (itWatcher != m_dpPlayers.end() && !m_demoPlayersById.count(sourceDplayId))
            {
                itWatcher->second->statusPacket = s;
            }
            break;
        }
        default:
            break;
        }
    }
}

int Replayer::doSyncWatcher(DpPlayer& dpPlayer)
{
    std::uint32_t hostDpId = m_demoPlayers[0]->dpId;
    std::uint32_t dpPlayerId = dpPlayer.dpid;
    switch (dpPlayer.state)
    {
    case DpPlayerState::START_SYNC:
    {
        qInfo() << "[Replayer::doSyncWatcher] START_SYNC, dpid,m_demoUnitInfo,m_demoUnitInfoLinear=" << dpPlayerId << ',' << m_demoUnitInfo.size() << ',' << m_demoUnitInfoLinear.size();
        dpPlayer.unitSyncNextUnit = 0u;
        dpPlayer.unitSyncSendCount = dpPlayer.unitSyncReceiveCount;
        this->send(hostDpId, dpPlayer.dpid, TADemo::TPacket::createHostMigrationSubpacket(dpPlayer.ordinalId));

        // request remote to send unit data
        this->send(hostDpId, dpPlayer.dpid, TADemo::TUnitData().asSubPacket());
        ++dpPlayer.unitSyncSendCount;
        dpPlayer.state = DpPlayerState::SEND_UNITS;
        break;
    }
    case DpPlayerState::SEND_UNITS:
    {
        for (int n = 0; n < UNITS_SYNC_PER_TICK; ++n)
        {
            const UnitInfo* unitInfo = m_demoUnitInfoLinear[dpPlayer.unitSyncNextUnit];
            TADemo::TUnitData msg(unitInfo->id, unitInfo->limit, true);
            qInfo() << "[Replayer::doSyncWatcher] SEND_UNITS" << dpPlayerId << ": n,id,limit,inUse="
                << dpPlayer.unitSyncNextUnit << ',' << unitInfo->id << ',' << unitInfo->limit << ',' << unitInfo->inUse;
            this->send(hostDpId, dpPlayer.dpid, msg.asSubPacket());
            ++dpPlayer.unitSyncNextUnit;
            ++dpPlayer.unitSyncSendCount;
            if (dpPlayer.unitSyncNextUnit == m_demoUnitInfoLinear.size())
            {
                std::ostringstream ss;
                ss << "*** Sent all units - " << dpPlayer.unitSyncSendCount;
                this->say(hostDpId, ss.str());
                dpPlayer.unitSyncNextUnit = 0;
                dpPlayer.state = DpPlayerState::WAIT_RECEIVE_UNITS;
                break;
            }
        }
        break;
    }
    case DpPlayerState::WAIT_RECEIVE_UNITS:
    {
        qInfo() << "[Replayer::doSyncWatcher] WAIT_RECEIVE_UNITS" << dpPlayerId << ": unitSyncNextUnit,unitSyncReceiveCount=" << dpPlayer.unitSyncNextUnit << ',' << dpPlayer.unitSyncReceiveCount;
        ++dpPlayer.unitSyncNextUnit;
        if (dpPlayer.unitSyncReceiveCount+1 >= dpPlayer.unitSyncSendCount)
        {
            dpPlayer.unitSyncSendCount = dpPlayer.unitSyncReceiveCount;
            dpPlayer.state = DpPlayerState::CHECK_ERRORS;
        }
        if (dpPlayer.unitSyncNextUnit > 10u)
        {
            std::ostringstream ss;
            ss << "*** Current ack status: " << dpPlayer.unitSyncReceiveCount << " of " << dpPlayer.unitSyncSendCount;
            this->say(hostDpId, ss.str());
            this->say(hostDpId, "*** Attempting resync");
            dpPlayer.state = DpPlayerState::START_SYNC;
        }
        break;
    }
    case DpPlayerState::CHECK_ERRORS:
    {
        qInfo() << "[replayer::doSyncWatcher] CHECK_ERRORS" << dpPlayerId << ": errorCount,ackCount=" << dpPlayer.unitSyncErrorCount << ',' << dpPlayer.unitSyncAckCount;
        if (dpPlayer.unitSyncErrorCount != 0u)
        {
            std::ostringstream ss;
            ss << "*** You have CRC errors on " << dpPlayer.unitSyncErrorCount << " units!";
            this->say(hostDpId, ss.str());
        }
        if (dpPlayer.unitSyncAckCount < m_demoUnitInfo.size())
        {
            std::ostringstream ss;
            ss << "*** You are missing " << m_demoUnitInfo.size() - dpPlayer.unitSyncAckCount << " of " << m_demoUnitInfo.size() << " units!";
            this->say(hostDpId, ss.str());
        }

        dpPlayer.state = DpPlayerState::SEND_ACKS;
        dpPlayer.unitSyncNextUnit = 0u;
        break;
    }
    case DpPlayerState::SEND_ACKS:
    {
        qInfo() << "[replayer::doSyncWatcher] SEND_ACKS" << dpPlayerId;
        for (std::size_t n = 0u; n < UNITS_SYNC_PER_TICK; ++n)
        {
            const UnitInfo& unitInfo = *m_demoUnitInfoLinear[dpPlayer.unitSyncNextUnit];
            TADemo::TUnitData unitData(unitInfo.id, unitInfo.limit, true);
            this->send(hostDpId, dpPlayer.dpid, unitData.asSubPacket());
            ++dpPlayer.unitSyncNextUnit;
            ++dpPlayer.unitSyncSendCount;
            if (dpPlayer.unitSyncNextUnit == m_demoUnitInfo.size())
            {
                std::ostringstream ss;
                ss << "*** Sent ack on all units - " << dpPlayer.unitSyncSendCount;
                this->say(hostDpId, ss.str());
                dpPlayer.unitSyncNextUnit = 0u;
                dpPlayer.state = DpPlayerState::WAIT_ACKS;
                dpPlayer.unitSyncNextUnit = 0u;
            }
        }
        break;
    }
    case DpPlayerState::WAIT_ACKS:
    {
        qInfo() << "[replayer::doSyncWatcher] WAIT_ACKS" << ": unitSyncNextUnit=" << dpPlayer.unitSyncNextUnit;
        ++dpPlayer.unitSyncNextUnit;
        if (dpPlayer.unitSyncReceiveCount == dpPlayer.unitSyncSendCount)
        {
            dpPlayer.state = DpPlayerState::END_SYNC;
        }
        if (dpPlayer.unitSyncNextUnit > 20u)
        {
            std::ostringstream ss;
            ss << "*** Current ack status: " << dpPlayer.unitSyncReceiveCount << " of " << dpPlayer.unitSyncSendCount;
            this->say(hostDpId, ss.str());
            this->say(hostDpId, "*** Attempting resync 2");
            dpPlayer.state = DpPlayerState::SEND_ACKS;
            dpPlayer.unitSyncSendCount = m_demoUnitInfo.size() + 1u;
        }
        break;
    }
    case DpPlayerState::END_SYNC:
    {
        qInfo() << "[replayer::doSyncWatcher] END_SYNC" << dpPlayerId;
        this->say(hostDpId, "*** Unit sync is complete");
        dpPlayer.state = DpPlayerState::WAIT_GO;
        break;
    }
    case DpPlayerState::WAIT_GO:
        qInfo() << "[replayer::doSyncWatcher] WAIT_GO" << dpPlayerId;
        TADemo::TPlayerInfo playerInfo(dpPlayer.statusPacket);
        if (playerInfo.isClickedIn())
        {
            return 1;
        }
        break;
    }

    return 0;
}

void Replayer::doLaunch()
{
    for (const auto& demoPlayer : m_demoPlayers)
    {
        TADemo::TIdent3 id3(demoPlayer->dpId, demoPlayer->ordinalId);
        this->send(m_demoPlayers[0]->dpId, 0, id3.asSubPacket());
    }

    for (const auto& dpPlayer : m_dpPlayers)
    {
        TADemo::TIdent3 id3(dpPlayer.first, dpPlayer.second->ordinalId);
        this->send(m_demoPlayers[0]->dpId, 0, id3.asSubPacket());
        dpPlayer.second->state = DpPlayerState::LOADING;
    }

    std::uint32_t user1, user2, user3, user4;
    m_jdPlay->dpGetSession(user1, user2, user3, user4);
    user1 = (user1 & 0xff00ffff) | 0x00320000;
    m_jdPlay->dpSetSession(user1, user2, user3, user4);

    static const std::uint8_t replayerServerMsg[] = { std::uint8_t(TADemo::SubPacketCode::REPLAYER_SERVER_FA), 0x06 };
    static const std::uint8_t loadingStartedMsg[] = { std::uint8_t(TADemo::SubPacketCode::LOADING_STARTED_08), 0x06 };
    this->send(m_demoPlayers[0]->dpId, 0, TADemo::bytestring(replayerServerMsg, sizeof(replayerServerMsg)));
    this->send(m_demoPlayers[0]->dpId, 0, TADemo::bytestring(loadingStartedMsg, sizeof(loadingStartedMsg)));
}

bool Replayer::doLoad()
{
    std::uint8_t buffer[10000];
    std::uint32_t fromId, toId;
    std::uint32_t bytesReceived = sizeof(buffer);

    while (m_jdPlay->dpReceive(buffer, bytesReceived, fromId, toId))
    {
        if (m_demoPlayersById.count(fromId) == 0u)
        {
            onLoadingTaMessage(fromId, toId, buffer, bytesReceived);
        }
    }

    if (m_wallClockTicks % 10u == 0u)
    {
        for (const auto& demoPlayer : m_demoPlayers)
        {
            int percent = m_wallClockTicks > 20u ? 100u : m_wallClockTicks * 5u;
            TADemo::TProgress progress(percent);
            this->send(demoPlayer->dpId, 0, progress.asSubPacket());
        }
    }

    if (m_wallClockTicks == 20u)
    {
        for (const auto& demoPlayer : m_demoPlayers)
        {
            qInfo() << "[doLoad] starting demo player" << demoPlayer->dpId;
            static const std::uint8_t startMsg[] = { std::uint8_t(TADemo::SubPacketCode::START_15) };
            this->send(demoPlayer->dpId, 0, TADemo::bytestring(startMsg, sizeof(startMsg)));
        }

        for (const auto& dpPlayer : m_dpPlayers)
        {
            qInfo() << "[doLoad] starting dp player" << dpPlayer.first;
            const std::uint8_t startMsg[] = { std::uint8_t(TADemo::SubPacketCode::START_1E), 1 + dpPlayer.second->ordinalId };
            this->send(m_demoPlayers[0]->dpId, dpPlayer.first, TADemo::bytestring(startMsg, sizeof(startMsg)));
        }
    }

    if (m_wallClockTicks >= 20u)
    {
        bool allgo = true;
        for (const auto& dpPlayer : m_dpPlayers)
        {
            allgo &= dpPlayer.second->state == DpPlayerState::PLAYING;
        }
        return allgo;
    }

    return false;
}

void Replayer::onLoadingTaMessage(std::uint32_t sourceDplayId, std::uint32_t destDplayId, const std::uint8_t *_payload, int _payloadSize)
{
    TADemo::bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        std::uint16_t checksum[2];
        TADemo::TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            // not for us
            return;
        }
    }

    if (TADemo::PacketCode(payload[0]) == TADemo::PacketCode::COMPRESSED)
    {
        payload = TADemo::TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[Replayer::onLoadingTaMessage] decompression ran out of bytes!";
        }
    }

    std::vector<TADemo::bytestring> subpaks = TADemo::TPacket::unsmartpak(payload, true, true);
    for (const TADemo::bytestring& s : subpaks)
    {
        unsigned expectedSize = TADemo::TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[Replayer::onLoadingTaMessage] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16);
            std::ostringstream ss;
            ss << "  _payload:\n";
            TADemo::HexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            continue;
        }

        if (TADemo::SubPacketCode(s[0]) == TADemo::SubPacketCode::UNIT_STAT_AND_MOVE_2C)
        {
            auto it = m_dpPlayers.find(sourceDplayId);
            if (it != m_dpPlayers.end())
            {
                //qInfo() << "[Replayer::onLoadingTaMessage] UNIT_STAT_AND_MOVE dpid=" << it->first;
                it->second->state = DpPlayerState::PLAYING;
            }
            else
            {
                //qInfo() << "[Replayer::onLoadingTaMessage] UNIT_STAT_AND_MOVE non-player dpid=" << it->first;
            }
        }
    }
}

bool Replayer::doPlay()
{
    std::uint8_t buffer[10000];
    std::uint32_t fromId, toId;
    std::uint32_t bytesReceived = sizeof(buffer);

    while (m_jdPlay->dpReceive(buffer, bytesReceived, fromId, toId))
    {
        if (toId == m_demoPlayers[0]->dpId && m_demoPlayersById.count(fromId) == 0u)
        {
            onPlayingTaMessage(fromId, toId, buffer, bytesReceived);
        }
    }

    if (!m_isPaused)
    {
        m_targetTicksFractional += double(m_playBackSpeed) / 10.0 * WALL_TO_GAME_TICK_RATIO;
        m_targetTicks += m_targetTicksFractional;
        m_targetTicksFractional -= unsigned(m_targetTicksFractional);
    }

    //qInfo() << "[Replayer::doPlay] isPaused=" << m_isPaused << "pending=" << m_pendingGamePackets.size() << "m_targetTicks=" << m_targetTicks + m_targetTicksFractional << "m_demoTicks=" << m_demoTicks;
    for (; !m_isPaused && std::int32_t(m_targetTicks - m_demoTicks) > 0; m_pendingGamePackets.pop())
    {
        if (m_pendingGamePackets.size() < NUM_PAKS_TO_PRELOAD)
        {
            this->parse(m_demoDataStream, NUM_PAKS_TO_PRELOAD);
        }
        if (m_pendingGamePackets.empty())
        {
            std::uint8_t pausePacket[] = { 0x19, 0x00, 0x01 };
            TADemo::bytestring bs(pausePacket, sizeof(pausePacket));
            this->sendUdp(m_demoPlayers[0]->dpId, 0, bs);
            m_isPaused = true;
            this->say(m_demoPlayers[0]->dpId, "Replay buffer is empty. Unpause to continue");
            break;
        }

        TADemo::Packet &packet = m_pendingGamePackets.front().first;
        std::vector<TADemo::bytestring>& moves = m_pendingGamePackets.front().second;
        std::shared_ptr<DemoPlayer> sender;
        for (const auto& demoPlayer : m_demoPlayers)
        {
            if (demoPlayer->number == packet.sender)
            {
                sender = demoPlayer;
            }
        }
        if (!sender)
        {
            qWarning() << "Unable to find demo player number" << packet.sender;
            continue;
        }

        TADemo::bytestring filteredMoves;
        std::uint32_t initialSenderTicks = sender->ticks;
        for (auto& move : moves)
        {
            switch (TADemo::SubPacketCode(move[0]))
            {
            case TADemo::SubPacketCode::CHAT_05:
                filteredMoves += move;
                break;

            case TADemo::SubPacketCode::SPEED_19:
            {
                std::ostringstream ss;
                if (move[1] == 0 && move[2] == 1u)
                {
                    ss << sender->name << " paused the game";
                }
                else if (move[1] == 0 && move[2] != 1u)
                {
                    ss << sender->name << " unpaused the game";
                }
                else
                {
                    ss << sender->name << " set speed to " << std::showpos << int(move[2]) - 10;
                }
                std::string msg = ss.str();
                if (!msg.empty())
                {
                    say(sender->dpId, msg);
                }
                // do not feed
                break;
            }

            case TADemo::SubPacketCode::REJECT_1B:
            {
                std::uint32_t originalDpId = *(std::uint32_t*) & move[1];
                auto rejectee = getDemoPlayerByOriginalDpId(originalDpId);
                if (rejectee)
                {
                    say(sender->dpId, sender->name + " rejected " + rejectee->name);
                }
                filteredMoves += move;
                break;
            }

            case TADemo::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
            {
                std::uint32_t ticks = *(std::uint32_t*) & move[3];
                m_demoTicks = sender->ticks = ticks;
                for (const auto& demoPlayer : m_demoPlayers)
                {
                    m_demoTicks = std::min(m_demoTicks, demoPlayer->ticks);
                }
                filteredMoves += move;
                break;
            }

            case TADemo::SubPacketCode::ALLY_CHAT_F9:
            {
                std::uint32_t originalDpId = *(std::uint32_t*) & move[1];
                auto player = getDemoPlayerByOriginalDpId(originalDpId);
                std::string msg((const char*)move.data()+9, move.size()-9);
                std::size_t leftBrack = msg.find_first_of('<', 0u);
                std::size_t rightBrack = msg.find_last_of('>', std::string::npos);
                if (leftBrack != std::string::npos && rightBrack != std::string::npos)
                {
                    msg[leftBrack] = '[';
                    msg[rightBrack] = ']';
                    say(player->dpId, msg);
                }
                // do not feed
                break;
            }

            default:
                filteredMoves += move;
                break;
            }
        }

        //static const std::uint8_t debugpak[] = {
        //    0x03, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x2C, 0x0E, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        //    0x24, 0xF0, 0xFF, 0x1F, 0x00, 0xFC, 0x40, 0x02, 0xC7, 0x06, 0xFC, 0x40, 0x02, 0xC7, 0x06
        //};
        //if (filteredMoves == TADemo::bytestring(debugpak, sizeof(debugpak)))
        //{
        //    TADemo::bytestring bs(packet.data);
        //    std::cout << "packet:" << std::endl;
        //    TADemo::HexDump(bs.data(), bs.size(), std::cout);

        //    std::cout << "subpaks(calculated):" << std::endl;
        //    for (const auto& subpak : TADemo::TPacket::unsmartpak(bs, false, false))
        //    {
        //        TADemo::HexDump(subpak.data(), subpak.size(), std::cout);
        //    }

        //    std::cout << "subpaks(given):" << std::endl;
        //    for (const auto& subpak : moves)
        //    {
        //        TADemo::HexDump(subpak.data(), subpak.size(), std::cout);
        //    }

        //    std::cout << "filteredMoves:" << std::endl;
        //    TADemo::HexDump(filteredMoves.data(), filteredMoves.size(), std::cout);
        //}

        if (filteredMoves.size() > 0)
        {
            //std::cout << "sender=" << int(packet.sender) << '\n';
            //std::cout << "raw:\n";
            //TADemo::HexDump(filteredMoves.data(), filteredMoves.size(), std::cout);
            //std::cout << "compressed/encrypted:\n";
            //TADemo::HexDump(filteredMoves.data(), filteredMoves.size(), std::cout);
            sendUdp(sender->dpId, 0, filteredMoves);
        }
    }
    return false;
}

void Replayer::onPlayingTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize)
{
    TADemo::bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        std::uint16_t checksum[2];
        TADemo::TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            // not for us
            return;
        }
    }
    
    if (TADemo::PacketCode(payload[0]) == TADemo::PacketCode::COMPRESSED)
    {
        payload = TADemo::TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[Replayer::onPlayingTaMessage] decompression ran out of bytes!";
        }
    }

    std::vector<TADemo::bytestring> subpaks = TADemo::TPacket::unsmartpak(payload, true, true);
    for (const TADemo::bytestring& s : subpaks)
    {
        unsigned expectedSize = TADemo::TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[Replayer::onPlayingTaMessage] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16);
            std::ostringstream ss;
            ss << "  _payload:\n";
            TADemo::HexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            continue;
        }

        switch (TADemo::SubPacketCode(s[0]))
        {
        case TADemo::SubPacketCode::SPEED_19:
        {
            std::ostringstream ss;
            if (s[1] == 0 && s[2] == 1u)
            {
                m_isPaused = true;
            }
            else if (s[1] == 0 && s[2] != 1u)
            {
                m_isPaused = false;
            }
            else
            {
                m_playBackSpeed = s[2];
            }
            break;
        }
        case TADemo::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
        {
            std::uint32_t ticks = *(std::uint32_t*) & s[3];
            for (auto& dpPlayer : m_dpPlayers)
            {
                if (dpPlayer.second->dpid == sourceDplayId)
                {
                    if (ticks >= 100u && dpPlayer.second->ticks < 100u)
                    {
                        createSonar(sourceDplayId, dpPlayer.second->ordinalId - m_demoPlayers.size());
                    }
                    dpPlayer.second->ticks = ticks;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void Replayer::createSonar(std::uint32_t receivingDpId, unsigned number)
{
    const std::uint8_t startBuildSonar[] = {
        0x09, 0x46, 0x00, 0x31, 0x32, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x00, 0x00, 0x00, 0xE9, 0x74, 0x00, 0x00 };
    const std::uint8_t finishBuildSonar[] = {
        0x11, 0x05, 0x00, 0x01 };
    const std::uint8_t giveSonar[] = {
        0x14, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x00, 0x00, 0x00, 0x00, 0x46, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x64, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00 };

    const std::uint16_t unitNumber = m_header.maxUnits - 1u;

    // doesn't seem to matter which player creates it, but anyway m_demoPlayersById.begin should match the unitNumber
    auto& benefactor = m_demoPlayersById.begin()->second;

    qInfo() << "[Replayer::createSonar] for watcher using drone number:" << benefactor->number << "dpid:" << benefactor->dpId << "ordinal:" << benefactor->ordinalId;
    TADemo::bytestring bs(startBuildSonar, sizeof(startBuildSonar));
    *(std::uint16_t*)& bs[1] = m_demoUnitInfo.size();
    *(std::uint16_t*)& bs[3] = unitNumber;
    *(std::uint16_t*)& bs[7] = number*0xa0 + 0x50;
    send(benefactor->dpId, 0, bs);

    bs.assign(finishBuildSonar, sizeof(finishBuildSonar));
    *(std::uint16_t*)& bs[1] = unitNumber;
    send(benefactor->dpId, 0, bs);

    bs.assign(giveSonar, sizeof(giveSonar));
    *(std::uint16_t*)& bs[1] = unitNumber;
    *(std::uint32_t*)& bs[3] = receivingDpId;
    send(benefactor->dpId, 0, bs);
}

int doMain(int argc, char* argv[])
{
    const char* DEFAULT_DPLAY_REGISTERED_GAME_GUID = "{99797420-F5F5-11CF-9827-00A0241496C8}";
    const char* DEFAULT_DPLAY_REGISTERED_GAME_PATH = "c:\\cavedog\\totala";

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TaReplayer");
    QCoreApplication::setApplicationVersion("0.14");

    QCommandLineParser parser;
    parser.setApplicationDescription("TA Replayer");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("logfile", "path to file in which to write logs.", "logfile", "c:\\temp\\tareplayer.log"));
    parser.addOption(QCommandLineOption("loglevel", "level of noise in log files. 0 (silent) to 5 (debug).", "logfile", "5"));
    parser.addOption(QCommandLineOption("demofile", "path to demo file to replay.", "demofile"));
    parser.process(app);

    Logger::Initialise(parser.value("logfile").toStdString(), Logger::Verbosity(parser.value("loglevel").toInt()));
    qInstallMessageHandler(Logger::Log);

    std::ifstream demoFile(parser.value("demofile").toStdString().c_str());
    Replayer replayer(&demoFile);
    replayer.hostGame(DEFAULT_DPLAY_REGISTERED_GAME_GUID, "TAReplayer", "127.0.0.1");

    app.exec();
    return 0;
}

int main(int argc, char* argv[])
{
    try
    {
        return doMain(argc, argv);
    }
    catch (std::exception & e)
    {
        std::cerr << "[main catch std::exception] " << e.what() << std::endl;
        qWarning() << "[main catch std::exception]" << e.what();
        return 1;
    }
    catch (...)
    {
        std::cerr << "[main catch ...] " << std::endl;
        qWarning() << "[main catch ...]";
        return 1;
    }
}
