#include "EngineeringNotation.h"
#include <cmath>

static QString _engineeringNotation(double x, char k)
{
    if (x >= 1e15)
    {
        return QString("%1").arg(x, 0, 'g', 2);
    }
    if (x >= 1e12)
    {
        return _engineeringNotation(x / 1e12, 'T');
    }
    if (x >= 1e9)
    {
        return _engineeringNotation(x / 1e9, 'G');
    }
    else if (x >= 1e6)
    {
        return _engineeringNotation(x / 1e6, 'M');
    }
    else if (x >= 1e3)
    {
        return _engineeringNotation(x / 1e3, 'k');
    }
    else if (x >= 10.0 && k == 0)
    {
        return QString::number(int(x));
    }
    else if (x >= 10.0)
    {
        return _engineeringNotation(x, 0) + k;
    }
    else if (x >= 1.0 && k == 0)
    {
        QString s = QString("%1").arg(x).mid(0, 3);
        while (s.contains('.') && (*s.rbegin() == '0' || *s.rbegin() == '.'))
        {
            s = s.mid(0, s.size() - 1);
        }
        return s;
    }
    else if (x >= 1.0)
    {
        QString s = _engineeringNotation(x, 0);
        return s.contains('.')
            ? s.replace('.', k)
            : s + k;
    }
    else if (x > 0.0)
    {
        QString s = QString("%1").arg(x).mid(0, 4);
        while (s.contains('.') && (*s.rbegin() == '0' || *s.rbegin() == '.'))
        {
            s = s.mid(0, s.size() - 1);
        }
        return s;
    }
    else if (x == 0.0)
    {
        return "0";
    }
    else
    {
        return "-" + _engineeringNotation(-x, k);
    }
}

static double twoOrThreSigFigs(double x)
{
    if (x == 0.0)
    {
        return 0.0;
    }

    int order = std::floor(std::log(x) / std::log(10.0));
    if (order % 3 == 2)
    {
        order -= 2;
    }
    else // including -ve
    {
        order -= 1;
    }
    x += std::pow(10.0, order) / 2.0;
    return x;
}

QString taflib::engineeringNotation(double x)
{
    QString s = _engineeringNotation(twoOrThreSigFigs(x), 0).replace('.', ',');
    while (s[0] == '0' && s.size() > 1)
    {
        s = s.mid(1, s.length() - 1);
    }
    return s;
}
