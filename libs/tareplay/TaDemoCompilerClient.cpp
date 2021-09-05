#include "TaDemoCompilerClient.h"
#include "TaDemoCompilerMessages.h"
#include "taflib/HexDump.h"
#include <sstream>

using namespace tareplay;

TaDemoCompilerClient::TaDemoCompilerClient(QHostAddress taDemoCompilerAddress, quint16 taDemoCompilerPort, quint32 tafGameId, QString playerPublicAddr) :
    m_taDemoCompilerAddress(taDemoCompilerAddress),
    m_taDemoCompilerPort(taDemoCompilerPort),
    m_tafGameId(tafGameId),
    m_playerPublicAddr(playerPublicAddr),
    m_datastream(&m_tcpSocket),
    m_protocol(m_datastream),
    m_localPlayerDplayId(0u),
    m_hostDplayId(0u),
    m_ticks(-1)
{
    qInfo() << "[TaDemoCompilerClient::TaDemoCompilerClient] connecting to " << taDemoCompilerAddress.toString() << ":" << taDemoCompilerPort;
    m_datastream.setByteOrder(QDataStream::LittleEndian);
    m_tcpSocket.connectToHost(m_taDemoCompilerAddress, m_taDemoCompilerPort);
    if (!m_tcpSocket.waitForConnected(3000))
    {
        throw ConnectionError();
    }
}

TaDemoCompilerClient::~TaDemoCompilerClient()
{
    m_tcpSocket.close();
}

void TaDemoCompilerClient::setHostPlayerName(QString name)
{
    qInfo() << "[TaDemoCompilerClient::setHostPlayerName]" << name;
    m_hostPlayerName = name;
}

void TaDemoCompilerClient::setLocalPlayerName(QString name)
{
    qInfo() << "[TaDemoCompilerClient::setLocalPlayerName]" << name;
    m_localPlayerName = name;
}

void TaDemoCompilerClient::sendHello(quint32 gameId, quint32 dplayPlayerId, QString playerPublicAddr)
{
    qInfo() << "[TaDemoCompilerClient::sendHello] gameid,dplayPlayerId,playerPublicAddr" << gameId << dplayPlayerId << playerPublicAddr;
    m_protocol.sendCommand(HelloMessage::ID, 3);
    m_protocol.sendArgument(gameId);
    m_protocol.sendArgument(dplayPlayerId);
    m_protocol.sendArgument(playerPublicAddr.toUtf8());
}

void TaDemoCompilerClient::sendGameInfo(quint16 maxUnits, QString mapName)
{
    m_protocol.sendCommand(GameInfoMessage::ID, 2);
    m_protocol.sendArgument(maxUnits);
    m_protocol.sendArgument(mapName.toUtf8());
}

void TaDemoCompilerClient::sendGamePlayer(qint8 side, QString name, QByteArray statusMessage)
{
    m_protocol.sendCommand(GamePlayerMessage::ID, 3);
    m_protocol.sendArgument(side);
    m_protocol.sendArgument(name.toUtf8());
    m_protocol.sendArgument(statusMessage);
}

void TaDemoCompilerClient::sendGamePlayerNumber(quint32 dplayId, quint8 number)
{
    qInfo() << "[TaDemoCompilerClient::sendGamePlayerNumber] dplayid,number" << dplayId << number;
    m_protocol.sendCommand(GamePlayerNumber::ID, 2);
    m_protocol.sendArgument(dplayId);
    m_protocol.sendArgument(number);
}

void TaDemoCompilerClient::sendGamePlayerLoading(QSet<quint32> lockedInPlayers)
{
    qInfo() << "[TaDemoCompilerClient::sendGamePlayerLoading]";
    m_protocol.sendCommand(GamePlayerLoading::ID, lockedInPlayers.size());
    m_protocol.sendArgument(m_hostDplayId);
    for (quint32 dpid: lockedInPlayers)
    {
        if (dpid != m_hostDplayId)
        {
            m_protocol.sendArgument(dpid);
        }
    }
}

void TaDemoCompilerClient::sendUnitData(QByteArray unitData)
{
    m_protocol.sendCommand(GameUnitDataMessage::ID, 1);
    m_protocol.sendArgument(unitData);
}

void TaDemoCompilerClient::sendMoves(QByteArray moves)
{
    if (m_ticks < 3)
    {
        qInfo() << "[TaDemoCompilerClient::sendMoves]" << moves.size() << "bytes";
    }

    m_protocol.sendCommand(GameMoveMessage::ID, 1);
    m_protocol.sendArgument(moves);
}

