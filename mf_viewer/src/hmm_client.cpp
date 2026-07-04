/*
 * hmm_client.cpp - Qt Socket Client for HMM Training Daemon
 */

#include "hmm_client.h"
#include <QDataStream>
#include <QFileInfo>
#include <cstring>

HmmClient::HmmClient(QObject *parent)
    : QObject(parent),
      _socket(new QLocalSocket(this)),
      _training(false)
{
    connect(_socket, &QLocalSocket::connected,
            this, &HmmClient::onConnected);
    connect(_socket, &QLocalSocket::disconnected,
            this, &HmmClient::onDisconnected);
    connect(_socket, &QLocalSocket::readyRead,
            this, &HmmClient::onReadyRead);
    connect(_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
            this, &HmmClient::onError);
}

HmmClient::~HmmClient()
{
    disconnect();
}

bool HmmClient::connectToDaemon(const QString &socketPath)
{
    _socketPath = socketPath;

    if (_socket->state() == QLocalSocket::ConnectedState) {
        return true;
    }

    /* Check if socket file exists */
    QFileInfo socketInfo(socketPath);
    if (!socketInfo.exists()) {
        emit connectionError(QString("Socket not found: %1\n\n"
                                     "Make sure pmod_train_daemon is running:\n"
                                     "  cd /home/mf/mf/bitforce/pmod\n"
                                     "  ./pmod_train_daemon")
                            .arg(socketPath));
        return false;
    }

    _socket->connectToServer(socketPath);

    /* Wait for connection with timeout */
    if (!_socket->waitForConnected(3000)) {
        emit connectionError(QString("Failed to connect to %1: %2")
                            .arg(socketPath)
                            .arg(_socket->errorString()));
        return false;
    }

    return true;
}

void HmmClient::disconnect()
{
    if (_socket->state() != QLocalSocket::UnconnectedState) {
        _socket->disconnectFromServer();
        _socket->waitForDisconnected(1000);
    }
}

bool HmmClient::isConnected() const
{
    return _socket->state() == QLocalSocket::ConnectedState;
}

void HmmClient::startTraining(const QString &dataFile,
                              uint32_t startIndex, uint32_t endIndex,
                              uint32_t inputWindow, uint32_t outputWindow,
                              uint32_t numHiddenStates,
                              uint32_t maxIterations,
                              const QString &modelOutput,
                              uint8_t modelType,
                              uint32_t maxDuration)
{
    if (!isConnected()) {
        if (!connectToDaemon()) {
            return;
        }
    }

    TrainRequest req;
    memset(&req, 0, sizeof(req));

    req.msg_type = PMOD_TRAIN_MSG_START;
    req.model_type = modelType;
    req.num_variables = 11;  /* TFX has 11 fields per record */
    req.start_index = startIndex;
    req.end_index = endIndex;
    req.input_window = inputWindow;
    req.output_window = outputWindow;
    req.num_hidden_states = numHiddenStates;
    req.max_iterations = maxIterations;
    req.max_duration = maxDuration;

    /* Copy file paths */
    QByteArray dataFileBytes = dataFile.toUtf8();
    QByteArray modelOutputBytes = modelOutput.toUtf8();

    strncpy(req.data_file, dataFileBytes.constData(),
            PMOD_TRAIN_MAX_PATH - 1);
    strncpy(req.model_output, modelOutputBytes.constData(),
            PMOD_TRAIN_MAX_PATH - 1);

    /* Send request */
    qint64 written = _socket->write(reinterpret_cast<const char*>(&req),
                                    sizeof(req));
    if (written != sizeof(req)) {
        emit trainingError("Failed to send training request");
        return;
    }

    _socket->flush();
    _training = true;
    emit trainingStarted();
}

void HmmClient::stopTraining()
{
    if (!isConnected()) {
        return;
    }

    /* Send stop message (just msg_type byte is enough) */
    uint8_t msg_type = PMOD_TRAIN_MSG_STOP;
    uint8_t padding[3] = {0, 0, 0};

    _socket->write(reinterpret_cast<const char*>(&msg_type), 1);
    _socket->write(reinterpret_cast<const char*>(padding), 3);
    _socket->flush();
}

void HmmClient::requestStatus()
{
    if (!isConnected()) {
        return;
    }

    uint8_t msg_type = PMOD_TRAIN_MSG_STATUS;
    uint8_t padding[3] = {0, 0, 0};

    _socket->write(reinterpret_cast<const char*>(&msg_type), 1);
    _socket->write(reinterpret_cast<const char*>(padding), 3);
    _socket->flush();
}

void HmmClient::onConnected()
{
    emit connected();
}

void HmmClient::onDisconnected()
{
    _training = false;
    emit disconnected();
}

void HmmClient::onReadyRead()
{
    processIncomingData();
}

void HmmClient::onError(QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(error);
    emit connectionError(_socket->errorString());
}

