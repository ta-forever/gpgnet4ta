#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qthread.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qudpsocket.h>

#include <cinttypes>
#include <memory>
#include <functional>

#include "tademo/HexDump.h"
#include "tademo/TPacket.h"

std::uint16_t NetworkByteOrder(std::uint16_t x)
{
    return (x >> 8) | (x << 8);
}

std::uint32_t NetworkByteOrder(std::uint32_t x)
{
    return (x << 24) | ((x & 0xff00) << 8) | ((x & 0xff0000) >> 8) | ((x & 0xff000000) >> 24);
}

const std::uint16_t* skipCWStr(const std::uint16_t* p)
{
    while (*p++ != 0);
    return p;
}

struct DPAddress
{
    std::uint16_t family;
    std::uint16_t _port;
    std::uint32_t _ipv4;
    std::uint8_t pad[8];

    std::uint16_t port() const {
        return NetworkByteOrder(_port);
    }

    void port(std::uint16_t port)
    {
        _port = NetworkByteOrder(port);
    }

    std::uint32_t address() const {
        return NetworkByteOrder(_ipv4);
    }

    void address(std::uint32_t addr) {
        _ipv4 = NetworkByteOrder(addr);
    }

};

struct DPHeader
{
    unsigned size() const {
        return size_and_token & 0x000fffff;
    }
    unsigned token() const {
        return size_and_token >> 20;
    }
    bool looksOk() const
    {
        return
            size() >= sizeof(DPHeader) &&
            (token() == 0xfab || token() == 0xcab || token() == 0xbab) &&
            address.family == 2 &&   // AF_INET
            //dialect == 0x0e && // dplay 9
            std::memcmp(address.pad, "\0\0\0\0\0\0\0\0", 8) == 0;
    }

    std::uint32_t size_and_token;
    DPAddress address;
    char actionstring[4];
    std::uint16_t command;
    std::uint16_t dialect;
};

struct DPPackedPlayer
{
    std::uint32_t size;
    std::uint32_t flags;
    std::uint32_t id;
    std::uint32_t short_name_length;
    std::uint32_t long_name_length;
    std::uint32_t service_provider_data_size;
    std::uint32_t player_data_size;
    std::uint32_t player_count;
    std::uint32_t system_player_id;
    std::uint32_t fixed_size;
    std::uint32_t dplay_version;
    std::uint32_t unknown;
};


class DplayAddressTranslater
{
    std::uint32_t newIpv4Address;
    std::uint16_t newPorts[2];

public:
    DplayAddressTranslater(std::uint32_t newIpv4Address, std::uint16_t _newPorts[2]):
        newIpv4Address(newIpv4Address)
    { 
        newPorts[0] = _newPorts[0];
        newPorts[1] = _newPorts[1];
    }

    void operator()(char *buf, int len)
    {
        for (char *ptr = buf; ptr < buf+len;) {
            DPHeader* hdr = (DPHeader*)ptr;
            if (translateHeader(ptr, hdr->size()))
            {
                translateSuperEnumPlayersReply(ptr, hdr->size()) ||
                translateForwardOrCreateRequest(ptr, hdr->size());
            }
            ptr += hdr->size();
        }
    }

    bool translateHeader(char* buf, int len)
    {
        DPHeader* dp = (DPHeader*)buf;
        if (dp->looksOk())
        {
            translateAddress(dp->address, newPorts[0]);
            return true;
        }
        else
        {
            return false;
        }
    }

    void translateAddress(DPAddress& address, std::uint16_t newPort)
    {
        qDebug() << "address translate" << QHostAddress(address.address()).toString() << address.port()
            << "->" << QHostAddress(newIpv4Address).toString() << newPort;
        address.port(newPort);
        address.address(newIpv4Address);
    }

