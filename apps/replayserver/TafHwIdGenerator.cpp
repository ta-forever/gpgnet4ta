#include "TafHwIdGenerator.h"

#include "taflib/Logger.h"

#include <QtCore/qfileinfo.h>
#include <QtCore/qprocess.h>

TafHwIdGenerator::TafHwIdGenerator(QString uidAppPath) :
    m_uidAppPath(uidAppPath)
{
    QFileInfo fileInfo(uidAppPath);
    if (!fileInfo.exists())
    {
        throw std::runtime_error(std::string("App not found: '") + uidAppPath.toStdString() + "'");
    }
    if (!fileInfo.isFile())
    {
        throw std::runtime_error(std::string("App not a regular file: '") + uidAppPath.toStdString() + "'");
    }
    if (!fileInfo.isExecutable())
    {
        throw std::runtime_error(std::string("App not executable: '") + uidAppPath.toStdString() + "'");
    }
}


QString TafHwIdGenerator::get(int sessionId)
{
    QProcess uidProcess;
    QStringList arguments;
    uidProcess.start(m_uidAppPath, QStringList() << QString::number(sessionId));
    if (!uidProcess.waitForStarted(3000))
    {
        qWarning() << "Unable to start UID generator!";
        return "0";
    }
    if (!uidProcess.waitForFinished(3000))
    {
        qWarning() << "Timeout waiting for HW UID generator to complete!";
        return "0";
    }
    return uidProcess.readAll();
}