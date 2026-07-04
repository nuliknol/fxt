/*
 * logger.cpp - Logging System Implementation
 */

#include "logger.h"
#include <QMutexLocker>

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
    : _level(LogLevel::Info),
      _fileOpen(false)
{
    /* Default log file */
    setLogFile("/tmp/mf_viewer.log");
}

Logger::~Logger()
{
    if (_fileOpen) {
        _stream.flush();
        _logFile.close();
    }
}

void Logger::setLogFile(const QString &path)
{
    QMutexLocker locker(&_mutex);

    if (_fileOpen) {
        _stream.flush();
        _logFile.close();
        _fileOpen = false;
    }

    _logFile.setFileName(path);
    if (_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        _stream.setDevice(&_logFile);
        _fileOpen = true;

        /* Write startup message */
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        _stream << "\n[" << timestamp << "] [INFO] === mf_viewer started ===\n";
        _stream.flush();
    }
}

void Logger::setLogLevel(LogLevel level)
{
    _level = level;
}

void Logger::log(LogLevel level, const QString &msg)
{
    if (level < _level) {
        return;
    }

    QMutexLocker locker(&_mutex);

    if (!_fileOpen) {
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);

    _stream << "[" << timestamp << "] [" << levelStr << "] " << msg << "\n";
    _stream.flush();
}

QString Logger::levelToString(LogLevel level) const
{
    switch (level) {
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    default:                return "UNKNOWN";
    }
}

void Logger::debug(const QString &msg)
{
    log(LogLevel::Debug, msg);
}

void Logger::info(const QString &msg)
{
    log(LogLevel::Info, msg);
}

void Logger::warning(const QString &msg)
{
    log(LogLevel::Warning, msg);
}

void Logger::error(const QString &msg)
{
    log(LogLevel::Error, msg);
}

void Logger::logTrainingStart(int startIdx, int endIdx, int inputWindow, int outputWindow)
{
    info(QString("Training started: range [%1-%2], input_window=%3, output_window=%4")
         .arg(startIdx).arg(endIdx).arg(inputWindow).arg(outputWindow));
}

void Logger::logTrainingProgress(int iteration, int maxIter, double ll)
{
    debug(QString("Training progress: iteration %1/%2, LL=%3")
          .arg(iteration).arg(maxIter).arg(ll, 0, 'f', 6));
}

void Logger::logTrainingComplete(int iterations, double finalLL, double trainTime)
{
    info(QString("Training complete: %1 iterations, final_LL=%2, time=%.2fs")
         .arg(iterations).arg(finalLL, 0, 'f', 6).arg(trainTime));
}

void Logger::logTrainingError(const QString &error)
{
    this->error(QString("Training error: %1").arg(error));
}

void Logger::logPredictionRequest(int startIdx, int endIdx)
{
    debug(QString("Prediction requested for indices %1-%2").arg(startIdx).arg(endIdx));
}

void Logger::logPredictionResult(int count, double inferenceTime)
{
    debug(QString("Prediction complete: %1 candles in %.3fms")
          .arg(count).arg(inferenceTime * 1000.0));
}

void Logger::logInputWindow(const std::vector<int32_t> &input)
{
    if (_level > LogLevel::Debug) return;

    QString values;
    int maxShow = std::min(static_cast<int>(input.size()), 20);
    for (int i = 0; i < maxShow; ++i) {
        if (i > 0) values += ", ";
        values += QString::number(input[i]);
    }
    if (input.size() > 20) {
        values += ", ...";
    }

    debug(QString("Input window: [%1]").arg(values));
}

void Logger::logPredictedOutput(const std::vector<int32_t> &output)
{
    if (_level > LogLevel::Debug) return;

    QString values;
    for (size_t i = 0; i < output.size(); ++i) {
        if (i > 0) values += ", ";
        values += QString::number(output[i]);
    }

    debug(QString("Predicted output: [%1]").arg(values));
}
