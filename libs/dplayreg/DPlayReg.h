#pragma once

#include <QtCore/qstring.h>

namespace dplayreg {

    bool CheckDplayLobbyableApplication(QString guid, QString path, QString file, QString commandLine, QString currentDirectory);
    void RegisterDplayLobbyableApplication(QString name, QString guid, QString path, QString file, QString commandLine, QString currentDirectory);
    QString GetDplayLobbableAppPath(QString appGuid, QString defaultPath);
    QMap<QString, QString> GetDplayLobbableApp(QString appGuid);

}
