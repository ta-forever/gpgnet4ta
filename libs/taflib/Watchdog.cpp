#include "Watchdog.h"
#include <QtCore/qdebug.h>
#include <QTimerEvent>

using namespace taflib;

WatchdogThread Watchdog::m_watchdogThread;

WatchdogThread::WatchdogThread()
{
    this->moveToThread(this);
}

WatchdogThread::~WatchdogThread()
{
    this->quit();
    this->wait(300);
}

void WatchdogThread::startTimer(QString name, int millis)
{
    QMetaObject::invokeMethod(this, "sl_startTimer", Qt::QueuedConnection, QGenericArgument("QString", &name), QGenericArgument("int", &millis));
}

void WatchdogThread::killTimer(QString name)
{
    QMetaObject::invokeMethod(this, "sl_killTimer", Qt::QueuedConnection, QGenericArgument("QString", &name));
}

void WatchdogThread::timerEvent(QTimerEvent *event)
{
    QString name = m_timerNames[event->timerId()];
    qWarning() << name << "timed out!";
}

void WatchdogThread::sl_startTimer(QString name, int millis)
{
    int id = this->QObject::startTimer(millis);
    m_timerNames[id] = name;
    m_timerIds[name] = id;
}

void WatchdogThread::sl_killTimer(int id)
{
    this->QObject::killTimer(id);
    QString name = m_timerNames[id];
    m_timerNames.remove(id);
    m_timerIds.remove(name);
}

void WatchdogThread::sl_killTimer(QString name)
{
    int id = m_timerIds[name];
    this->QObject::killTimer(id);
    m_timerNames.remove(id);
    m_timerIds.remove(name);
}

Watchdog::Watchdog(QString name, int timeoutms):
m_name(name)
{
    if (!m_watchdogThread.isRunning())
    {
        m_watchdogThread.start();
    }
    m_watchdogThread.startTimer(name, timeoutms);
}

Watchdog::~Watchdog()
{
    m_watchdogThread.killTimer(m_name);
}
