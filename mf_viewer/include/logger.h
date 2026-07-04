/*
 * logger.h - Logging System for mf_viewer
 *
 * Thread-safe logging to /tmp/mf_viewer.log
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QString>
#include <QDateTime>
#include <vector>
#include <cstdint>

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

class Logger : public QObject {
    Q_OBJECT

public:
    /* Singleton access */
    static Logger& instance();

    /* Configuration */
    void setLogFile(const QString &path);
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const { return _level; }

    /* General logging */
    void debug(const QString &msg);
    void info(const QString &msg);
    void warning(const QString &msg);
    void error(const QString &msg);

    /* HMM-specific logging */
    void logTrainingStart(int startIdx, int endIdx, int inputWindow, int outputWindow);
    void logTrainingProgress(int iteration, int maxIter, double ll);
    void logTrainingComplete(int iterations, double finalLL, double trainTime);
    void logTrainingError(const QString &error);

    void logPredictionRequest(int startIdx, int endIdx);
    void logPredictionResult(int count, double inferenceTime);

    void logInputWindow(const std::vector<int32_t> &input);
    void logPredictedOutput(const std::vector<int32_t> &output);

private:
    Logger();
    ~Logger();

    /* Prevent copying */
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel level, const QString &msg);
    QString levelToString(LogLevel level) const;

    QFile _logFile;
    QTextStream _stream;
    QMutex _mutex;
    LogLevel _level;
    bool _fileOpen;
};

/* Convenience macros */
#define LOG_DEBUG(msg)   Logger::instance().debug(msg)
#define LOG_INFO(msg)    Logger::instance().info(msg)
#define LOG_WARNING(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg)   Logger::instance().error(msg)

#endif /* LOGGER_H */
