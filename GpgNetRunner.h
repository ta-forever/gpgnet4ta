#pragma once

#include <QtCore/qthread.h>

class GpgNetRunner : public QThread
{
    Q_OBJECT

    QString iniTemplate;
    QString iniTarget;
    QString gpgneturl;
    double mean;
    double deviation;
    QString country;
    int numGames;
    QString guid;
    int playerLimit;
    bool lockOptions;

public:
    GpgNetRunner(QString iniTemplate, QString iniTarget, QString gpgneturl,
        double mean, double deviation, QString country, int numGames, QString guid, int playerLimit, bool lockOptions);

    virtual void run();

signals:
    void createLobby(int protocol, int localPort, QString playerName, int playerId, int natTraversal);
    void joinGame(QString host, QString playerName, int playerId);
    void connectToPeer(QString host, QString playerName, int playerId);

};
