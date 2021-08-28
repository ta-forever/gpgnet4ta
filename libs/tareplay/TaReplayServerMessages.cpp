#include "TaReplayServerMessages.h"

#include "taflib/Logger.h"

using namespace tareplay;

TaReplayServerSubscribe::TaReplayServerSubscribe():
    gameId(0u)
{ }

const char * const TaReplayServerSubscribe::ID = "ReplayServerSubscribe";

TaReplayServerSubscribe::TaReplayServerSubscribe(QVariantList command)
{
    set(command);
}

void TaReplayServerSubscribe::set(QVariantList command)
{
    QString cmd = command[0].toString();
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    gameId = command[1].toUInt();
    position = command[2].toUInt();
}

TaReplayServerData::TaReplayServerData():
    status(TaReplayServerStatus::CONNECTING)
{ }

const char* const TaReplayServerData::ID = "ReplayServerData";

TaReplayServerData::TaReplayServerData(QVariantList command)
{
    set(command);
}

void TaReplayServerData::set(QVariantList command)
{
    QString cmd = command[0].toString();
    qInfo() << "[TaReplayServerData::set] cmd=" << cmd;
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    status = TaReplayServerStatus(command[1].toUInt());
    qInfo() << "[TaReplayServerData::set] status=" << int(status);
    data = command[2].toByteArray();
    qInfo() << "[TaReplayServerData::set] data=" << data.size();
}
