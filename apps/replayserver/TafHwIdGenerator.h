#pragma once

#include <QtCore/qstring.h>

class TafHwIdGenerator
{
public:
    TafHwIdGenerator(QString uidAppPath);

    QString get(int sessionId);

private:
    QString m_uidAppPath;
};
