#include "GameEventHandlerQt.h"
#include <QtCore/qmap.h>
#include <QtCore/qvariant.h>
#include "taflib/Watchdog.h"

void GameEventsSignalQt::onGameSettings(const std::string &mapName, std::uint16_t maxUnits, const std::string &hostName, const std::string &localName)
{
    taflib::Watchdog wd("GameEventsSignalQt::onGameSettings", 100);
    emit gameSettings(QString::fromStdString(mapName), maxUnits, QString::fromStdString(hostName), QString::fromStdString(localName));
}

void GameEventsSignalQt::onPlayerStatus(const PlayerData &player, const std::set<std::string> & _mutualAllies)
{
    taflib::Watchdog wd("GameEventsSignalQt::onPlayerStatus", 100);
    QStringList mutualAllies;
    for (const std::string & other: _mutualAllies)
    {
        mutualAllies.append(QString::fromStdString(other));
    }

    emit playerStatus(
        player.dplayid, QString::fromStdString(player.name), player.slotNumber, quint8(player.side), 
        player.isWatcher, player.isAI, player.isDead, player.armyNumber, player.teamNumber, mutualAllies);
}

void GameEventsSignalQt::onClearSlot(const PlayerData &player)
{
    taflib::Watchdog wd("GameEventsSignalQt::onClearSlot", 100);
    emit clearSlot(player.dplayid, QString::fromStdString(player.name), player.slotNumber);
}

void GameEventsSignalQt::onGameStarted(std::uint32_t tick, bool teamsFrozen)
{
    taflib::Watchdog wd("GameEventsSignalQt::onGameStarted", 100);
    emit gameStarted(tick, teamsFrozen);
}

void GameEventsSignalQt::onGameEnded(const GameResult &result)
{
    taflib::Watchdog wd("GameEventsSignalQt::onGameEnded", 100);
    QList<QVariantMap> qresults;
    for (const auto &r: result.results)
    {
        QVariantMap qr;
        qr.insert("slot", QVariant(r.slot));
        qr.insert("army", QVariant(r.army));
        qr.insert("team", QVariant(r.team));
        qr.insert("alias", QVariant(QString::fromStdString(r.alias)));
        qr.insert("realName", QVariant(QString::fromStdString(r.realName)));
        qr.insert("score", QVariant(r.score));
        qresults.push_back(qr);
    }

    emit gameEnded(qresults);
}

void GameEventsSignalQt::onChat(const std::string& msg, bool isLocalPlayerSource)
{
    taflib::Watchdog wd("GameEventsSignalQt::onChat", 100);
    emit chat(QString::fromStdString(msg), isLocalPlayerSource);
}
