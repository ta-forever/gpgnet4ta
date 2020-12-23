/*
 * Copyright (C) 2008-2020 The Communi Project
 *
 * This example is free, and not covered by the BSD license. There is no
 * restriction applied to their modification, redistribution, using and so on.
 * You can study them, modify them, use them in your own program - either
 * completely or partially.
 */

#include "IrcForward.h"
#include <IrcMessage>
#include <IrcCommand>
#include <QCoreApplication>
#include <QTimer>

IrcForward::IrcForward(QObject* parent) : IrcConnection(parent)
{
    //! [messages]
    connect(this, SIGNAL(privateMessageReceived(IrcPrivateMessage*)), this, SLOT(processMessage(IrcPrivateMessage*)));
    //! [messages]

    bufferModel.setConnection(this);
    //! [channels]
    connect(&bufferModel, SIGNAL(channelsChanged(QStringList)), &parser, SLOT(setChannels(QStringList)));
    //! [channels]
}

IrcForward::~IrcForward()
{
    if (isActive()) {
        quit(realName());
        close();
    }
}

IrcCommandParser* IrcForward::getParser()
{
    return &parser;
}

void IrcForward::join(QString channel)
{
    sendCommand(IrcCommand::createJoin(channel));
}

//![receive]
void IrcForward::processMessage(IrcPrivateMessage* message)
{
    IrcCommand* cmd = parser.parse(message->content());
    if (cmd) {
        sendCommand(cmd);
    }
}
//![receive]
