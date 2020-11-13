#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include <QtCore/qthread.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtNetwork/qudpsocket.h>

#include <cinttypes>

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
    std::uint16_t port;
    std::uint32_t ipv4;
    std::uint8_t pad[8];
};

struct DPHeader
{
    unsigned size() {
        return size_and_token & 0x000fffff;
    }
    unsigned token() {
        return size_and_token >> 20;
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
    std::uint16_t newPort;

public:
    DplayAddressTranslater(std::uint32_t newIpv4Address, std::uint16_t newPort):
        newIpv4Address(newIpv4Address),
        newPort(newPort)
    { }

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
        if (dp->size() >= sizeof(DPHeader) &&
            (dp->token() == 0xfab || dp->token() == 0xcab || dp->token() == 0xbab) &&
            dp->address.family == 2 &&   // AF_INET
            //dp->dialect == 0x0e && // dplay 9
            std::memcmp(dp->address.pad, "\0\0\0\0\0\0\0\0", 8) == 0)
        {
            translateAddress(dp->address);
            return true;
        }
        else
        {
            return false;
        }
    }

    void translateAddress(DPAddress& address)
    {
        qDebug() << "address translate" << QHostAddress(address.ipv4) << NetworkByteOrder(address.port)
            << "->" << QHostAddress(newIpv4Address) << newPort;
        address.port = NetworkByteOrder(newPort);
        address.ipv4 = NetworkByteOrder(newIpv4Address);
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
        for (unsigned n = 0u; n < req->player.service_provider_data_size; n += sizeof(DPAddress))
        {
            translateAddress(*addr++);
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
            translateAddress(*addr);
            addr += 1;
            translateAddress(*addr);
            player = (DPSuperPackedPlayer*)(addr + 1);
        }
        return true;
    }
};


class DplayUdpPortProxy : public QObject
{
    QString m_tahost;
    int m_taport;
    QUdpSocket m_taSocket;
    QUdpSocket m_proxySocket;
    DplayAddressTranslater m_addrTranslate;
    DplayUdpPortProxy* m_peer;

    void onReadyRead()
    {
        qDebug() << m_tahost << m_taport << "udp onReadyRead";
        QByteArray datas;
        datas.resize(m_proxySocket.pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;
        m_proxySocket.readDatagram(datas.data(), datas.size(), &sender, &senderPort);
        m_peer->handlePeerData(datas);
    }

public:
    DplayUdpPortProxy(QString tahost, int taport, QString proxyBindAddress, int proxyBindPort, DplayAddressTranslater addrTranslate) :
        m_tahost(tahost),
        m_taport(taport),
        m_taSocket(this),
        m_proxySocket(this),
        m_addrTranslate(addrTranslate),
        m_peer(NULL)
    {
        if (proxyBindPort > 0)
        {
            m_proxySocket.bind(QHostAddress(proxyBindAddress), proxyBindPort);
            QObject::connect(&m_proxySocket, &QUdpSocket::readyRead, this, &DplayUdpPortProxy::onReadyRead);
        }
    }

    DplayUdpPortProxy* getPeer() {
        return m_peer;
    }

    void setPeer(DplayUdpPortProxy* peer) {
        m_peer = peer;
    }

    // peer received data from game, we need to forward to our game
    void handlePeerData(QByteArray datas)
    {
        qDebug() << m_tahost << m_taport << "udp handlePeerData";
#ifdef _DEBUG
        TADemo::HexDump(datas.data(), datas.size(), std::cout);
#endif
        m_addrTranslate(datas.data(), datas.size());
#ifdef _DEBUG
        TADemo::HexDump(datas.data(), datas.size(), std::cout);
#endif
        m_taSocket.writeDatagram(datas, QHostAddress(m_tahost), m_taport);
        m_taSocket.flush();
    }
};


class DplayTcpPortProxy : public QObject
{
    QString m_tahost;
    int m_taport;
    QTcpSocket m_taSocket;
    QTcpServer m_proxyService;
    QList<QTcpSocket*> m_proxySockets;
    DplayAddressTranslater m_addrTranslate;
    DplayTcpPortProxy* m_peer;

    void onNewConnection()
    {
        qDebug() << m_tahost << m_taport << "tcp onNewConnection";
        if (m_peer && m_peer->handlePeerConnect())
        {
            QTcpSocket* clientSocket = m_proxyService.nextPendingConnection();
            QObject::connect(clientSocket, &QTcpSocket::readyRead, this, &DplayTcpPortProxy::onReadyRead);
            QObject::connect(clientSocket, &QTcpSocket::stateChanged, this, &DplayTcpPortProxy::onSocketStateChanged);
            m_proxySockets.push_back(clientSocket);
        }
    }

