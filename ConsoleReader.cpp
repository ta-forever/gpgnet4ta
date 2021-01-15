#include "ConsoleReader.h"
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <sstream>

ConsoleReader::ConsoleReader(std::istream &is, QObject *parent) :
QThread(parent),
m_is(is)
{ }

void ConsoleReader::run()
{
    char buffer[16384];
    qInfo() << "[ConsoleReader::run] thread started";
    for (;;)
    {
        m_is.clear();
        m_is.read(buffer, sizeof(buffer));
        std::size_t bytesRead = m_is.gcount();
        if (bytesRead == 0)
        {
            msleep(300);
            continue;
        }

        buffer[bytesRead] = 0;

        std::istringstream ss(buffer);
        while (!ss.eof())
        {
            std::string line;
            std::getline(ss, line);
            if (line.empty())
            {
                continue;
            }

            QString qline = QString::fromStdString(line);
            qInfo() << "[ConsoleReader::run] received" << qline;
            emit textReceived(qline);
        }
    }
}