    bool translateForwardOrCreateRequest(char* buf, int len)
    {
        DPHeader* dp = (DPHeader*)buf;
        if (dp->command != 0x2e && // DPSP_MSG_ADDFORWARD
            dp->command != 0x13 && // DPSP_MSG_ADDFORWARDREQUEST
            dp->command != 0x08 && // DPSP_MSG_CREATEPLAYER
            dp->command != 0x38    // DPSP_MSG_CREATEPLAYERVERIFY
            )
        {
            return false;
        }
        struct DPForwardOrCreateRequest
        {
            std::uint32_t id_to;
            std::uint32_t player_id;
            std::uint32_t group_id;
            std::uint32_t create_offset;
            std::uint32_t password_offset;
            DPPackedPlayer player;
            char sentinel;
        };
        DPForwardOrCreateRequest* req = (DPForwardOrCreateRequest*)(dp + 1);
        if (req->player.service_provider_data_size == 0)
        {
            return false;
        }
        DPAddress* addr = (DPAddress*)(&req->sentinel + req->player.short_name_length + req->player.long_name_length);
        for (unsigned n = 0u; n*sizeof(DPAddress) < req->player.service_provider_data_size; ++n)
        {
            translateAddress(*addr++, newPorts[n]);
        }
        return true;
    }

    bool translateSuperEnumPlayersReply(char* buf, int len)
    {
        DPHeader* dp = (DPHeader*)buf;
        if (dp->command != 0x0029) // Super Enum Players Reply
        {
            return false;
        }

        struct DPSuperEnumPlayersReplyHeader
        {
            std::uint32_t player_count;
            std::uint32_t group_count;
            std::uint32_t packed_offset;
            std::uint32_t shortcut_count;
            std::uint32_t description_offset;
            std::uint32_t name_offset;
            std::uint32_t password_offset;
        };
        DPSuperEnumPlayersReplyHeader* ephdr = (DPSuperEnumPlayersReplyHeader*)(dp + 1);
        if (ephdr->packed_offset == 0)
        {
            return true;
        }

        struct DPSuperPackedPlayerInfo
        {
            unsigned haveShortName : 1;
            unsigned haveLongName : 1;
            unsigned serviceProvideDataLengthBytes : 2;
            unsigned playerDataLengthBytes : 2;
            unsigned pad : 26;
        };
        struct DPSuperPackedPlayer
        {
            std::uint32_t size;
            std::uint32_t flags;
            std::uint32_t id;
            DPSuperPackedPlayerInfo info;
        };

        DPSuperPackedPlayer* player = (DPSuperPackedPlayer*)(buf + 0x4a-0x36 + ephdr->packed_offset);
        for (unsigned n = 0u; n < ephdr->player_count; ++n)
        {
            char* ptr = (char*)player + player->size;
            ptr += 4;  //  systemPlayerIdOrDirectPlayVersion
            if (player->info.haveShortName)
            {
                ptr = (char*)skipCWStr((std::uint16_t*)ptr);
            }
            if (player->info.haveLongName)
            {
                ptr = (char*)skipCWStr((std::uint16_t*)ptr);
            }
            if (player->info.playerDataLengthBytes == 1)
            {
                int len = *(std::uint8_t*)ptr;
                ptr += 1+len;
            }
            if (player->info.playerDataLengthBytes == 2)
            {
                int len = *(std::uint16_t*)ptr;
                ptr += 2 + len;
            }
            if (player->info.serviceProvideDataLengthBytes == 1)
            {
                int len = *(std::uint8_t*)ptr;
                ptr += 1;
            }
            if (player->info.serviceProvideDataLengthBytes == 2)
            {
                int len = *(std::uint16_t*)ptr;
                ptr += 2;
            }
            DPAddress* addr = (DPAddress*)ptr;
            translateAddress(*addr, newPorts[0]);
            addr += 1;
            translateAddress(*addr, newPorts[1]);
            player = (DPSuperPackedPlayer*)(addr + 1);
        }
        return true;
    }
};


struct TafnetMessageHeader
{
    static const unsigned ACTION_HELLO = 1;
    static const unsigned ACTION_ENUM = 2;
    static const unsigned ACTION_TCP_OPEN = 3;
    static const unsigned ACTION_TCP_DATA = 4;
    static const unsigned ACTION_TCP_CLOSE = 5;
    static const unsigned ACTION_UDP_DATA = 6;

