#include "TaDemoCompilerClient.h"
#include "tademo/HexDump.h"
#include <sstream>

TaDemoCompilerClient::TaDemoCompilerClient(QHostAddress taDemoCompilerAddress, quint16 taDemoCompilerPort, quint32 tafGameId) :
    m_taDemoCompilerAddress(taDemoCompilerAddress),
    m_taDemoCompilerPort(taDemoCompilerPort),
    m_tafGameId(tafGameId),
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
        throw std::runtime_error("unable to connect to TA Demo Compiler");
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

void TaDemoCompilerClient::sendHello(quint32 gameId, quint32 dplayPlayerId)
{
    qInfo() << "[TaDemoCompilerClient::sendHello] gameid,dplayPlayerId" << gameId << dplayPlayerId;
    m_protocol.sendCommand("Hello", 2);
    m_protocol.sendArgument(gameId);
    m_protocol.sendArgument(dplayPlayerId);
}

void TaDemoCompilerClient::sendGameInfo(quint16 maxUnits, QString mapName)
{
    qInfo() << "[TaDemoCompilerClient::sendGameInfo] numPlayers,maxUnits,mapName" << maxUnits << mapName;
    m_protocol.sendCommand("GameInfo", 2);
    m_protocol.sendArgument(maxUnits);
    m_protocol.sendArgument(mapName.toUtf8());
}

void TaDemoCompilerClient::sendGamePlayer(qint8 side, QString name, QByteArray statusMessage)
{
    qInfo() << "[TaDemoCompilerClient::sendGamePlayer] side,name" << side << name;
    m_protocol.sendCommand("GamePlayer", 3);
    m_protocol.sendArgument(side);
    m_protocol.sendArgument(name.toUtf8());
    m_protocol.sendArgument(statusMessage);
}

void TaDemoCompilerClient::sendGamePlayerNumber(quint32 dplayId, quint8 number)
{
    qInfo() << "[TaDemoCompilerClient::sendGamePlayerNumber] dplayid,number" << dplayId << number;
    m_protocol.sendCommand("GamePlayerNumber", 2);
    m_protocol.sendArgument(dplayId);
    m_protocol.sendArgument(number);
}

void TaDemoCompilerClient::sendGamePlayerLoading(QSet<quint32> lockedInPlayers)
{
    qInfo() << "[TaDemoCompilerClient::sendGamePlayerLoading]";
    m_protocol.sendCommand("GamePlayerLoading", lockedInPlayers.size());
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
    qInfo() << "[TaDemoCompilerClient::sendUnitData]";
    m_protocol.sendCommand("UnitData", 1);
    m_protocol.sendArgument(unitData);
}

void TaDemoCompilerClient::sendMoves(QByteArray moves)
{
    qInfo() << "[TaDemoCompilerClient::sendMoves]";

    std::ostringstream ss;
    ss << "\n";
    TADemo::HexDump(moves.data(), moves.size(), ss);
    qInfo() << ss.str().c_str();

    m_protocol.sendCommand("Move", 1);
    m_protocol.sendArgument(moves);
}

void TaDemoCompilerClient::onDplaySuperEnumPlayerReply(std::uint32_t dplayId, const std::string& name, TADemo::DPAddress* , TADemo::DPAddress* )
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
            sendHello(m_tafGameId, dplayId);
        }
    }
}

void TaDemoCompilerClient::onDplayCreateOrForwardPlayer(std::uint16_t command, std::uint32_t dplayId, const std::string& name, TADemo::DPAddress* tcp, TADemo::DPAddress* udp)
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
            sendHello(m_tafGameId, dplayId);
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
    const std::vector<TADemo::bytestring>& subpaks)
{
    if (!isLocalSource) //sourceDplayId != m_localPlayerDplayId)
    {
        return;
    }

    QByteArray filteredMoves(1, 0x03);  // uncompressed, no checksum no timestamp
    TADemo::SmartPaker smartPaker;
    for (const TADemo::bytestring& s : subpaks)
    {
        switch (TADemo::SubPacketCode(s[0]))
        {
        case TADemo::SubPacketCode::PLAYER_INFO_20:
        {
            TADemo::TPlayerInfo playerInfo(s);
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
        case TADemo::SubPacketCode::UNIT_DATA_1A:
        {
            TADemo::TUnitData ud(TADemo::bytestring((std::uint8_t*)s.data(), s.size()));
            if (ud.sub == 2 || ud.sub == 3)
            {
                std::ostringstream ss;
                ss << "unitdata:\n";
                TADemo::HexDump(s.data(), s.size(), ss);
                qInfo() << ss.str().c_str();
                sendUnitData(QByteArray((char*)s.data(), s.size()));
            }
            break;
        }
        case TADemo::SubPacketCode::LOADING_STARTED_08:
        {
            sendGamePlayerLoading(m_dpConnectedPlayers);
            break;
        }
        case TADemo::SubPacketCode::LOADING_PROGRESS_2A:
        {
            if (s[1] == 100)
            {
                m_ticks = 0;
            }
            break;
        }
        case TADemo::SubPacketCode::UNIT_STAT_AND_MOVE_2C:
        {
            // NB 32bit uint to 64bit int
            m_ticks = *(std::uint32_t*)(&s[3]);
            break;
        }
        default:
            break;
        };

        if (s[0] == std::uint8_t(TADemo::SubPacketCode::UNIT_STAT_AND_MOVE_2C))
        {
            TADemo::bytestring bs = smartPaker(s);
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
    else
    {
        qInfo() << "[TaDemoCompilerClient::onTaPacket] not sending because m_ticks=" << m_ticks;
    }
}
