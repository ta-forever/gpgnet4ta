#include "TaReplayer.h"

#include "jdplay/JDPlay.h"
#include "taflib/EngineeringNotation.h"
#include "taflib/HexDump.h"
#include "taflib/Logger.h"

#include <cmath>
#include <dplay.h>
#include <iomanip>
#include <sstream>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

Replayer::Replayer(std::istream* demoDataStream) :
    m_timerId(startTimer(100, Qt::TimerType::PreciseTimer)),
    m_tcpSeq(0xfffffffe),
    m_wallClockTicks(0u),
    m_demoTicks(0u),
    m_targetTicks(0u),
    m_targetTicksFractional(0.0),
    m_playBackSpeed(1.0),
    m_rateControl(1.0),
    m_state(ReplayState::LOADING_DEMO_PLAYERS),
    m_isPaused(false),
    m_tickLastUserPauseEvent(0u),
    m_demoDataStream(demoDataStream),
    m_launch(true),
    m_nochat(false)
{ }

Replayer::DemoPlayer::DemoPlayer(const tapacket::Player& player) :
    tapacket::Player(player),
    dpId(0u),
    originalDpId(0u),
    ticks(0u),
    cumulativeMetal(0.0),
    cumulativeMetalShared(0.0),
    cumulativeEnergy(0.0),
    cumulativeEnergyShared(0.0)
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
    hasTaken(false),
    warnedWatcherState(WarnedWatcherState::INITIAL)
{ }

Replayer::UnitInfo::UnitInfo() :
    id(0u),
    crc(0u),
    limit(0u),
    inUse(false)
{ }

void Replayer::setNoChat(bool option)
{
    m_nochat = option;
}

void Replayer::send(std::uint32_t fromId, std::uint32_t toId, const tapacket::bytestring& subpak)
{
    tapacket::bytestring bs = tapacket::TPacket::trivialSmartpak(subpak, toId == 0 ? m_tcpSeq-- : 0xffffffff);
    tapacket::TPacket::encrypt(bs);
    m_jdPlay->dpSend(fromId, toId, 1, (void*)bs.data(), bs.size());
}

void Replayer::sendUdp(std::uint32_t fromId, std::uint32_t toId, const tapacket::bytestring& subpak)
{
    tapacket::bytestring bs = tapacket::TPacket::trivialSmartpak(subpak, 0);
    tapacket::TPacket::encrypt(bs);
    m_jdPlay->dpSend(fromId, toId, 0, (void*)bs.data(), bs.size());
}

