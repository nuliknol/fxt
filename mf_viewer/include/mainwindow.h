#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QScrollBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QListWidget>
#include <QDockWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include <vector>
#include <string>
#include <memory>
#include "candle.h"
#include "mf_view.h"
#include "layer.h"
#include "hmm_client.h"

// Record structure for the input binary file
typedef struct {
    int32_t year_offset;
    int32_t month;
    int32_t day;
    int32_t hour;
    int32_t minute_interval;
    int32_t time_slot;
    int32_t high_open_diff;
    int32_t close_open_diff;
    int32_t low_close_diff;
    int32_t high_low_diff;
    int32_t original_close;
} DataRecord;

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	void	addLayer(const QString &filePath);

private slots:
	void	onCandleClicked(const Candle &c, int layerIndex, int candleIndex);
	void	onViewScrolled(int firstVisibleIndex);
	void	onCandleHovered(const Candle &c, int layerIndex, int candleIndex);
	void	onOpenFile();
	void	onLayerItemClicked(QListWidgetItem *item);
	void	onRemoveLayer();
	void	onConfigureLayer();
	void	onMoveLayerUp();
	void	onMoveLayerDown();

	// HMM Training slots
	void	onSetStartPoint();
	void	onSetEndPoint();
	void	onStartTraining();
	void	onStopTraining();
	void	onHmmPointSelected(int candleIndex, int64_t timestamp);

	// HMM Client slots
	void	onHmmConnected();
	void	onHmmDisconnected();
	void	onHmmConnectionError(const QString &error);
	void	onModelTypeChanged(int index);
	void	onHmmTrainingStarted();
	void	onHmmTrainingProgress(int iteration, int maxIterations,
	                             double logLikelihood, double improvement,
	                             double elapsedSec);
	void	onHmmTrainingComplete(bool success, int totalIterations,
	                             double finalLogLikelihood, double trainTime,
	                             const QString &modelPath);
	void	onHmmTrainingError(const QString &error);

	// Model save/load slots
	void	onLoadHmmModel();
	void	onSaveHmmModel();

	// Inference/Prediction slots
	void	onPredictionsReceived(int startIndex, const QVector<HmmPredictedCandle> &predictions);
	void	onInferenceError(const QString &error);

private:
	MfView		*_view;
	QLabel		*_infoLabel;
	QScrollBar	*_scrollBar;
	QMenu		*_fileMenu;
	QAction		*_openAction;
	QListWidget	*_layersListWidget;
	QDockWidget	*_layersDock;
	QPushButton	*_removeLayerBtn;
	QPushButton	*_configureLayerBtn;
	QPushButton	*_moveUpBtn;
	QPushButton	*_moveDownBtn;

	std::vector<std::shared_ptr<Layer>> _layers;
	std::string		_instrumentName;

	// HMM Training UI
	QDockWidget		*_hmmDock;
	QPushButton		*_setStartBtn;
	QPushButton		*_setEndBtn;
	QPushButton		*_trainBtn;
	QPushButton		*_stopTrainBtn;
	QSpinBox		*_inputWindowSpin;
	QSpinBox		*_outputWindowSpin;
	QSpinBox		*_hiddenStatesSpin;
	QComboBox		*_modelTypeCombo;
	QSpinBox		*_maxDurationSpin;
	QLabel			*_maxDurationLabel;
	QProgressBar	*_trainingProgress;
	QLabel			*_startPointLabel;
	QLabel			*_endPointLabel;
	QLabel			*_hmmStatusLabel;

	// HMM Training state
	int				_hmmStartIndex;
	int				_hmmEndIndex;
	bool			_selectingStartPoint;
	bool			_selectingEndPoint;
	bool			_isTraining;
	QString			_currentDataFile;

	// HMM Client
	HmmClient		*_hmmClient;
	QString			_currentModelPath;

	// Menu actions for model
	QAction			*_loadModelAction;
	QAction			*_saveModelAction;

	void	setupUi();
	void	createMenus();
	void	setupHmmDock();
	void	updateLayersList();
	void	updateView();
	void	updateViewKeepExtent();
	void	updateHmmButtonStates();
	bool	loadBinaryFile(const QString &filePath, std::vector<Candle> &candles);
	bool	loadTextFile(const QString &filePath, std::vector<Candle> &candles);
	bool	loadMt4CsvFile(const QString &filePath, std::vector<Candle> &candles, double multiplier);
};

#endif // MAINWINDOW_H
