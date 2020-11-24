#include <QtCore/qcoreapplication.h>
#include <QtCore/qcommandlineparser.h>
#include "tafnet/TafnetGameNode.h"

using namespace tafnet;

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

    TafnetNode node1(1, true, QHostAddress("127.0.0.1"), 6112);
    TafnetNode node2(2, false, QHostAddress("127.0.0.1"), 6113);

    TafnetGameNode ta1(
        &node1,
        []() { return new GameSender(QHostAddress("127.0.0.1"), 47624); },
        [](QSharedPointer<QUdpSocket> udpSocket) { return new GameReceiver(QHostAddress("127.0.0.1"), 0, 0, udpSocket); });
    TafnetGameNode ta2(
        &node2,
        []() { return new GameSender(QHostAddress("192.168.1.104"), 47624); },
        [](QSharedPointer<QUdpSocket> udpSocket) { return new GameReceiver(QHostAddress("192.168.1.109"), 0, 0, udpSocket); });

    node2.joinGame(QHostAddress("127.0.0.1"), 6112, 1);
    node1.connectToPeer(QHostAddress("127.0.0.1"), 6113, 2);
    ta1.registerRemotePlayer(2, 0);
    ta2.registerRemotePlayer(1, 47624);

    return app.exec();
}
