#include "GameEventHandlerQt.h"
#include <QtCore/qmap.h>

void GameEventsSignalQt::onGameSettings(const std::string &mapName, std::uint16_t maxUnits)
{
    emit gameSettings(QString::fromStdString(mapName), maxUnits);
}

void GameEventsSignalQt::onPlayerStatus(const PlayerData &player, const std::set<std::string> & _mutualAllies)
{
    QStringList mutualAllies;
    for (const std::string & other: _mutualAllies)
    {
        mutualAllies.append(QString::fromStdString(other));
    }

    emit playerStatus(
        player.dplayid, QString::fromStdString(player.name), quint8(player.side),
        player.is_dead, player.armyNumber, player.teamNumber, mutualAllies);
}

void GameEventsSignalQt::onGameStarted(std::uint32_t tick, bool teamsFrozen)
{
    emit gameStarted(tick, teamsFrozen);
}

void GameEventsSignalQt::onGameEnded(const GameResult &result)
{
    QMap<qint32, qint32> resultByArmy;
    for (const auto & pair : result.resultByArmy)
    {
        resultByArmy[pair.first] = pair.second;
    }

    QMap<QString, qint32> armyNumbersByPlayerName;
    for (const auto & pair : result.armyNumbersByPlayerName)
    {
        armyNumbersByPlayerName[QString::fromStdString(pair.first)] = pair.second;
    }

    emit gameEnded(resultByArmy, armyNumbersByPlayerName);
}

void GameEventsSignalQt::onChat(const std::string& msg, bool isLocalPlayerSource)
{
    emit chat(QString::fromStdString(msg), isLocalPlayerSource);
}