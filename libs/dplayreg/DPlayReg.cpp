#include "DPlayReg.h"

#include <QtCore/qdebug.h>
#include <QtCore/qstring.h>
#include <QtCore/qsettings.h>

bool dplayreg::CheckDplayLobbyableApplication(QString guid, QString path, QString file, QString commandLine, QString currentDirectory)
{
    QString registryPath = QString(R"(%1\%2)")
        .arg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)")
        .arg("Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();

    qInfo() << "\nCHECK:" << guid << path << file << commandLine << currentDirectory;
    Q_FOREACH(QString appName, applications)
    {
        QString nthGuid = registry.value(appName + "/Guid").toString();
        QString nthPath = registry.value(appName + "/Path").toString();
        QString nthFile = registry.value(appName + "/File").toString();
        QString nthCommandLine = registry.value(appName + "/CommandLine").toString();
        QString nthCurrentDirectory = registry.value(appName + "/CurrentDirectory").toString();
        if (QString::compare(guid, nthGuid, Qt::CaseInsensitive) == 0 &&
            QString::compare(path, nthPath, Qt::CaseInsensitive) == 0 &&
            QString::compare(file, nthFile, Qt::CaseInsensitive) == 0 &&
            QString::compare(commandLine, nthCommandLine) == 0 &&
            QString::compare(currentDirectory, nthCurrentDirectory, Qt::CaseInsensitive) == 0)
        {
            qInfo() << "MATCH:" << nthGuid << nthPath << nthFile << nthCommandLine << nthCurrentDirectory;
            return true;
        }
        qInfo() << "NO MATCH:" << nthGuid << nthPath << nthFile << nthCommandLine << nthCurrentDirectory;

    }
    return false;
}

void dplayreg::RegisterDplayLobbyableApplication(QString name, QString guid, QString path, QString file, QString commandLine, QString currentDirectory)
{
    QString registryPath = QString(R"(%1\%2)")
        .arg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay\Applications)")
        .arg(name);

    QSettings registry(registryPath, QSettings::NativeFormat);
    registry.setValue("Guid", guid);
    registry.setValue("Path", path);
    registry.setValue("File", file);
    registry.setValue("CommandLine", commandLine);
    registry.setValue("CurrentDirectory", currentDirectory);
}

QString dplayreg::GetDplayLobbableAppPath(QString appGuid, QString defaultPath)
{
    QString registryPath = QString(R"(%1\%2)")
        .arg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)")
        .arg("Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();
    Q_FOREACH(QString appName, applications)
    {
        QString nthGuid = registry.value(appName + "/Guid").toString();
        if (QString::compare(appGuid, nthGuid) == 0)
        {
            return registry.value(appName + "/Path").toString();
        }
    }
    return defaultPath;
}


QMap<QString, QString> dplayreg::GetDplayLobbableApp(QString appGuid)
{
    QMap<QString, QString> result;

    QString registryPath = QString(R"(%1\%2)")
        .arg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectPlay)")
        .arg("Applications");
    QSettings registry(registryPath, QSettings::NativeFormat);
    QStringList applications = registry.childGroups();
    Q_FOREACH(QString appName, applications)
    {
        QString nthGuid = registry.value(appName + "/Guid").toString();
        if (QString::compare(appGuid, nthGuid) == 0)
        {
            result["Guid"] = nthGuid;
            result["Path"] = registry.value(appName + "/Path", QVariant("<no Path>")).toString();
            result["File"] = registry.value(appName + "/File", QVariant("<no File>")).toString();
            result["CommandLine"] = registry.value(appName + "/CommandLine", QVariant("<no CommandLine>")).toString();
            result["CurrentDirectory"] = registry.value(appName + "/CurrentDirectory", QVariant("<no CurrentDirectory>")).toString();
            return result;
        }
    }
    return result;
}