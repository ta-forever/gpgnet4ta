#pragma once

#include <QtCore/qobject.h>

class IrcCommand
{
public:
    static void* createMessage(QString, const char*);
};

class IrcPrivateMessage
{
public:
    bool isPrivate();
    QString nick();
    QString content();
};

class IrcConnection: public QObject
{
    Q_OBJECT

signals:
    void privateMessageReceived(IrcPrivateMessage *);
};

class IrcForward: public IrcConnection
{
    Q_OBJECT

public:
    IrcForward(QObject* parent = nullptr);
    ~IrcForward();

    QString realName();
    void quit(QString);
    void close();
    void open();
    bool isActive();
    void* getParser();
    void sendCommand(void*);

    void setHost(QString);
    void setUserName(QString);
    void setNickName(QString);
    void setRealName(QString);
    void setPort(quint16);

public slots:
    void join(QString channel);

};