    std::uint32_t action : 3;
    std::uint32_t sourceId : 8;
    std::uint32_t destId : 8;
    std::uint32_t data_bytes : 13;
};


class TafnetNode: public QObject
{
    const std::uint32_t m_tafnetId;
    QTcpServer m_tcpServer;
    QMap<QTcpSocket*, std::uint32_t> m_remoteTafnetIds;
    QMap<std::uint32_t, QTcpSocket*> m_tcpSockets;
    QList<QUdpSocket*> m_udpSockets;    // maybe later
    std::function<void(const TafnetMessageHeader&, char*, int)> m_handleMessage;

    virtual void onNewConnection()
    {
        QTcpSocket* clientSocket = m_tcpServer.nextPendingConnection();
        qDebug() << "[TafnetNode::onNewConnection]" << m_tafnetId << "from" << clientSocket->peerAddress().toString();
        QObject::connect(clientSocket, &QTcpSocket::readyRead, this, &TafnetNode::onReadyRead);
        QObject::connect(clientSocket, &QTcpSocket::stateChanged, this, &TafnetNode::onSocketStateChanged);
        m_remoteTafnetIds[clientSocket] = 0u;
    }

    virtual void onSocketStateChanged(QAbstractSocket::SocketState socketState)
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            std::uint32_t remoteTafnetId = m_remoteTafnetIds[sender];
            qDebug() << "[TafnetNode::onSocketStateChanged/disconnect]" << m_tafnetId << "from" << sender->peerAddress().toString() << remoteTafnetId;
            m_remoteTafnetIds.remove(sender);
            m_tcpSockets.remove(remoteTafnetId);
            delete sender;
        }
    }

    virtual void onReadyRead()
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        QByteArray datas = sender->readAll();

        char* ptr = datas.data();
        int remain = datas.size();
        while (remain >= sizeof(TafnetMessageHeader))
        {
            const TafnetMessageHeader* tafheader = (TafnetMessageHeader*)ptr;
            ptr += sizeof(TafnetMessageHeader);
            remain -= sizeof(TafnetMessageHeader);

            qDebug() << "[TafnetNode::onReadyRead]" << m_tafnetId << "from" << tafheader->sourceId << ", action=" << tafheader->action;

            if (m_remoteTafnetIds[sender] == 0u &&
                tafheader->action == TafnetMessageHeader::ACTION_HELLO)
            {
                m_remoteTafnetIds[sender] = tafheader->sourceId;
                m_tcpSockets[tafheader->sourceId] = sender;
                if (tafheader->destId == 0)
                {
                    // source doesn't know who we are.  reply the hello
                    sendHello(sender);
                }
            }

            if (remain >= (int)tafheader->data_bytes)
            {
                handleMessage(*tafheader, ptr, tafheader->data_bytes);
            }
            ptr += tafheader->data_bytes;
            remain -= tafheader->data_bytes;
        }
    }

    void sendHello(QTcpSocket* socket)
    {
        TafnetMessageHeader header;
        header.action = TafnetMessageHeader::ACTION_HELLO;
        header.sourceId = m_tafnetId;
        header.destId = m_remoteTafnetIds[socket];  // 0 if unknown
        header.data_bytes = 0;
        socket->write((const char*)&header, sizeof(header));
        socket->flush();
    }

    virtual void handleMessage(const TafnetMessageHeader& tafheader, char* data, int len)
    {
        m_handleMessage(tafheader, data, len);
    }

