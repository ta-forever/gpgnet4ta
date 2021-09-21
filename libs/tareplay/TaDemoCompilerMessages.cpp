#include "TaDemoCompilerMessages.h"

using namespace tareplay;

const char* const HelloMessage::ID = "Hello";

HelloMessage::HelloMessage():
    gameId(0u),
    playerDpId(0u),
    playerPublicAddr("127.0.0.1")
{ }

HelloMessage::HelloMessage(QVariantList command)
{
    set(command);
}

void HelloMessage::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    gameId = command[1].toUInt();
    playerDpId = command[2].toUInt();
    playerPublicAddr = command[3].toString();
}

const char* const GameInfoMessage::ID = "GameInfo";

GameInfoMessage::GameInfoMessage():
    maxUnits(1500u),
    mapName("SHERWOOD")
{ }

GameInfoMessage::GameInfoMessage(QVariantList command)
{
    set(command);
}

void GameInfoMessage::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    maxUnits = command[1].toUInt();
    mapName = command[2].toString();
}

const char* const GamePlayerMessage::ID = "GamePlayer";

GamePlayerMessage::GamePlayerMessage():
    side(1),
    name("BILLY_IDOL")
{ }

GamePlayerMessage::GamePlayerMessage(QVariantList command)
{
    set(command);
}

void GamePlayerMessage::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    side = command[1].toInt();
    name = command[2].toString();
    statusMessage = command[3].toByteArray();
}

const char* const GamePlayerNumber::ID = "GamePlayerNumber";

GamePlayerNumber::GamePlayerNumber():
    dplayId(0u),
    number(0)
{ }

GamePlayerNumber::GamePlayerNumber(QVariantList command)
{
    set(command);
}

void GamePlayerNumber::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    dplayId = command[1].toUInt();
    number = command[2].toUInt();
}

const char* const GamePlayerLoading::ID = "GamePlayerLoading";

GamePlayerLoading::GamePlayerLoading()
{ }

GamePlayerLoading::GamePlayerLoading(QVariantList command)
{
    set(command);
}

void GamePlayerLoading::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    for (int n = 1; n < command.size(); ++n)
    {
        lockedInPlayers.push_back(command[n].toUInt());
    }
}

const char* const GameUnitDataMessage::ID = "UnitData";

GameUnitDataMessage::GameUnitDataMessage()
{ }

GameUnitDataMessage::GameUnitDataMessage(QVariantList command)
{
    set(command);
}

void GameUnitDataMessage::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    unitData = command[1].toByteArray();
}

const char* const GameMoveMessage::ID = "Move";

GameMoveMessage::GameMoveMessage()
{ }

GameMoveMessage::GameMoveMessage(QVariantList command)
{
    set(command);
}

void GameMoveMessage::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    moves = command[1].toByteArray();
}

const char* const StopRecording::ID = "StopRecording";

StopRecording::StopRecording()
{ }

StopRecording::StopRecording(QVariantList command)
{
    set(command);
}

void StopRecording::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
}
