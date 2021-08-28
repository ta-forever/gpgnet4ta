#pragma once

#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>

namespace taflib {

    class ConsoleReader : public QObject
    {
        Q_OBJECT

        QTcpServer m_tcpServer;
        QList<QTcpSocket*> m_tcpSockets;
        bool m_loggedAConnection;

    public:
        explicit ConsoleReader(QHostAddress addr, quint16 port);
        ~ConsoleReader();

    signals:
        void textReceived(QString message);

    private:
        void onNewConnection();
        void onReadyReadTcp();
        void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    };

}