public:
    TafnetNode(std::uint32_t tafnetId, QHostAddress bindAddress, quint16 bindPort):
        m_tafnetId(tafnetId)
    {
        qDebug() << "[TafnetNode::TafnetNode] node" << m_tafnetId << "tcp binding to" << bindAddress.toString() << ":" << bindPort;
        m_tcpServer.listen(bindAddress, bindPort);
        QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &TafnetNode::onNewConnection);
    }

    void setHandler(const std::function<void(const TafnetMessageHeader&, char*, int)>& f)
    {
        m_handleMessage = f;
    }

    std::uint32_t getTafnetId()
    {
        return m_tafnetId;
    }

    bool connectToPeer(QHostAddress peer, quint16 peerPort)
    {
        qDebug() << "[TafnetNode::connectToPeer] node" << m_tafnetId << "connecting to" << peer.toString() << ":" << peerPort;
        QTcpSocket* socket = new QTcpSocket();
        socket->connectToHost(peer, peerPort);
        socket->waitForConnected(2000);
        if (socket->isOpen())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, this, &TafnetNode::onReadyRead);
            QObject::connect(socket, &QTcpSocket::stateChanged, this, &TafnetNode::onSocketStateChanged);
            m_remoteTafnetIds[socket] = 0;
            sendHello(socket);
            return true;
        }
        else
        {
            delete socket;
            return false;
        }
    }

    void forwardGameData(std::uint32_t destNodeId, std::uint32_t action, char* data, int len)
    {
        qDebug() << "[TafnetNode::forwardGameData] node" << m_tafnetId << "forwarding to node" << destNodeId << ", action=" << action;
        if (m_tcpSockets.count(destNodeId) == 0)
        {
            return;
        }

#ifdef _DEBUG
        TADemo::HexDump(data, len, std::cout);
#endif

        QByteArray buf;
        buf.resize(sizeof(TafnetMessageHeader) + len);

        TafnetMessageHeader* header = (TafnetMessageHeader*)buf.data();
        header->sourceId = this->getTafnetId();
        header->destId = destNodeId;
        header->action = action;
        header->data_bytes = len;
        std::memcpy(buf.data()+sizeof(header), data, len);

        QTcpSocket* socket = m_tcpSockets[destNodeId];
        socket->write(buf);
        socket->flush();
    }
};


class GameSender : public QObject
{
    QTcpSocket m_enumSocket;
    QTcpSocket m_tcpSocket;
    QSharedPointer<QUdpSocket> m_udpSocket;

    QHostAddress m_gameAddress;
    quint16 m_enumPort;
    quint16 m_tcpPort;
    quint16 m_udpPort;



public:
    GameSender(QHostAddress gameAddress, quint16 enumPort) :
        m_gameAddress(gameAddress),
        m_enumPort(enumPort),
        m_udpSocket(new QUdpSocket()),
        m_tcpPort(0),
        m_udpPort(0)
    { }

    virtual void setTcpPort(quint16 port)
    {
        m_tcpPort = port;
    }

    virtual void setUdpPort(quint16 port)
    {
        m_udpPort = port;
    }

    virtual void enumSessions(char* data, int len, QHostAddress replyAddress, quint16 replyPorts[2])
    {
        qDebug() << "[GameSender::enumSessions]" << m_gameAddress.toString() << ":" << m_enumPort << "/ reply to" << replyAddress.toString() << ":" << replyPorts[0] << '/' << replyPorts[1];
        DplayAddressTranslater(replyAddress.toIPv4Address(), replyPorts)(data, len);
        //m_enumSocket.writeDatagram(data, len, m_gameAddress, m_enumPort);
        //m_enumSocket.flush();
        m_enumSocket.connectToHost(m_gameAddress, m_enumPort);
        m_enumSocket.waitForConnected(30);
        m_enumSocket.write(data, len);
        m_enumSocket.flush();
        m_enumSocket.disconnectFromHost();
    }

    virtual bool openTcpSocket(int timeoutMillisecond)
    {
        if (m_tcpPort > 0 && !m_tcpSocket.isOpen())
        {
            qDebug() << "[GameSender::openTcpSocket]" << m_gameAddress.toString() << ":" << m_tcpPort;
            m_tcpSocket.connectToHost(m_gameAddress, m_tcpPort);
            m_tcpSocket.waitForConnected(timeoutMillisecond);
        }
        return m_tcpSocket.isOpen();
    }