    void onSocketStateChanged(QAbstractSocket::SocketState socketState)
    {
        qDebug() << m_tahost << m_taport << "tcp onSocketStateChanged" << socketState;
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            m_peer->handlePeerDisconnect();
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            sender->close();
            m_proxySockets.removeOne(sender);
        }
    }

    void onReadyRead()
    {
        qDebug() << m_tahost << m_taport << "tcp onReadyRead";
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        QByteArray datas = sender->readAll();
        m_peer->handlePeerData(datas);
    }

public:
    DplayTcpPortProxy(QString tahost, int taport, QString proxyBindAddress, int proxyBindPort, DplayAddressTranslater addrTranslate) :
        m_tahost(tahost),
        m_taport(taport),
        m_taSocket(this),
        m_proxyService(this),
        m_addrTranslate(addrTranslate),
        m_peer(NULL)
    {
        if (proxyBindPort > 0)
        {
            m_proxyService.listen(QHostAddress(proxyBindAddress), proxyBindPort);
            QObject::connect(&m_proxyService, &QTcpServer::newConnection, this, &DplayTcpPortProxy::onNewConnection);
        }
    }

    DplayTcpPortProxy* getPeer() {
        return m_peer;
    }

    void setPeer(DplayTcpPortProxy* peer) {
        m_peer = peer;
    }

    bool handlePeerConnect()
    {
        qDebug() << m_tahost << m_taport << "tcp handlePeerConnect";
        m_taSocket.connectToHost(QHostAddress(m_tahost), m_taport);
        m_taSocket.waitForConnected(2000);
        if (m_taSocket.isOpen())
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    void handlePeerDisconnect()
    {
        qDebug() << m_tahost << m_taport << "tcp handlePeerDisconnect";
        m_taSocket.disconnect();
    }

    // peer received data from game, we need to forward to our game
    void handlePeerData(QByteArray datas)
    {
        if (!m_taSocket.isOpen())
        {
            handlePeerConnect();
        }
        if (m_taSocket.isOpen())
        {
            qDebug() << m_tahost << m_taport << "tcp handlePeerData";
#ifdef _DEBUG
            TADemo::HexDump(datas.data(), datas.size(), std::cout);
#endif
            m_addrTranslate(datas.data(), datas.size());
#ifdef _DEBUG
            TADemo::HexDump(datas.data(), datas.size(), std::cout);
#endif

            m_taSocket.write(datas.data(), datas.size());
            m_taSocket.flush();
        }
        else
        {
            qDebug() << m_tahost << m_taport << "tcp handlePeerData: ta socket is closed!";
        }
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

    //DplayAddressTranslater tx1(QHostAddress(parser.value("proxyaddr")).toIPv4Address(), proxyGamePort1);
    //DplayAddressTranslater tx2(QHostAddress(parser.value("proxyaddr")).toIPv4Address(), proxyGamePort2);
    //DplayTcpPortProxy ta1Enum(parser.value("host1"), 47624, parser.value("proxyaddr"), 47624, tx1);
    //DplayTcpPortProxy ta2Enum(parser.value("host2"), 47624, parser.value("proxyaddr"), 0, tx2);
    //DplayTcpPortProxy ta1Game(parser.value("host1"), parser.value("port1").toInt(), parser.value("proxyaddr"), proxyGamePort1, tx1);
    //DplayTcpPortProxy ta2Game(parser.value("host2"), parser.value("port2").toInt(), parser.value("proxyaddr"), proxyGamePort2, tx2);

    //DplayAddressTranslater tx1(QHostAddress("192.168.1.109").toIPv4Address(), 2310);
    //DplayAddressTranslater tx2(QHostAddress("14.203.145.183").toIPv4Address(), 2311);
    //DplayAddressTranslater tx1(QHostAddress("0.0.0.0").toIPv4Address(), 2310);
    //DplayAddressTranslater tx2(QHostAddress("0.0.0.0").toIPv4Address(), 2311);
    //DplayTcpPortProxy ta1Enum("192.168.1.109", 47624, "192.168.1.109", 0, tx1);
    //DplayTcpPortProxy ta2Enum("139.180.169.179", 47624, "192.168.1.109", 47625, tx2);
    //DplayTcpPortProxy ta1Game("192.168.1.109", 2300, "192.168.1.109", 2310, tx1);
    //DplayTcpPortProxy ta2Game("139.180.169.179", 2300, "192.168.1.109", 2311, tx2);

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

    return app.exec();
}