void Replayer::say(std::uint32_t fromId, const std::string& text)
{
    this->sendUdp(fromId, 0, tapacket::TPacket::createChatSubpacket(text));
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
    std::string guid = _guid.toStdString();
    std::string player = _player.toStdString();
    std::string ipaddr = _ipaddr.toStdString();

    qInfo() << "[Replayer::hostGame]" << guid.c_str() << player.c_str() << ipaddr.c_str();
    m_jdPlay.reset(new jdplay::JDPlay(player.c_str(), 3, NULL));// "c:\\temp\\jdplay_ta_replayer.log"));
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
        }

        if (m_dpIdsPrealloc.front() > m_dpIdsPrealloc.back())
        {
            qWarning() << "[Replayer::hostGame] don't like these DPIDs (they may confuse the user's instance). reallocating ...";
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

void Replayer::handle(const tapacket::Header& header)
{
    m_header = header;
}

void Replayer::handle(const tapacket::Player& _player, int n, int ofTotal)
{
    std::shared_ptr<DemoPlayer> demoPlayer(new DemoPlayer(_player));
    m_demoPlayers.push_back(demoPlayer);
}

void Replayer::handle(const tapacket::PlayerStatusMessage& msg, std::uint32_t dplayid, int n, int ofTotal)
{
    m_demoPlayers[n]->ordinalId = n;
    m_demoPlayers[n]->originalDpId = dplayid;
    m_demoPlayers[n]->statusPacket = msg.statusMessage;
}

void Replayer::handle(const tapacket::UnitData& unitData)
{
    const std::uint8_t* ptr = unitData.unitData.data();
    const std::uint8_t* end = ptr + unitData.unitData.size();
    unsigned subpakLen = tapacket::TPacket::getExpectedSubPacketSize(ptr, end - ptr);
    while (ptr < end && subpakLen > 0u)
    {
        tapacket::bytestring bs(ptr, subpakLen);
        processUnitData(tapacket::TUnitData(bs));
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

void Replayer::processUnitData(const tapacket::TUnitData& unitData)
{
    if (unitData.pktid == tapacket::SubPacketCode::GIVE_UNIT_14 || unitData.pktid == tapacket::SubPacketCode::TEAM_24)
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

void Replayer::handle(const tapacket::Packet& packet, const std::vector<tapacket::bytestring>& unpaked, std::size_t n)
{
    //tapacket::HexDump(packet.data.data(), packet.data.size(), std::cout);
    this->m_pendingGamePackets.push(std::make_pair(packet, unpaked));
}

void Replayer::timerEvent(QTimerEvent* event)
{
    try
    {
        ++m_wallClockTicks;
        if (m_state != ReplayState::PLAY && m_state != ReplayState::DONE && m_pendingGamePackets.size() < 1u)
        {
            this->parse(m_demoDataStream, NUM_PAKS_TO_PRELOAD);
        }

        switch (m_state) {
        case ReplayState::LOADING_DEMO_PLAYERS:
            if (this->m_pendingGamePackets.size() > 0u && m_demoPlayers.size() > 0u && m_demoPlayers.back()->originalDpId != 0u)
            {
                onCompletedLoadingDemoPlayers();
                m_state = ReplayState::LOBBY;
                qInfo() << "LOBBY";
                m_wallClockTicks = 0u;   // we're going to use it to indicate load progress
            }
            break;

        case ReplayState::LOBBY:
            if (m_launch)
            {
                m_launch = false;
                emit readyToJoin();
            }
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
    catch (const std::exception & e)
    {
        qWarning() << "[Replayer::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[Replayer::timerEvent] general exception:";
    }
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
            tapacket::TPlayerInfo demoPlayerStatus(demoPlayer->statusPacket);
            demoPlayerStatus.setDpId(demoPlayer->dpId);
            demoPlayerStatus.setAllowWatch(true);
            demoPlayerStatus.setPermLos(true);
            demoPlayerStatus.setCheat(true);
            tapacket::bytestring bs = demoPlayerStatus.asSubPacket();
            this->send(demoPlayer->dpId, 0, demoPlayerStatus.asSubPacket());
        }
    }
    return launch;
}

void Replayer::sendPlayerInfos()
{
    tapacket::TIdent2 id2;
    for (std::size_t n = 0u; n < m_demoPlayers.size(); ++n)
    {
        id2.dpids[n] = m_demoPlayers[n]->dpId;
    }
    tapacket::bytestring bs = id2.asSubPacket();
    this->send(m_demoPlayers[0]->dpId, 0, bs);

    int ordinalId = 0;
    for (const auto& demoPlayer : m_demoPlayers)
    {
        demoPlayer->ordinalId = ordinalId++;
        tapacket::bytestring bs = tapacket::TIdent3(demoPlayer->dpId, demoPlayer->ordinalId).asSubPacket();
        this->send(m_demoPlayers[0]->dpId, 0, bs);
    }
    for (const auto& dpPlayer : m_dpPlayers)
    {
        dpPlayer.second->ordinalId = ordinalId++;
        tapacket::bytestring bs = tapacket::TIdent3(dpPlayer.first, dpPlayer.second->ordinalId).asSubPacket();
        this->send(m_demoPlayers[0]->dpId, 0, bs);
    }
    for (const auto& demoPlayer : m_demoPlayers)
    {
        tapacket::TPlayerInfo playerInfo(demoPlayer->statusPacket);
        playerInfo.setDpId(demoPlayer->dpId);
        playerInfo.setInternalVersion(0u);
        playerInfo.setAllowWatch(true);
        playerInfo.setCheat(true);
        tapacket::bytestring bs = playerInfo.asSubPacket();
        this->send(m_demoPlayers[0]->dpId, 0, bs);
    }
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
        qInfo() << "[Replayer::onCompletedLoadingDemoPlayers] name:" << m_demoPlayers[n]->name.c_str() << "originalDpId:" << m_demoPlayers[n]->originalDpId << "dpId:" << dpid;
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
        qInfo() << "[Replayer::timerEvent] DPMSG session description";
        break;
    }
    default:
        qWarning() << "[Replayer::timerEvent] DPMSG unhandled type";
    };
}

void Replayer::onLobbyTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize)
{
    tapacket::bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        std::uint16_t checksum[2];
        tapacket::TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            // not for us
            return;
        }
    }

    if (tapacket::PacketCode(payload[0]) == tapacket::PacketCode::COMPRESSED)
    {
        payload = tapacket::TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[Replayer::onLobbyTaMessage] decompression ran out of bytes!";
        }
    }

    std::vector<tapacket::bytestring> subpaks = tapacket::TPacket::unsmartpak(payload, true, true);
    for (const tapacket::bytestring& s : subpaks)
    {
        unsigned expectedSize = tapacket::TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[Replayer::onLobbyTaMessage] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16);
            std::ostringstream ss;
            ss << "  _payload:\n";
            taflib::HexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            continue;
        }

        switch (tapacket::SubPacketCode(s[0]))
        {
        case tapacket::SubPacketCode::PING_02:
        {
            tapacket::TPing ping(s);
            ping.value = 1000000u + std::uint32_t(rand()) % 1000000u;
            this->send(otherDplayId, sourceDplayId, ping.asSubPacket());
            break;
        }
        case tapacket::SubPacketCode::UNIT_DATA_1A:
        {
            auto itWatcher = m_dpPlayers.find(sourceDplayId);
            if (itWatcher != m_dpPlayers.end())
            {
                tapacket::TUnitData unitData(s);
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
                else if (unitData.sub == 0x09)
                {
                    qInfo() << "[Replayer::onLobbyTaMessage] unitData: id,sub,crc,crc,status=" << unitData.id << ',' << (int)unitData.sub << ',' << unitData.u.crc << ',' << unitInfoCrc << ',' << unitData.u.statusAndLimit[0];
                }
            }
            break;
        }
        case tapacket::SubPacketCode::PLAYER_INFO_20:
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
        this->send(hostDpId, dpPlayer.dpid, tapacket::TPacket::createHostMigrationSubpacket(dpPlayer.ordinalId));

        // request remote to send unit data
        this->send(hostDpId, dpPlayer.dpid, tapacket::TUnitData().asSubPacket());
        ++dpPlayer.unitSyncSendCount;
        dpPlayer.state = DpPlayerState::SEND_UNITS;
        break;
    }
    case DpPlayerState::SEND_UNITS:
    {
        for (int n = 0; n < UNITS_SYNC_PER_TICK; ++n)
        {
            const UnitInfo* unitInfo = m_demoUnitInfoLinear[dpPlayer.unitSyncNextUnit];
            tapacket::TUnitData msg(unitInfo->id, unitInfo->limit, true);
            this->send(hostDpId, dpPlayer.dpid, msg.asSubPacket());
            ++dpPlayer.unitSyncNextUnit;
            ++dpPlayer.unitSyncSendCount;
            if (dpPlayer.unitSyncNextUnit == m_demoUnitInfoLinear.size())
            {
                //tapacket::TUnitData msg(unitInfo->id, unitInfo->limit, true);
                //msg.sub = 9;
                //msg.id = 0;
                //msg.fill = 0;
                //this->send(hostDpId, dpPlayer.dpid, msg.asSubPacket());

                qInfo() << "[Replayer::doSyncWatcher] Sent all units - " << dpPlayer.unitSyncSendCount;
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
        ++dpPlayer.unitSyncNextUnit;
        if (dpPlayer.unitSyncReceiveCount + 1 >= dpPlayer.unitSyncSendCount)
        {
            dpPlayer.unitSyncSendCount = dpPlayer.unitSyncReceiveCount;
            dpPlayer.state = DpPlayerState::CHECK_ERRORS;
        }
        else if (dpPlayer.unitSyncNextUnit > 10u)
        {
            qInfo() << "[Replayer::doSyncWatcher] Current ack status:" << dpPlayer.unitSyncReceiveCount << "of" << dpPlayer.unitSyncSendCount;
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
        if (dpPlayer.unitSyncErrorCount != 0u)
        {
            qInfo() << "[Replayer::doSyncWatcher] CRC errors on" << dpPlayer.unitSyncErrorCount << "units!";
            std::ostringstream ss;
            ss << "*** You have CRC errors on " << dpPlayer.unitSyncErrorCount << " units!";
            this->say(hostDpId, ss.str());
        }
        if (dpPlayer.unitSyncAckCount < m_demoUnitInfo.size())
        {
            qInfo() << "[Replayer::doSyncWatcher] You are missing" << m_demoUnitInfo.size() - dpPlayer.unitSyncAckCount << "of" << m_demoUnitInfo.size() << "units!";
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
        for (std::size_t n = 0u; n < UNITS_SYNC_PER_TICK; ++n)
        {
            const UnitInfo& unitInfo = *m_demoUnitInfoLinear[dpPlayer.unitSyncNextUnit];
            tapacket::TUnitData unitData(unitInfo.id, unitInfo.limit, true);
            this->send(hostDpId, dpPlayer.dpid, unitData.asSubPacket());
            ++dpPlayer.unitSyncNextUnit;
            ++dpPlayer.unitSyncSendCount;
            if (dpPlayer.unitSyncNextUnit == m_demoUnitInfo.size())
            {
                qInfo() << "[Replayer::doSyncWatcher] Sent ack on all units -" << dpPlayer.unitSyncSendCount;
                std::ostringstream ss;
                ss << "*** Sent ack on all units - " << dpPlayer.unitSyncSendCount;
                this->say(hostDpId, ss.str());
                dpPlayer.unitSyncNextUnit = 0u;
                dpPlayer.state = DpPlayerState::WAIT_ACKS;
            }
        }
        break;
    }
    case DpPlayerState::WAIT_ACKS:
    {
        ++dpPlayer.unitSyncNextUnit;
        if (dpPlayer.unitSyncReceiveCount == dpPlayer.unitSyncSendCount)
        {
            dpPlayer.state = DpPlayerState::END_SYNC;
        }
        else if (dpPlayer.unitSyncNextUnit > 30u)
        {
            qInfo() << "[replayer::doSyncWatcher] Current ack status:" << dpPlayer.unitSyncReceiveCount << "of" << dpPlayer.unitSyncSendCount;
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
        qInfo() << "[replayer::doSyncWatcher] Unit sync is complete";
        this->say(hostDpId, "*** Unit sync is complete");
        dpPlayer.state = DpPlayerState::WAIT_GO;
        break;
    }
    case DpPlayerState::WAIT_GO:
        tapacket::TPlayerInfo playerInfo(dpPlayer.statusPacket);
        if (playerInfo.isClickedIn() && (!playerInfo.isWatcher() || dpPlayer.warnedWatcherState == WarnedWatcherState::PROCEED_REGARDLESS))
        {
            return 1;
        }
        else if (playerInfo.isClickedIn() && dpPlayer.warnedWatcherState == WarnedWatcherState::INITIAL)
        {
            dpPlayer.warnedWatcherState = WarnedWatcherState::HAVE_WARNED;
            this->say(hostDpId, "Please join as a regular player, not as a watcher");
            this->say(hostDpId, "(or green-off / green-on again to proceed as watcher)");
        }
        else if (!playerInfo.isClickedIn() && dpPlayer.warnedWatcherState == WarnedWatcherState::HAVE_WARNED)
        {
            dpPlayer.warnedWatcherState = WarnedWatcherState::PROCEED_REGARDLESS;
        }
        break;
    }

    return 0;
}

void Replayer::doLaunch()
{
    for (const auto& demoPlayer : m_demoPlayers)
    {
        tapacket::TIdent3 id3(demoPlayer->dpId, demoPlayer->ordinalId);
        this->send(m_demoPlayers[0]->dpId, 0, id3.asSubPacket());
    }

    for (const auto& dpPlayer : m_dpPlayers)
    {
        tapacket::TIdent3 id3(dpPlayer.first, dpPlayer.second->ordinalId);
        this->send(m_demoPlayers[0]->dpId, 0, id3.asSubPacket());
        dpPlayer.second->state = DpPlayerState::LOADING;
    }

    std::uint32_t user1, user2, user3, user4;
    m_jdPlay->dpGetSession(user1, user2, user3, user4);
    user1 = (user1 & 0xff00ffff) | 0x00320000;
    m_jdPlay->dpSetSession(user1, user2, user3, user4);

    static const std::uint8_t replayerServerMsg[] = { std::uint8_t(tapacket::SubPacketCode::REPLAYER_SERVER_FA), 0x06 };
    static const std::uint8_t loadingStartedMsg[] = { std::uint8_t(tapacket::SubPacketCode::LOADING_STARTED_08), 0x06 };
    this->send(m_demoPlayers[0]->dpId, 0, tapacket::bytestring(replayerServerMsg, sizeof(replayerServerMsg)));
    this->send(m_demoPlayers[0]->dpId, 0, tapacket::bytestring(loadingStartedMsg, sizeof(loadingStartedMsg)));
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
            tapacket::TProgress progress(percent);
            this->send(demoPlayer->dpId, 0, progress.asSubPacket());
        }
    }

    if (m_wallClockTicks % 10u == 0u && m_wallClockTicks >= 20u)
    {
        for (const auto& demoPlayer : m_demoPlayers)
        {
            qInfo() << "[doLoad] starting demo player" << demoPlayer->dpId;
            static const std::uint8_t startMsg[] = { std::uint8_t(tapacket::SubPacketCode::START_15) };
            this->send(demoPlayer->dpId, 0, tapacket::bytestring(startMsg, sizeof(startMsg)));
        }

        for (const auto& dpPlayer : m_dpPlayers)
        {
            qInfo() << "[doLoad] starting dp player" << dpPlayer.first;
            const std::uint8_t startMsg[] = { std::uint8_t(tapacket::SubPacketCode::START_1E), 1 + dpPlayer.second->ordinalId };
            this->send(m_demoPlayers[0]->dpId, dpPlayer.first, tapacket::bytestring(startMsg, sizeof(startMsg)));
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

void Replayer::onLoadingTaMessage(std::uint32_t sourceDplayId, std::uint32_t destDplayId, const std::uint8_t* _payload, int _payloadSize)
{
    tapacket::bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        std::uint16_t checksum[2];
        tapacket::TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            // not for us
            return;
        }
    }

    if (tapacket::PacketCode(payload[0]) == tapacket::PacketCode::COMPRESSED)
    {
        payload = tapacket::TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[Replayer::onLoadingTaMessage] decompression ran out of bytes!";
        }
    }

    //std::ostringstream ss;
    //ss << "[Replayer::onLoadingTaMessage] decompressed:\n";
    //taflib::HexDump(payload.data(), payload.size(), ss);
    //qInfo() << ss.str().c_str();

    std::vector<tapacket::bytestring> subpaks = tapacket::TPacket::unsmartpak(payload, true, true);
    for (const tapacket::bytestring& s : subpaks)
    {
        //std::ostringstream ss;
        //ss << "[Replayer::onLoadingTaMessage] subpak:\n";
        //taflib::HexDump(s.data(), s.size(), ss);
        //qInfo() << ss.str().c_str();

        unsigned expectedSize = tapacket::TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[Replayer::onLoadingTaMessage] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16);
            std::ostringstream ss;
            ss << "  _payload:\n";
            taflib::HexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            continue;
        }

        switch(tapacket::SubPacketCode(s[0]))
        {
        case tapacket::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
        case tapacket::SubPacketCode::START_15:
            auto it = m_dpPlayers.find(sourceDplayId);
            if (it != m_dpPlayers.end())
            {
                it->second->state = DpPlayerState::PLAYING;
            }
            else
            {
                qWarning() << "[Replayer::onLoadingTaMessage] packet from" << sourceDplayId << ", but player is not found in m_players";
            }
        }
    }
}