    virtual void sendTcpData(char* data, int len, QHostAddress replyAddress, quint16 replyPorts[2])
    {
        if (!m_tcpSocket.isOpen())
        {
            openTcpSocket(3);
        }
        qDebug() << "[GameSender::sendTcpData]" << m_gameAddress.toString() << m_tcpPort << "/ reply to" << replyAddress.toString() << ":" << replyPorts[0] << '/' << replyPorts[1];
        DplayAddressTranslater(replyAddress.toIPv4Address(), replyPorts)(data, len);
#ifdef _DEBUG
        TADemo::HexDump(data, len, std::cout);
#endif
        m_tcpSocket.write(data, len);
        m_tcpSocket.flush();
    }

    virtual void closeTcpSocket()
    {
        qDebug() << "[GameSender::closeTcpSocket]" << m_gameAddress.toString() << ":" << m_tcpPort;
        m_tcpSocket.disconnectFromHost();
    }

    virtual void sendUdpData(char* data, int len)
    {
        if (m_udpPort > 0)
        {
            qDebug() << "[GameSender::sendUdpData]" << m_gameAddress.toString() << ":" << m_udpPort;
#ifdef _DEBUG
            TADemo::HexDump(data, len, std::cout);
#endif
            m_udpSocket->writeDatagram(data, len, m_gameAddress, m_udpPort);
            m_udpSocket->flush();
        }
    }

    virtual QSharedPointer<QUdpSocket> getUdpSocket()
    {
        return m_udpSocket;
    }
};


class GameReceiver: public QObject
{
    QHostAddress m_bindAddress;
    quint16 m_enumPort;
    quint16 m_tcpPort;
    quint16 m_udpPort;
    GameSender* m_sender;

    QTcpServer m_tcpServer;
    QTcpServer m_enumServer;
    QSharedPointer<QUdpSocket> m_udpSocket;
    QList<QAbstractSocket*> m_sockets;   // those associated with m_tcpServer, and also those not
    std::function<void(QAbstractSocket*, int, char*, int)> m_handleMessage;

    int getChannelCodeFromSocket(QAbstractSocket* socket)
    {
        if (socket->localPort() == m_enumPort)
        {
            return CHANNEL_ENUM;
        }
        else if (socket->socketType() == QAbstractSocket::SocketType::TcpSocket && socket->localPort() == m_tcpPort)
        {
            return CHANNEL_TCP;
        }
        else if (socket->socketType() == QAbstractSocket::SocketType::UdpSocket && socket->localPort() == m_udpPort)
        {
            return CHANNEL_UDP;
        }
        return 0;
    }

    virtual void onNewConnection()
    {
        QTcpSocket* clientSocket = m_tcpServer.nextPendingConnection();
        if (clientSocket == NULL)
        {
            clientSocket = m_enumServer.nextPendingConnection();
        }
        if (clientSocket == NULL)
        {
            qDebug() << "[GameReceiver::onNewConnection] unexpected connection";
            return;
        }
        qDebug() << "[GameReceiver::onNewConnection]" << clientSocket->localAddress().toString() << ":" << clientSocket->localPort() << "from" << clientSocket->peerAddress().toString();
        QObject::connect(clientSocket, &QTcpSocket::readyRead, this, &GameReceiver::onReadyReadTcp);
        QObject::connect(clientSocket, &QTcpSocket::stateChanged, this, &GameReceiver::onSocketStateChanged);
        m_sockets.push_back(clientSocket);
    }

