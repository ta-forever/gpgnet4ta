#pragma once

#include <QtCore/qthread.h>
#include <istream>

class ConsoleReader : public QThread
{
    Q_OBJECT

    std::istream &m_is;

public:
    explicit ConsoleReader(std::istream &is, QObject *parent = 0);

signals:
    void textReceived(QString message);

private:
    void run();
};
