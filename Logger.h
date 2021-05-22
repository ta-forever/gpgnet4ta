#pragma once

#include <fstream>
#include <memory>
#include <string>

#include <QtCore/qdebug.h>

class Logger
{
public:
    enum class Verbosity { SILENT = 0, FATAL = 1, WARNING = 2, CRITICAL = 3, INFO = 4, DEBUG = 5 };

private:
    std::ofstream m_logfile;
    Verbosity m_verbosity;
    static std::shared_ptr<Logger> m_instance;

public:
    Logger(const std::string& filename, Verbosity verbosity);
    static void Initialise(const std::string& filename, Verbosity level);
    static Logger* Get();
    static void Log(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    void LogToFile(QtMsgType type, const QMessageLogContext& context, const QString& msg);
};