    virtual void onSocketStateChanged(QAbstractSocket::SocketState socketState)
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            qDebug() << "[GameReceiver::onSocketStateChanged/UnconnectedState]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString();
            m_sockets.removeOne(sender);
            //delete sender;
        }
    }

    virtual void setSenderGamePorts(char *data, int len)
    {
        DPHeader* header = (DPHeader*)data;
        if (m_sender && len >= sizeof(DPHeader) && header->looksOk())
        {
            m_sender->setTcpPort(header->address.port());
            m_sender->setUdpPort(header->address.port() + 50);
            m_sender = NULL;
        }
    }

    virtual void onReadyReadTcp()
    {
        QAbstractSocket* sender = static_cast<QAbstractSocket*>(QObject::sender());
        qDebug() << "[GameReceiver::onReadyReadTcp]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString() << ":" << sender->peerPort();
        QByteArray datas = sender->readAll();
        setSenderGamePorts(datas.data(), datas.size());
        handleMessage(sender, getChannelCodeFromSocket(sender), datas.data(), datas.size());
    }

    virtual void onReadyReadUdp()
    {
        //QByteArray datas;
        //datas.resize(m_proxySocket.pendingDatagramSize());
        //QHostAddress sender;
        //quint16 senderPort;
        //m_proxySocket.readDatagram(datas.data(), datas.size(), &sender, &senderPort);
        QUdpSocket* sender = dynamic_cast<QUdpSocket*>(QObject::sender());
        qDebug() << "[GameReceiver::onReadyReadUdp]" << sender->localAddress().toString() << ":" << sender->localPort() << "from" << sender->peerAddress().toString() << ":" << sender->peerPort();
        QByteArray datas;
        datas.resize(sender->pendingDatagramSize());
        QHostAddress senderAddress;
        quint16 senderPort;
        sender->readDatagram(datas.data(), datas.size(), &senderAddress, &senderPort);
        handleMessage(sender, CHANNEL_UDP, datas.data(), datas.size());
    }

    virtual void handleMessage(QAbstractSocket* receivingSocket, int channel, char* data, int len)
    {
        m_handleMessage(receivingSocket, channel, data, len);
    }

public:
    static const int CHANNEL_ENUM = 1;
    static const int CHANNEL_TCP = 2;
    static const int CHANNEL_UDP = 3;

    GameReceiver(QHostAddress bindAddress, quint16 enumPort, quint16 tcpPort, quint16 udpPort, GameSender *sender):
        m_bindAddress(bindAddress),
        m_enumPort(enumPort),
        m_tcpPort(tcpPort),
        m_udpPort(udpPort),
        m_sender(sender),
        m_udpSocket(sender->getUdpSocket())
    {
        qDebug() << "[GameReceiver::GameReceiver] tcp binding to" << bindAddress.toString() << ":" << tcpPort;
        m_tcpServer.listen(bindAddress, tcpPort);
        QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &GameReceiver::onNewConnection);

        qDebug() << "[GameReceiver::GameReceiver] tcp binding to" << bindAddress.toString() << ":" << enumPort;
        m_enumServer.listen(bindAddress, enumPort);
        QObject::connect(&m_enumServer, &QTcpServer::newConnection, this, &GameReceiver::onNewConnection);

        QUdpSocket* udpSocket = m_udpSocket.data(); //new QUdpSocket();
        qDebug() << "[GameReceiver::GameReceiver] udp binding to" << bindAddress.toString() << ":" << udpPort;
        udpSocket->bind(bindAddress, udpPort);
        QObject::connect(udpSocket, &QTcpSocket::readyRead, this, &GameReceiver::onReadyReadUdp);
    }

    void setHandler(const std::function<void(QAbstractSocket*, int, char*, int)>& f)
    {
        m_handleMessage = f;
    }

    QHostAddress getBindAddress()
    {
        return m_tcpServer.serverAddress();
    }

    quint16 getEnumListenPort()
    {
        return m_enumPort;
    }

    quint16 getTcpListenPort()
    {
        return m_tcpPort;
    }

    quint16 getUdpListenPort()
    {
        return m_udpPort;
    }

    quint16* getListenPorts(quint16 ports[2])
    {
        ports[0] = m_tcpPort;
        ports[1] = m_udpPort;
        return ports;
    }
};


