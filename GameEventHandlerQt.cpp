#include "GameEventHandlerQt.h"
#include <QtCore/qmap.h>
#include <QtCore/qvariant.h>

void GameEventsSignalQt::onGameSettings(const std::string &mapName, std::uint16_t maxUnits, const std::string &hostName, const std::string &localName)
{
    emit gameSettings(QString::fromStdString(mapName), maxUnits, QString::fromStdString(hostName), QString::fromStdString(localName));
}

void GameEventsSignalQt::onPlayerStatus(const PlayerData &player, const std::set<std::string> & _mutualAllies)
{
    QStringList mutualAllies;
    for (const std::string & other: _mutualAllies)
    {
        mutualAllies.append(QString::fromStdString(other));
    }

    emit playerStatus(
        player.dplayid, QString::fromStdString(player.name), player.slotNumber, quint8(player.side), player.is_AI,
        player.is_dead, player.armyNumber, player.teamNumber, mutualAllies);
}

void GameEventsSignalQt::onClearSlot(const PlayerData &player)
{
    emit clearSlot(player.dplayid, QString::fromStdString(player.name), player.slotNumber);
}

void GameEventsSignalQt::onGameStarted(std::uint32_t tick, bool teamsFrozen)
{
    emit gameStarted(tick, teamsFrozen);
}

void GameEventsSignalQt::onGameEnded(const GameResult &result)
{
    QList<QVariantMap> qresults;
    for (const auto &r: result.results)
    {
        QVariantMap qr;
        qr.insert("slot", QVariant(r.slot));
        qr.insert("army", QVariant(r.army));
        qr.insert("team", QVariant(r.team));
        qr.insert("name", QVariant(QString::fromStdString(r.name)));
        qr.insert("score", QVariant(r.score));
        qresults.push_back(qr);
    }

    emit gameEnded(qresults);
}

void GameEventsSignalQt::onChat(const std::string& msg, bool isLocalPlayerSource)
{
    emit chat(QString::fromStdString(msg), isLocalPlayerSource);
}