void TaDemoCompilerClient::onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string& name, tapacket::DPAddress* , tapacket::DPAddress* )
{
    if (dplayId > 0u && !name.empty())
    {
        qInfo() << "[TaDemoCompilerClient::onDplaySuperEnumPlayerReply] dplayId,name:" << dplayId << name.c_str();
        m_dpConnectedPlayers.insert(dplayId);
        if (m_hostPlayerName.isEmpty())
        {
            throw std::runtime_error("you need to determine and setHostPlayerName() before GameMonitor receives any packets!");
        }
        else if (name.c_str() == m_hostPlayerName)
        {
            m_hostDplayId = dplayId;
        }

        if (m_localPlayerName.isEmpty())
        {
            throw std::runtime_error("you need to determine and setLocalPlayerName() before GameMonitor receives any packets!");
        }
        else if (name.c_str() == m_localPlayerName && dplayId != m_localPlayerDplayId)
        {
            m_localPlayerDplayId = dplayId;
            sendHello(m_tafGameId, dplayId, m_playerPublicAddr);
        }
    }
}

void TaDemoCompilerClient::onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string& name, tapacket::DPAddress* tcp, tapacket::DPAddress* udp)
{
    if (dplayId > 0u && !name.empty())
    {
        qInfo() << "[TaDemoCompilerClient::onDplayCreateOrForwardPlayer] command,dplayId,name:" << command << dplayId << name.c_str();
        m_dpConnectedPlayers.insert(dplayId);
        if (m_hostPlayerName.isEmpty())
        {
            throw std::runtime_error("you need to determine and setHostPlayerName() before GameMonitor receives any packets!");
        }
        else if (name.c_str() == m_hostPlayerName)
        {
            m_hostDplayId = dplayId;
        }

        if (m_localPlayerName.isEmpty())
        {
            throw std::runtime_error("you need to determine and setLocalPlayerName() before GameMonitor receives any packets!");
        }
        else if (name.c_str() == m_localPlayerName && dplayId != m_localPlayerDplayId)
        {
            m_localPlayerDplayId = dplayId;
            sendHello(m_tafGameId, dplayId, m_playerPublicAddr);
        }
    }
}

void TaDemoCompilerClient::onDplayDeletePlayer(std::uint32_t dplayId)
{
    m_dpConnectedPlayers.remove(dplayId);
    if (dplayId == m_localPlayerDplayId)
    {
        m_localPlayerDplayId = 0u;
    }
}

void TaDemoCompilerClient::onTaPacket(
    std::uint32_t sourceDplayId, std::uint32_t otherDplayId, bool isLocalSource,
    const char* encrypted, int sizeEncrypted,
    const std::vector<tapacket::bytestring>& subpaks)
{
    if (!isLocalSource) //sourceDplayId != m_localPlayerDplayId)
    {
        return;
    }

    QByteArray filteredMoves(1, 0x03);  // uncompressed, no checksum no timestamp
    tapacket::SmartPaker smartPaker;
    for (const tapacket::bytestring& s : subpaks)
    {
        switch (tapacket::SubPacketCode(s[0]))
        {
        case tapacket::SubPacketCode::PLAYER_INFO_20:
        {
            tapacket::TPlayerInfo playerInfo(s);
            if (playerInfo.getSlotNumber() < 10u)
            {
                if (sourceDplayId == m_hostDplayId)
                {
                    sendGameInfo(playerInfo.maxUnits, QString::fromStdString(playerInfo.getMapName()));
                }
                sendGamePlayer(playerInfo.getSide(), m_localPlayerName, QByteArray((char*)s.data(), s.size()));
            }
            break;
        }
        case tapacket::SubPacketCode::UNIT_DATA_1A:
        {
            tapacket::TUnitData ud(tapacket::bytestring((std::uint8_t*)s.data(), s.size()));
            if (ud.sub == 2 || ud.sub == 3)
            {
                //std::ostringstream ss;
                //ss << "unitdata:\n";
                //taflib::HexDump(s.data(), s.size(), ss);
                //qInfo() << ss.str().c_str();
                sendUnitData(QByteArray((char*)s.data(), s.size()));
            }
            break;
        }
        case tapacket::SubPacketCode::LOADING_STARTED_08:
        {
            sendGamePlayerLoading(m_dpConnectedPlayers);
            break;
        }
        case tapacket::SubPacketCode::LOADING_PROGRESS_2A:
        {
            if (s[1] == 100)
            {
                m_ticks = 0;
            }
            break;
        }
        case tapacket::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
        {
            // NB 32bit uint to 64bit int
            m_ticks = *(std::uint32_t*)(&s[3]);
            break;
        }
        default:
            break;
        };

        if (s[0] == std::uint8_t(tapacket::SubPacketCode::UNIT_STAT_AND_MOVE_2C))
        {
            tapacket::bytestring bs = smartPaker(s);
            filteredMoves.append((char*)bs.data(), bs.size());
        }
        else
        {
            filteredMoves.append((char*)s.data(), s.size());
        }
    }

    if (m_ticks >= 0)   // NB 64bit int, initialised to -1 but later populated with 32bit uint
    {
        sendMoves(filteredMoves);
    }
}