class TafnetGameNode
{
    TafnetNode* m_tafnetNode;
    QMap<std::uint32_t, QSharedPointer<GameSender> > m_gameSenders;     // keyed by Tafnet sourceId
    QMap<std::uint32_t, QSharedPointer<GameReceiver> > m_gameReceivers; // keyed by Tafnet sourceId
    QMap<quint32, std::uint32_t> m_remoteTafnetIds;                     // keyed by gameReceiver's receive socket port (both tcp and udp)

    std::function<GameSender * ()> m_gameSenderFactory;
    std::function<GameReceiver * (GameSender*)> m_gameReceiverFactory;

    GameSender* getGameSender(std::uint32_t remoteTafnetId)
    {
        QSharedPointer<GameSender>& gameSender = m_gameSenders[remoteTafnetId];
        if (!gameSender)
        {
            gameSender.reset(m_gameSenderFactory());
        }
        return gameSender.data();
    }

    GameReceiver* getGameReceiver(std::uint32_t remoteTafnetId, GameSender* sender)
    {
        QSharedPointer<GameReceiver>& gameReceiver = m_gameReceivers[remoteTafnetId];
        if (!gameReceiver)
        {
            gameReceiver.reset(m_gameReceiverFactory(sender));
            if (m_remoteTafnetIds.count(gameReceiver->getTcpListenPort()) == 0) m_remoteTafnetIds[gameReceiver->getTcpListenPort()] = remoteTafnetId;
            if (m_remoteTafnetIds.count(gameReceiver->getUdpListenPort()) == 0) m_remoteTafnetIds[gameReceiver->getUdpListenPort()] = remoteTafnetId;
            if (m_remoteTafnetIds.count(gameReceiver->getEnumListenPort()) == 0) m_remoteTafnetIds[gameReceiver->getEnumListenPort()] = remoteTafnetId;
            gameReceiver->setHandler([this](QAbstractSocket* receivingSocket, int channelCode, char* data, int len) {
                this->handleGameData(receivingSocket, channelCode, data, len);
            });
        }
        return gameReceiver.data();
    }

    virtual void handleGameData(QAbstractSocket* receivingSocket, int channelCode, char* data, int len)
    {
        qDebug() << "[TafnetGameNode::handleGameData] recievePort=" << receivingSocket->localPort() << "channelCode=" << channelCode << "len=" << len;
        if (m_remoteTafnetIds.count(receivingSocket->localPort()) == 0)
        {
            return;
        }

        std::uint32_t destNodeId = m_remoteTafnetIds[receivingSocket->localPort()];
        if (destNodeId == 0)
        {
            qDebug() << "ERROR: unable to determine destination tafnetid for game data received on port" << receivingSocket->localPort();
            return;
        }

        if (channelCode == GameReceiver::CHANNEL_UDP)
        {
            m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_UDP_DATA, data, len);
        }
        else if (channelCode == GameReceiver::CHANNEL_TCP)
        {
            m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_TCP_DATA, data, len);
        }
        else if (channelCode == GameReceiver::CHANNEL_ENUM)
        {
            m_tafnetNode->forwardGameData(destNodeId, TafnetMessageHeader::ACTION_ENUM, data, len);
        }
    }

