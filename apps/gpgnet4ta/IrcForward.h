/*
 * Copyright (C) 2008-2020 The Communi Project
 *
 * This example is free, and not covered by the BSD license. There is no
 * restriction applied to their modification, redistribution, using and so on.
 * You can study them, modify them, use them in your own program - either
 * completely or partially.
 */

#pragma once

#define IRC_STATIC
#include <ircconnection.h>
#include <ircbuffermodel.h>
#include <irccommandparser.h>

class IrcForward : public IrcConnection
{
    Q_OBJECT

public:
    IrcForward(QObject* parent = nullptr);
    ~IrcForward();

    IrcCommandParser* getParser();

public slots:
    void join(QString channel);

private slots:
    void processMessage(IrcPrivateMessage* message);

private:
    IrcCommandParser parser;
    IrcBufferModel bufferModel;
};
