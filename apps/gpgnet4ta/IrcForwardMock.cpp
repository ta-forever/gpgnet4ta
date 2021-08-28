#include "IrcForwardMock.h"

IrcForward::IrcForward(QObject* parent)
{
}

IrcForward::~IrcForward()
{
}

QString IrcForward::realName()
{
    return QString();
}

void IrcForward::quit(QString)
{
}

void IrcForward::join(QString)
{
}

void IrcForward::close()
{
}

void IrcForward::open()
{
}

bool IrcForward::isActive()
{
    return false;
}

void* IrcForward::getParser()
{
    return nullptr;
}

void IrcForward::sendCommand(void*)
{
}

void IrcForward::setHost(QString)
{
}

void IrcForward::setUserName(QString)
{
}

void IrcForward::setNickName(QString)
{
}

void IrcForward::setRealName(QString)
{
}

void IrcForward::setPort(quint16)
{
}

void* IrcCommand::createMessage(QString, const char*)
{
    return nullptr;
}

bool IrcPrivateMessage::isPrivate()
{
    return false;
}

QString IrcPrivateMessage::nick()
{
    return QString();
}

QString IrcPrivateMessage::content()
{
    return QString();
}