public:
    TafnetGameNode(TafnetNode* tafnetNode, std::function<GameSender * ()> gameSenderFactory, std::function<GameReceiver * (GameSender*)> gameReceiverFactory) :
        m_tafnetNode(tafnetNode),
        m_gameSenderFactory(gameSenderFactory),
        m_gameReceiverFactory(gameReceiverFactory)
    {
        m_tafnetNode->setHandler([this](const TafnetMessageHeader& tafheader, char* data, int len) {
            GameSender* gameSender = getGameSender(tafheader.sourceId);
            GameReceiver* gameReceiver = getGameReceiver(tafheader.sourceId, gameSender);
            quint16 replyPorts[2];
            qDebug() << "[TafnetGameNode::<tafmsg handler>] me=" << m_tafnetNode->getTafnetId() << "to=" << tafheader.destId << "from=" << tafheader.sourceId << "action=" << tafheader.action;
            switch (tafheader.action)
            {
            case TafnetMessageHeader::ACTION_HELLO:
                // no further action beyond creating a gameSender/Receiver required
                break;
            case TafnetMessageHeader::ACTION_ENUM:
                gameSender->enumSessions(data, len, gameReceiver->getBindAddress(), gameReceiver->getListenPorts(replyPorts));
                break;
            case TafnetMessageHeader::ACTION_TCP_OPEN:
                gameSender->openTcpSocket(3);
                break;
            case TafnetMessageHeader::ACTION_TCP_DATA:
                gameSender->sendTcpData(data, len, gameReceiver->getBindAddress(), gameReceiver->getListenPorts(replyPorts));
                break;
            case TafnetMessageHeader::ACTION_TCP_CLOSE:
                gameSender->closeTcpSocket();
                break;
            case TafnetMessageHeader::ACTION_UDP_DATA:
                gameSender->sendUdpData(data, len);
                break;
            default:
                qDebug() << "[TafnetGameNode::<tafmsg handler>] ERROR unknown action!";
                break;
            };
        });
    }
};


int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("taproxy");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("test for proxying data between games");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("host1", "hostname of 1st TA instance eg 192.168.1.109", "host1"));
    parser.addOption(QCommandLineOption("port1", "game port of 1st TA instance eg 2300", "port1"));
    parser.addOption(QCommandLineOption("host2", "hostname of 2nd TA instance eg 192.168.1.104", "host2"));
    parser.addOption(QCommandLineOption("port2", "game port of 2nd TA instance eg 2300", "port2"));
    parser.addOption(QCommandLineOption("proxyaddr", "address on which to bind the proxy", "proxyaddr"));
    parser.process(app);

    const int proxyGamePort1 = 2310;
    const int proxyGamePort2 = 2311;

    /*
    DplayAddressTranslater tx1(QHostAddress("192.168.1.109").toIPv4Address(), 2310);
    DplayAddressTranslater tx2(QHostAddress("192.168.1.109").toIPv4Address(), 2311);
    DplayTcpPortProxy ta1Enum("127.0.0.1", 47624, "192.168.1.109", 47624, tx1);
    DplayTcpPortProxy ta2Enum("192.168.1.104", 47624, "192.168.1.109", 0, tx2);
    DplayTcpPortProxy ta1tcp("127.0.0.1", 2300, "192.168.1.109", 2310, tx1);
    DplayTcpPortProxy ta2tcp("192.168.1.104", 2300, "192.168.1.109", 2311, tx2);
    DplayUdpPortProxy ta1udp("127.0.0.1", 2350, "192.168.1.109", 2310, tx1);
    DplayUdpPortProxy ta2udp("192.168.1.104", 2350, "192.168.1.109", 2311, tx2);

    ta1Enum.setPeer(&ta2Enum);
    ta2Enum.setPeer(&ta1Enum);
    ta1tcp.setPeer(&ta2tcp);
    ta2tcp.setPeer(&ta1tcp);
    ta1udp.setPeer(&ta2udp);
    ta2udp.setPeer(&ta1udp);
    */

    TafnetNode node1(1, QHostAddress("192.168.1.109"), 6112);
    TafnetNode node2(2, QHostAddress("192.168.1.109"), 6113);

    quint16 nextPort = 2310;
    TafnetGameNode ta1(
        &node1,
        []() { return new GameSender(QHostAddress("127.0.0.1"), 47624); },
        [&nextPort](GameSender* sender) { return new GameReceiver(QHostAddress("127.0.0.1"), 47625, nextPort++, nextPort++, sender); });
    TafnetGameNode ta2(
        &node2,
        []() { return new GameSender(QHostAddress("192.168.1.104"), 47624); },
        [&nextPort](GameSender* sender) { return new GameReceiver(QHostAddress("192.168.1.109"), 47624, nextPort++, nextPort++, sender); });

    node1.connectToPeer(QHostAddress("192.168.1.109"), 6113);

    return app.exec();
}
