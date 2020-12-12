#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include "tafnet/TafnetGameNode.h"
#include "GameMonitor2.h"

using namespace tafnet;

#include "tademo/TPacket.h"
#include "tademo/HexDump.h"
#include "tademo/TAPacketParser.h"

class GameEventPrinter: public GameEventHandler
{
public:
    virtual void onGameSettings(const std::string &mapName, std::uint16_t maxUnits)
    {
        std::cout << "[GameEventPrinter::onGameSettings] mapName=" << mapName << ", maxUnits=" << maxUnits << std::endl;
    }

    virtual void onPlayerStatus(const PlayerData &player, const std::set<std::string> & mutualAllies)
    {
        std::cout << "[GameEventPrinter::onPlayerStatus] ";
        player.print(std::cout);
        std::cout << ", allies=";
        for (const std::string &ally : mutualAllies)
        {
            std::cout << ally << ',';
        }
        std::cout << std::endl;
    }

    virtual void onGameStarted(std::uint32_t tick, bool teamsFrozen)
    {
        std::cout << "[GameEventPrinter::onGameStarted] tick=" << tick << ", teamFrozen=" << teamsFrozen << std::endl;
    }

    virtual void onGameEnded(const GameResult &gameResult)
    {
        std::cout << "[GameEventPrinter::onGameEnded]\n";
        gameResult.print(std::cout);
    }

    virtual void onChat(const std::string& msg, bool isLocalPlayerSource)
    {
        std::cout << "[GameEventPrinter::onChat]" << msg << (isLocalPlayerSource ? "local player" : "remote player");
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

    // hog the UDP enumeration ports so TA can't use them.  We want TA to respond to our proxied TCP enumeration requests only
    QUdpSocket udp47624hog[2];
    udp47624hog[0].bind(QHostAddress("192.168.1.109"), 47624);
    udp47624hog[1].bind(QHostAddress("192.168.1.59"), 47624);

    // TafnetNodes wrap all TA traffic in a UDP protocol suitable for use with FAF ICE adapter
    TafnetNode node1(1, true, QHostAddress("127.0.0.1"), 6111);
    TafnetNode node2(2, false, QHostAddress("127.0.0.1"), 6112);
    TafnetNode node3(3, false, QHostAddress("127.0.0.1"), 6113);

    // The GameEventHandler responds to high-level game events such as map selected, player allied, game started, game ended
    GameEventPrinter gameEventHandler;
    // GameMonitor interprets parsed packets to infer high level game events, which it pushses to a GameEventHandler
    GameMonitor2 gameMonitor(&gameEventHandler, 1800, 60);
    // TAPacketParser parses the network datagrams and pushes results to a GameMonitor
    TADemo::TAPacketParser taPacketParser(&gameMonitor, true);

    // A TafnetGameNode bridges a game instance (using GameReceiver and GameSender) with remote peers via a TafnetNode,
    // and to other consumers of game data (eg GameMonitorPrinter) via a TAPacketParser
    TafnetGameNode ta1(
        &node1,
        &taPacketParser,
        []() { return new GameSender(QHostAddress("127.0.0.1"), 47624); },
        [](QSharedPointer<QUdpSocket> udpSocket) { return new GameReceiver(QHostAddress("127.0.0.1"), 0, 0, udpSocket); });

    TafnetGameNode ta2(
        &node2,
        NULL,
        []() { return new GameSender(QHostAddress("192.168.1.104"), 47624); },
        [](QSharedPointer<QUdpSocket> udpSocket) { return new GameReceiver(QHostAddress("192.168.1.109"), 0, 0, udpSocket); });

    TafnetGameNode ta3(
        &node3,
        NULL,
        []() { return new GameSender(QHostAddress("192.168.1.117"), 47624); },
        [](QSharedPointer<QUdpSocket> udpSocket) { return new GameReceiver(QHostAddress("192.168.1.59"), 0, 0, udpSocket); });

    // these connections would normally be made in response to messages from GPGNet
    gameMonitor.setHostPlayerName("Axle1975");  // Game monitor needs to be told who is host since it can't(?) be inferred from packet data
    gameMonitor.setLocalPlayerName("Axle1975");  // Game monitor needs to be told who local player is since it can't(?) be inferred from packet data
    node1.connectToPeer(QHostAddress("127.0.0.1"), 6112, 2);
    node1.connectToPeer(QHostAddress("127.0.0.1"), 6113, 3);
    node2.joinGame(QHostAddress("127.0.0.1"), 6111, 1);
    node2.connectToPeer(QHostAddress("127.0.0.1"), 6113, 3);
    node3.joinGame(QHostAddress("127.0.0.1"), 6111, 1);
    node3.connectToPeer(QHostAddress("127.0.0.1"), 6112, 2);
    ta1.registerRemotePlayer(2, 0);
    ta1.registerRemotePlayer(3, 0);
    ta2.registerRemotePlayer(1, 47624);
    ta2.registerRemotePlayer(3, 0);
    ta3.registerRemotePlayer(1, 47624);
    ta3.registerRemotePlayer(2, 0);

    return app.exec();
}