void HmmClient::processIncomingData()
{
    while (_socket->bytesAvailable() >= 1) {
        /* Peek at message type */
        char msgType;
        _socket->peek(&msgType, 1);

        switch (static_cast<uint8_t>(msgType)) {
        case PMOD_TRAIN_MSG_PROGRESS: {
            if (_socket->bytesAvailable() < static_cast<qint64>(sizeof(TrainProgress))) {
                return;  /* Wait for more data */
            }
            TrainProgress progress;
            _socket->read(reinterpret_cast<char*>(&progress), sizeof(progress));
            handleProgressMessage(progress);
            break;
        }

        case PMOD_TRAIN_MSG_COMPLETE: {
            if (_socket->bytesAvailable() < static_cast<qint64>(sizeof(TrainComplete))) {
                return;
            }
            TrainComplete complete;
            _socket->read(reinterpret_cast<char*>(&complete), sizeof(complete));
            handleCompleteMessage(complete);
            break;
        }

        case PMOD_TRAIN_MSG_ERROR: {
            if (_socket->bytesAvailable() < static_cast<qint64>(sizeof(TrainError))) {
                return;
            }
            handleErrorMessage();
            break;
        }

        case PMOD_TRAIN_MSG_PREDICTIONS: {
            if (_socket->bytesAvailable() < static_cast<qint64>(sizeof(PredictionsHeader))) {
                return;
            }
            handlePredictionsMessage();
            break;
        }

        default:
            /* Unknown message, skip it */
            _socket->read(1);
            break;
        }
    }
}

void HmmClient::handleProgressMessage(const TrainProgress &progress)
{
    emit trainingProgress(
        progress.iteration,
        progress.max_iterations,
        progress.log_likelihood,
        progress.improvement,
        progress.elapsed_sec
    );
}

void HmmClient::handleCompleteMessage(const TrainComplete &complete)
{
    _training = false;

    bool success = (complete.status == PMOD_TRAIN_STATUS_COMPLETE);
    QString modelPath = QString::fromUtf8(complete.model_path);

    emit trainingComplete(
        success,
        complete.total_iterations,
        complete.final_log_likelihood,
        complete.train_time_sec,
        modelPath
    );
}

void HmmClient::handleErrorMessage()
{
    TrainError err;
    _socket->read(reinterpret_cast<char*>(&err), sizeof(err));

    QString errorMsg;
    if (err.msg_len > 0 && err.msg_len < 10000) {
        QByteArray msgData = _socket->read(err.msg_len);
        errorMsg = QString::fromUtf8(msgData);
    } else {
        errorMsg = QString("Error code: %1").arg(err.error_code);
    }

    _training = false;
    emit trainingError(errorMsg);
}

void HmmClient::requestInference(const QString &modelPath,
                                  const QString &dataFile,
                                  uint32_t startIndex,
                                  uint32_t inputWindow,
                                  uint16_t numPredictions)
{
    if (!isConnected()) {
        if (!connectToDaemon()) {
            emit inferenceError("Not connected to daemon");
            return;
        }
    }

    InferRequest req;
    memset(&req, 0, sizeof(req));

    req.msg_type = PMOD_TRAIN_MSG_INFER;
    req.reserved = 0;
    req.num_predictions = numPredictions;
    req.start_index = startIndex;
    req.input_window = inputWindow;

    QByteArray modelPathBytes = modelPath.toUtf8();
    QByteArray dataFileBytes = dataFile.toUtf8();

    strncpy(req.model_path, modelPathBytes.constData(),
            PMOD_TRAIN_MAX_PATH - 1);
    strncpy(req.data_file, dataFileBytes.constData(),
            PMOD_TRAIN_MAX_PATH - 1);

    qint64 written = _socket->write(reinterpret_cast<const char*>(&req),
                                    sizeof(req));
    if (written != sizeof(req)) {
        emit inferenceError("Failed to send inference request");
        return;
    }

    _socket->flush();
}

void HmmClient::handlePredictionsMessage()
{
    PredictionsHeader hdr;
    _socket->read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    if (hdr.status != PMOD_TRAIN_STATUS_COMPLETE) {
        emit inferenceError("Inference failed");
        return;
    }

    /* Read prediction data */
    qint64 dataSize = hdr.num_predictions * sizeof(HmmPredictedCandle);

    /* Wait for all prediction data */
    while (_socket->bytesAvailable() < dataSize) {
        if (!_socket->waitForReadyRead(5000)) {
            emit inferenceError("Timeout waiting for prediction data");
            return;
        }
    }

    QVector<HmmPredictedCandle> predictions;
    predictions.reserve(hdr.num_predictions);

    for (int i = 0; i < hdr.num_predictions; i++) {
        HmmPredictedCandle candle;
        _socket->read(reinterpret_cast<char*>(&candle), sizeof(candle));
        predictions.append(candle);
    }

    emit predictionsReceived(hdr.start_index, predictions);
}
