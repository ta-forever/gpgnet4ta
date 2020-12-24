#pragma once

#include "QtCore/qmap.h"
#include "QtCore/qobject.h"
#include "GameMonitor2.h"

class GameEventHandlerQt: public QObject
{
public:
    virtual void onGameSettings(QString mapName, quint16 maxUnits, QString hostName, QString localName) = 0;
    virtual void onPlayerStatus(quint32 dplayId, QString name, quint8 slot, quint8 side, bool isAI, bool isDead, quint8 armyNumber, quint8 teamNumber, QStringList mutualAllies) = 0;
    virtual void onClearSlot(quint32 dplayId, QString name, quint8 slot) = 0;
    virtual void onGameStarted(quint32 tick, bool teamsFrozen) = 0;
    virtual void onGameEnded(QList<QVariantMap> results) = 0;
    virtual void onChat(QString msg, bool isLocalPlayerSource) = 0;
};

class GameEventsSignalQt : public QObject, public GameEventHandler
{
    Q_OBJECT

public:
    virtual void onGameSettings(const std::string &mapName, std::uint16_t maxUnits, const std::string &hostName, const std::string &localName);
    virtual void onPlayerStatus(const PlayerData &player, const std::set<std::string> & mutualAllies);
    virtual void onClearSlot(const PlayerData&);
    virtual void onGameStarted(std::uint32_t tick, bool teamsFrozen);
    virtual void onGameEnded(const GameResult &);
    virtual void onChat(const std::string& msg, bool isLocalPlayerSource);

signals:
    void gameSettings(QString mapName, quint16 maxUnits, QString hostName, QString localName);
    void playerStatus(quint32 dplayId, QString name, quint8 slot, quint8 side, bool isAI, bool isDead, quint8 armyNumber, quint8 teamNumber, QStringList mutualAllies);
    void clearSlot(quint32 dplayId, QString name, quint8 slot);
    void gameStarted(quint32 tick, bool teamsFrozen);
    void gameEnded(QList<QVariantMap> results);
    void chat(QString msg, bool isLocalPlayerSource);
};