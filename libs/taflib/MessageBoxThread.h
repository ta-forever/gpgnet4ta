#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qthread.h>

namespace taflib {

    class MessageBoxThread : public QObject
    {
        Q_OBJECT

            // onUnableToLaunch() is called (via signal/slot) from an onReadyRead handler (which is associated with a socket).
            // Somehow the MessageBox opened by onUnableToLaunch doesn't block the event loop, but at the same time doesn't return until OK is clicked (wtf right???)
            // The event loop continues to handle, for example, onSocketStateChanged events with the same threadID.
            // In onSocketStateChanged event, we go socket.deleteLater() to cleanup sockets on disconnect.
            // So if onUnableToLaunch is allowed to be invoked from same thread as onSocketStateChanged ...
            // it crashes when it returns to event loop and eventloop tries to use the (now deleted) socket.
            // I can't fathom how that works ... but the remedy?  Run onUnableToLaunch in a separate thread.
            // Maybe QMessageBox works better.  But QMessageBox requires QApplication.
            // However jdplay fails to work with a QApplication.  We have to be a QCoreApplication ...
            QThread m_thread;

    public:
        MessageBoxThread();
        ~MessageBoxThread();

    signals:
        void userAcknowledged(int userSelection);

    public slots:

        void onMessage(QString title, QString content, unsigned int flags);
    };

}