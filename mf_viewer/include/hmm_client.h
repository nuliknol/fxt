/*
 * hmm_client.h - Qt Socket Client for HMM Training Daemon
 *
 * Provides communication with the pmod_train_daemon for
 * starting/stopping training and receiving progress updates.
 */

#ifndef HMM_CLIENT_H
#define HMM_CLIENT_H

#include <QObject>
#include <QLocalSocket>
#include <QString>
#include <QTimer>
#include <QVector>
#include <cstdint>

/* Message types (must match pmod_train_socket.h) */
#define PMOD_TRAIN_MSG_START        0x10
#define PMOD_TRAIN_MSG_STOP         0x11
#define PMOD_TRAIN_MSG_STATUS       0x12
#define PMOD_TRAIN_MSG_PROGRESS     0x13
#define PMOD_TRAIN_MSG_COMPLETE     0x14
#define PMOD_TRAIN_MSG_INFER        0x15
#define PMOD_TRAIN_MSG_PREDICTIONS  0x16
#define PMOD_TRAIN_MSG_ERROR        0xFF

/* Training status codes */
#define PMOD_TRAIN_STATUS_IDLE      0x00
#define PMOD_TRAIN_STATUS_RUNNING   0x01
#define PMOD_TRAIN_STATUS_STOPPED   0x02
#define PMOD_TRAIN_STATUS_COMPLETE  0x03
#define PMOD_TRAIN_STATUS_ERROR     0x04

/* Maximum path length */
#define PMOD_TRAIN_MAX_PATH 256

/* Training request structure (packed, binary protocol) */
#pragma pack(push, 1)
struct TrainRequest {
    uint8_t  msg_type;
    uint8_t  model_type;            /* 1=HMM1, 2=HMM2, 3=HMM3, 10=HSMM */
    uint16_t num_variables;
    uint32_t start_index;
    uint32_t end_index;
    uint32_t input_window;
    uint32_t output_window;
    uint32_t num_hidden_states;
    uint32_t max_iterations;
    uint32_t max_duration;          /* For HSMM: max state duration */
    char     data_file[PMOD_TRAIN_MAX_PATH];
    char     model_output[PMOD_TRAIN_MAX_PATH];
};

struct TrainProgress {
    uint8_t  msg_type;
    uint8_t  status;
    uint16_t iteration;
    uint16_t max_iterations;
    uint16_t reserved;
    double   log_likelihood;
    double   improvement;
    double   elapsed_sec;
};

struct TrainComplete {
    uint8_t  msg_type;
    uint8_t  status;
    uint16_t total_iterations;
    uint32_t reserved;
    double   final_log_likelihood;
    double   train_time_sec;
    char     model_path[PMOD_TRAIN_MAX_PATH];
};

struct TrainError {
    uint8_t  msg_type;
    uint8_t  error_code;
    uint16_t reserved;
    uint32_t msg_len;
};

struct InferRequest {
    uint8_t  msg_type;
    uint8_t  reserved;
    uint16_t num_predictions;
    uint32_t start_index;
    uint32_t input_window;
    char     model_path[PMOD_TRAIN_MAX_PATH];
    char     data_file[PMOD_TRAIN_MAX_PATH];
};

struct PredictionsHeader {
    uint8_t  msg_type;
    uint8_t  status;
    uint16_t num_predictions;
    uint32_t start_index;
};

struct HmmPredictedCandle {
    int32_t  high_open_diff;
    int32_t  close_open_diff;
    int32_t  low_close_diff;
    int32_t  high_low_diff;
    float    confidence;
};
#pragma pack(pop)

class HmmClient : public QObject {
    Q_OBJECT

public:
    explicit HmmClient(QObject *parent = nullptr);
    ~HmmClient();

    /* Connection management */
    bool connectToDaemon(const QString &socketPath = "/tmp/pmod_train.sock");
    void disconnect();
    bool isConnected() const;

    /* Training control */
    void startTraining(const QString &dataFile,
                       uint32_t startIndex, uint32_t endIndex,
                       uint32_t inputWindow, uint32_t outputWindow,
                       uint32_t numHiddenStates = 64,
                       uint32_t maxIterations = 100,
                       const QString &modelOutput = "/tmp/model.pmod",
                       uint8_t modelType = 1,
                       uint32_t maxDuration = 20);

    void stopTraining();
    void requestStatus();

    /* Inference - request predictions after training */
    void requestInference(const QString &modelPath,
                          const QString &dataFile,
                          uint32_t startIndex,
                          uint32_t inputWindow,
                          uint16_t numPredictions);

signals:
    /* Connection signals */
    void connected();
    void disconnected();
    void connectionError(const QString &error);

    /* Training signals */
    void trainingStarted();
    void trainingProgress(int iteration, int maxIterations,
                         double logLikelihood, double improvement,
                         double elapsedSec);
    void trainingComplete(bool success, int totalIterations,
                         double finalLogLikelihood, double trainTime,
                         const QString &modelPath);
    void trainingError(const QString &error);

    /* Inference signals */
    void predictionsReceived(int startIndex, const QVector<HmmPredictedCandle> &predictions);
    void inferenceError(const QString &error);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QLocalSocket::LocalSocketError error);

private:
    QLocalSocket *_socket;
    QString _socketPath;
    bool _training;

    void processIncomingData();
    void handleProgressMessage(const TrainProgress &progress);
    void handleCompleteMessage(const TrainComplete &complete);
    void handleErrorMessage();
    void handlePredictionsMessage();
};

#endif /* HMM_CLIENT_H */
