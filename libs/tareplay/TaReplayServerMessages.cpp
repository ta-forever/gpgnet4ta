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
    // 4th argument (signed watch ticket) is optional for backward compatibility
    // with clients that subscribe with only gameId+position.
    if (command.size() > 3)
    {
        ticket = command[3].toByteArray();
    }
    else
    {
        ticket.clear();
    }
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
    if (cmd.compare(ID))
    {
        throw std::runtime_error("Unexpected command");
    }
    status = TaReplayServerStatus(command[1].toUInt());
    data = command[2].toByteArray();
}
