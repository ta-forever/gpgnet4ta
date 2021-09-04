#pragma once

#include <fstream>
#include <memory>
#include <string>

#include <QtCore/qdebug.h>

namespace taflib
{

    class Logger
    {
    public:
        enum class Verbosity { SILENT = 0, FATAL = 1, WARNING = 2, CRITICAL = 3, INFO = 4, DEBUG = 5 };

    private:
        std::ostream *m_ostream;    // points to either m_optionalLogFile or std::cout
        std::shared_ptr<std::ofstream> m_optionalLogFile;
        Verbosity m_verbosity;
        static std::shared_ptr<Logger> m_instance;

        std::ostream& ostream();

    public:
        Logger(const std::string& filename, Verbosity verbosity);
        static void Initialise(const std::string& filename, Verbosity level);
        static Logger* Get();
        static void Log(QtMsgType type, const QMessageLogContext& context, const QString& msg);
        void LogToFile(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    };

}