bool Replayer::doPlay()
{
    std::uint8_t buffer[10000];
    std::uint32_t fromId, toId;
    std::uint32_t bytesReceived = sizeof(buffer);

    if (m_pendingGamePackets.size() < NUM_PAKS_TO_PRELOAD)
    {
        this->parse(m_demoDataStream, NUM_PAKS_TO_PRELOAD);
    }

    // as long as we're able to keep buffer at least quarter full, m_rateControl increases to 1.0
    m_rateControl += m_pendingGamePackets.size() < NUM_PAKS_TO_PRELOAD/4 ? -0.005 : 0.005;
    m_rateControl = std::max(m_rateControl, 0.5);
    m_rateControl = std::min(m_rateControl, 1.0);

    std::uint32_t dpTicks = 0u;
    for (auto dpPlayer : m_dpPlayers)
    {
        dpTicks = std::max(dpTicks, dpPlayer.second->ticks);
    }

    if (!m_isPaused && (!m_pendingGamePackets.empty() || std::int32_t(m_targetTicks - m_demoTicks) <= 0))
    {
        if (m_wallClockTicks < 100u)
        {
            m_targetTicks = dpTicks+1;
            m_targetTicksFractional = 0.0;
        }
        else
        {
            m_targetTicksFractional += m_rateControl * m_playBackSpeed * WALL_TO_GAME_TICK_RATIO;
            m_targetTicks += m_targetTicksFractional;
            m_targetTicksFractional -= unsigned(m_targetTicksFractional);
        }
    }

    for (;; m_pendingGamePackets.pop())
    {
        while (m_jdPlay->dpReceive(buffer, bytesReceived, fromId, toId))
        {
            if (toId == m_demoPlayers[0]->dpId && m_demoPlayersById.count(fromId) == 0u)
            {
                onPlayingTaMessage(fromId, toId, buffer, bytesReceived);
            }
        }

        if (m_pendingGamePackets.size() < NUM_PAKS_TO_PRELOAD)
        {
            this->parse(m_demoDataStream, NUM_PAKS_TO_PRELOAD);
        }

        if (m_isPaused || std::int32_t(m_targetTicks - m_demoTicks) <= 0 || m_pendingGamePackets.empty())
        {
            break;
        }

        tapacket::Packet& packet = m_pendingGamePackets.front().first;
        std::vector<tapacket::bytestring>& moves = m_pendingGamePackets.front().second;
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

        tapacket::bytestring filteredMoves;
        std::uint32_t initialSenderTicks = sender->ticks;
        for (auto& move : moves)
        {
            switch (tapacket::SubPacketCode(move[0]))
            {
            case tapacket::SubPacketCode::CHAT_05:
            {
                unsigned crc = m_crc.FullCRC(move.data(), 65);
                if (!m_nochat && 0u == m_recentChatMessageCrcsSet.count(crc))
                {
                    filteredMoves += move;
                }
                m_recentChatMessageCrcsSet.insert(crc);
                m_recentChatMessageCrcsExpireyQueue.push(crc);
                while (m_recentChatMessageCrcsExpireyQueue.size() > 16)
                {
                    crc = m_recentChatMessageCrcsExpireyQueue.front();
                    m_recentChatMessageCrcsExpireyQueue.pop();
                    m_recentChatMessageCrcsSet.erase(crc);
                }
                break;
            }

            case tapacket::SubPacketCode::GIVE_UNIT_14:
                // Yankspanker replayer filters this in Tsavefile.unsmartpak, the direct method of retrieving paks from savefile for feeding to game
                break;

            case tapacket::SubPacketCode::SPEED_19:
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

            case tapacket::SubPacketCode::REJECT_1B:
            {
                std::uint32_t originalDpId = *(std::uint32_t*) & move[1];
                auto rejectee = getDemoPlayerByOriginalDpId(originalDpId);
                if (rejectee)
                {
                    qInfo() << sender->name.c_str() << fromId << "rejected" << rejectee->name.c_str() << originalDpId;
                    say(sender->dpId, sender->name + " rejected " + rejectee->name);
                }
                filteredMoves += move;
                break;
            }

            case tapacket::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
            {
                std::uint32_t ticks = *(std::uint32_t*) & move[3];
                std::uint32_t demoTicks = sender->ticks = ticks;
                for (const auto& demoPlayer : m_demoPlayers)
                {
                    // sync with the the most lagged player that is actually progressing
                    if (demoPlayer->ticks > m_demoTicks)
                    {
                        demoTicks = std::min(demoTicks, demoPlayer->ticks);
                    }
                }
                m_demoTicks = demoTicks;
                filteredMoves += move;
                break;
            }

            case tapacket::SubPacketCode::ALLY_CHAT_F9:
            {
                std::uint32_t originalDpId = *(std::uint32_t*) & move[1];
                auto player = getDemoPlayerByOriginalDpId(originalDpId);
                std::string msg((const char*)move.data() + 9, move.size() - 9);
                std::size_t leftBrack = msg.find_first_of('<', 0u);
                std::size_t rightBrack = msg.find_last_of('>', std::string::npos);
                if (leftBrack != std::string::npos && rightBrack != std::string::npos)
                {
                    msg[leftBrack] = '[';
                    msg[rightBrack] = ']';
                    //say(player->dpId, msg);
                }
                // do not feed
                break;
            }

            case tapacket::SubPacketCode::PLAYER_RESOURCE_INFO_28:
            {
                sender->cumulativeMetal = *(float*)&move[46];
                sender->cumulativeEnergy = *(float*)&move[34];
                filteredMoves += move;
                break;
            }
            case tapacket::SubPacketCode::SHARE_RESOURCES_16:
            {
                std::uint32_t originalDpId = *(std::uint32_t*) & move[9];
                auto benefactor = getDemoPlayerByOriginalDpId(originalDpId);
                if (benefactor)
                {
                    const float amount = *(float*)&move[13];
                    const char type = move[1];
                    if (type == 2)
                    {
                        benefactor->cumulativeMetalShared += double(amount);
                    }
                    else
                    {
                        benefactor->cumulativeEnergyShared += double(amount);
                    }
                }
                filteredMoves += move;
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
        //if (filteredMoves == tapacket::bytestring(debugpak, sizeof(debugpak)))
        //{
        //    tapacket::bytestring bs(packet.data);
        //    std::cout << "packet:" << std::endl;
        //    tapacket::HexDump(bs.data(), bs.size(), std::cout);

        //    std::cout << "subpaks(calculated):" << std::endl;
        //    for (const auto& subpak : tapacket::TPacket::unsmartpak(bs, false, false))
        //    {
        //        tapacket::HexDump(subpak.data(), subpak.size(), std::cout);
        //    }

        //    std::cout << "subpaks(given):" << std::endl;
        //    for (const auto& subpak : moves)
        //    {
        //        tapacket::HexDump(subpak.data(), subpak.size(), std::cout);
        //    }

        //    std::cout << "filteredMoves:" << std::endl;
        //    tapacket::HexDump(filteredMoves.data(), filteredMoves.size(), std::cout);
        //}

        if (filteredMoves.size() > 0)
        {
            //std::cout << "sender=" << int(packet.sender) << '\n';
            //std::cout << "raw:\n";
            //tapacket::bytestring bs = tapacket::TPacket::trivialSmartpak(filteredMoves, 0xffffffff);
            //tapacket::HexDump(bs.data(), bs.size(), std::cout);

            //std::cout << "compressed/encrypted:\n";
            //bs = tapacket::TPacket::compress(bs);
            //tapacket::TPacket::encrypt(bs);
            //tapacket::HexDump(bs.data(), bs.size(), std::cout);
            sendUdp(sender->dpId, 0, filteredMoves);
        }
    }
    return false;
}

void Replayer::onPlayingTaMessage(std::uint32_t sourceDplayId, std::uint32_t otherDplayId, const std::uint8_t* _payload, int _payloadSize)
{
    tapacket::bytestring payload((const std::uint8_t*)_payload, _payloadSize);
    {
        std::uint16_t checksum[2];
        tapacket::TPacket::decrypt(payload, 0u, checksum[0], checksum[1]);
        if (checksum[0] != checksum[1])
        {
            // not for us
            return;
        }
    }

    if (tapacket::PacketCode(payload[0]) == tapacket::PacketCode::COMPRESSED)
    {
        payload = tapacket::TPacket::decompress(payload, 3);
        if (payload[0] != 0x03)
        {
            qWarning() << "[Replayer::onPlayingTaMessage] decompression ran out of bytes!";
        }
    }

    std::vector<tapacket::bytestring> subpaks = tapacket::TPacket::unsmartpak(payload, true, true);
    for (const tapacket::bytestring& s : subpaks)
    {
        unsigned expectedSize = tapacket::TPacket::getExpectedSubPacketSize(s);
        if (expectedSize == 0u || s.size() != expectedSize)
        {
            qWarning() << "[Replayer::onPlayingTaMessage] unknown subpacket. packet code" << QString::number(s[0], 16) << "expected size " << QString::number(expectedSize, 16) << "actual size " << QString::number(s.size(), 16);
            std::ostringstream ss;
            ss << "  _payload:\n";
            taflib::HexDump(_payload, _payloadSize, ss);
            qWarning() << ss.str().c_str();
            continue;
        }

        switch (tapacket::SubPacketCode(s[0]))
        {
        case tapacket::SubPacketCode::CHAT_05:
        {
            if (s.find((const std::uint8_t*) "> .total", 1) != tapacket::bytestring::npos)
            {
                for (auto player : m_demoPlayers)
                {
                    std::ostringstream ss;
                    double metalProduced = (player->cumulativeMetal - player->cumulativeMetalShared);
                    double energyProduced = (player->cumulativeEnergy - player->cumulativeEnergyShared);
                    double metalShared = player->cumulativeMetalShared;
                    ss << std::setw(15) << player->name << std::setw(0)
                        << " Metal: " << taflib::engineeringNotation(metalProduced).toStdString() 
                        << " Energy: " << taflib::engineeringNotation(energyProduced).toStdString()
                        << " Shared M: " << taflib::engineeringNotation(metalShared).toStdString();
                    this->say(player->dpId, ss.str());
                }
            }
            else if (s.find((const std::uint8_t*) "> .pos", 1) != tapacket::bytestring::npos)
            {
                std::streampos pos = m_demoDataStream->tellg();
                m_demoDataStream->seekg(0, m_demoDataStream->end);
                std::streampos size = m_demoDataStream->tellg();
                m_demoDataStream->seekg(pos, m_demoDataStream->beg);
                std::ostringstream ss;
                ss << int(100.0 * double(pos) / double(size)) << "% of " << size / 1000 << "KB";
                this->say(otherDplayId, ss.str());
            }
            break;
        }
        case tapacket::SubPacketCode::SPEED_19:
        {
            std::ostringstream ss;
            if (s[1] == 0 && s[2] == 1u)
            {
                m_isPaused = true;
                m_tickLastUserPauseEvent = m_wallClockTicks;
            }
            else if (s[1] == 0 && s[2] != 1u)
            {
                m_isPaused = false;
                m_tickLastUserPauseEvent = m_wallClockTicks;
            }
            // s[2] in range 1..20
            else if (s[2] == 19)
            {
                m_playBackSpeed = 4.24; // geometric mean between 1.8 and 10
            }
            else if (s[2] == 20)
            {
                m_playBackSpeed = 10.0;
            }
            else
            {
                m_playBackSpeed = double(s[2]) / 10.0;
                //m_playBackSpeed = std::pow(10.0, m_playBackSpeed) / 10.0;
            }
            break;
        }
        case tapacket::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
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
    tapacket::bytestring bs(startBuildSonar, sizeof(startBuildSonar));
    *(std::uint16_t*)& bs[1] = m_demoUnitInfo.size();
    *(std::uint16_t*)& bs[3] = unitNumber;
    *(std::uint16_t*)& bs[7] = number * 0xa0 + 0x50;
    send(benefactor->dpId, 0, bs);

    bs.assign(finishBuildSonar, sizeof(finishBuildSonar));
    *(std::uint16_t*)& bs[1] = unitNumber;
    send(benefactor->dpId, 0, bs);

    bs.assign(giveSonar, sizeof(giveSonar));
    *(std::uint16_t*)& bs[1] = unitNumber;
    *(std::uint32_t*)& bs[3] = receivingDpId;
    send(benefactor->dpId, 0, bs);
}
