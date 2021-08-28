#pragma once

#include <QtCore/qmap.h>
#include <QtCore/qstring.h>
#include <QtCore/qthread.h>
#include <QtCore/qtimer.h>

namespace taflib
{

    class WatchdogThread : public QThread
    {
        Q_OBJECT
        QMap<int, QString> m_timerNames;
        QMap<QString, int> m_timerIds;

    public:

        WatchdogThread();
        ~WatchdogThread();

        virtual void startTimer(QString name, int millis);
        virtual void killTimer(QString name);

        virtual void timerEvent(QTimerEvent *event);

        private slots:
        void sl_startTimer(QString name, int millis);
        void sl_killTimer(QString name);
        void sl_killTimer(int id);
    };

    class Watchdog : public QObject
    {
        Q_OBJECT

    private:
        QString m_name;

    public:
        Watchdog(QString name, int timeoutms);
        ~Watchdog();

    private:
        static WatchdogThread m_watchdogThread;
    };

}
