#pragma once

#include "QtCore/qmap.h"
#include "QtCore/qobject.h"
#include "GameMonitor2.h"

class GameEventHandlerQt: public QObject
{
public:
    virtual void onGameSettings(QString mapName, quint16 maxUnits) = 0;
    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 side, bool isDead, quint8 armyNumber, quint8 teamNumber, QStringList mutualAllies) = 0;
    virtual void onGameStarted(quint32 tick, bool teamsFrozen) = 0;
    virtual void onGameEnded(QMap<qint32, qint32> resultByArmy, QMap<QString, qint32> armyNumbersByPlayerName) = 0;
    virtual void onChat(QString msg, bool isLocalPlayerSource) = 0;
};

class GameEventsSignalQt : public QObject, public GameEventHandler
{
    Q_OBJECT

public:
    virtual void onGameSettings(const std::string &mapName, std::uint16_t maxUnits);
    virtual void onPlayerStatus(const PlayerData &player, const std::set<std::string> & mutualAllies);
    virtual void onGameStarted(std::uint32_t tick, bool teamsFrozen);
    virtual void onGameEnded(const GameResult &);
    virtual void onChat(const std::string& msg, bool isLocalPlayerSource);

signals:
    void gameSettings(QString mapName, quint16 maxUnits);
    void playerStatus(quint32 dplayId, QString name, quint8 side, bool isDead, quint8 armyNumber, quint8 teamNumber, QStringList mutualAllies);
    void gameStarted(quint32 tick, bool teamsFrozen);
    void gameEnded(QMap<qint32, qint32> resultByArmy, QMap<QString, qint32> armyNumbersByPlayerName);
    void chat(QString msg, bool isLocalPlayerSource);
};