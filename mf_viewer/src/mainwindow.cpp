#include "mainwindow.h"
#include "mf_view.h"
#include "logger.h"

#include <QDockWidget>
#include <QStatusBar>
#include <QDateTime>
#include <QFile>
#include <QDataStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QColorDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QFrame>
#include <cmath>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent),
	  _view(new MfView(this)),
	  _infoLabel(new QLabel(this)),
	  _scrollBar(new QScrollBar(Qt::Horizontal, this)),
	  _instrumentName(""),
	  _hmmStartIndex(-1),
	  _hmmEndIndex(-1),
	  _selectingStartPoint(false),
	  _selectingEndPoint(false),
	  _isTraining(false),
	  _hmmClient(new HmmClient(this))
{
	setupUi();
}

void MainWindow::setupUi()
{
	setWindowTitle("MF Viewer");

	createMenus();

	QWidget *centralWidget = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(centralWidget);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	layout->addWidget(_view);
	layout->addWidget(_scrollBar);

	setCentralWidget(centralWidget);

	// Info dock
	QDockWidget *infoDock = new QDockWidget("Info", this);
	infoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	infoDock->setWidget(_infoLabel);
	addDockWidget(Qt::RightDockWidgetArea, infoDock);

	// Layers dock
	_layersDock = new QDockWidget("Layers", this);
	_layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	QWidget *layersWidget = new QWidget();
	QVBoxLayout *layersLayout = new QVBoxLayout(layersWidget);

	_layersListWidget = new QListWidget();
	layersLayout->addWidget(_layersListWidget);

	// First row of buttons: Remove and Config
	QHBoxLayout *buttonsLayout1 = new QHBoxLayout();
	_removeLayerBtn = new QPushButton("Remove");
	_configureLayerBtn = new QPushButton("Config");
	buttonsLayout1->addWidget(_removeLayerBtn);
	buttonsLayout1->addWidget(_configureLayerBtn);
	layersLayout->addLayout(buttonsLayout1);

	// Second row of buttons: Move Up and Move Down
	QHBoxLayout *buttonsLayout2 = new QHBoxLayout();
	_moveUpBtn = new QPushButton("↑ Up");
	_moveDownBtn = new QPushButton("↓ Down");
	buttonsLayout2->addWidget(_moveUpBtn);
	buttonsLayout2->addWidget(_moveDownBtn);
	layersLayout->addLayout(buttonsLayout2);

	_layersDock->setWidget(layersWidget);
	addDockWidget(Qt::RightDockWidgetArea, _layersDock);

	statusBar()->showMessage("Ready");

	connect(_view, &MfView::candleClicked,
	        this, &MainWindow::onCandleClicked);
	connect(_scrollBar, &QScrollBar::valueChanged,
	        _view, &MfView::scrollTo);
	connect(_view, &MfView::viewScrolled,
	        this, &MainWindow::onViewScrolled);
	connect(_view, &MfView::candleHovered,
	        this, &MainWindow::onCandleHovered);
	connect(_layersListWidget, &QListWidget::itemClicked,
	        this, &MainWindow::onLayerItemClicked);
	connect(_removeLayerBtn, &QPushButton::clicked,
	        this, &MainWindow::onRemoveLayer);
	connect(_configureLayerBtn, &QPushButton::clicked,
	        this, &MainWindow::onConfigureLayer);
	connect(_moveUpBtn, &QPushButton::clicked,
	        this, &MainWindow::onMoveLayerUp);
	connect(_moveDownBtn, &QPushButton::clicked,
	        this, &MainWindow::onMoveLayerDown);

	// Setup HMM training dock
	setupHmmDock();

	// Connect HMM point selection from view
	connect(_view, &MfView::pointSelected,
	        this, &MainWindow::onHmmPointSelected);

	// Connect HMM client signals
	connect(_hmmClient, &HmmClient::connected,
	        this, &MainWindow::onHmmConnected);
	connect(_hmmClient, &HmmClient::disconnected,
	        this, &MainWindow::onHmmDisconnected);
	connect(_hmmClient, &HmmClient::connectionError,
	        this, &MainWindow::onHmmConnectionError);
	connect(_hmmClient, &HmmClient::trainingStarted,
	        this, &MainWindow::onHmmTrainingStarted);
	connect(_hmmClient, &HmmClient::trainingProgress,
	        this, &MainWindow::onHmmTrainingProgress);
	connect(_hmmClient, &HmmClient::trainingComplete,
	        this, &MainWindow::onHmmTrainingComplete);
	connect(_hmmClient, &HmmClient::trainingError,
	        this, &MainWindow::onHmmTrainingError);
	connect(_hmmClient, &HmmClient::predictionsReceived,
	        this, &MainWindow::onPredictionsReceived);
	connect(_hmmClient, &HmmClient::inferenceError,
	        this, &MainWindow::onInferenceError);
}

void MainWindow::createMenus()
{
	QMenuBar *menuBar = new QMenuBar(this);
	setMenuBar(menuBar);

	_fileMenu = menuBar->addMenu("&File");

	_openAction = new QAction("&Open Data File...", this);
	_openAction->setShortcut(QKeySequence::Open);
	connect(_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);
	_fileMenu->addAction(_openAction);

	_fileMenu->addSeparator();

	_loadModelAction = new QAction("Load HMM &Model...", this);
	connect(_loadModelAction, &QAction::triggered, this, &MainWindow::onLoadHmmModel);
	_fileMenu->addAction(_loadModelAction);

	_saveModelAction = new QAction("&Save HMM Model...", this);
	_saveModelAction->setEnabled(false);  // Enable when model is trained
	connect(_saveModelAction, &QAction::triggered, this, &MainWindow::onSaveHmmModel);
	_fileMenu->addAction(_saveModelAction);

	_fileMenu->addSeparator();

	QAction *quitAction = new QAction("&Quit", this);
	quitAction->setShortcut(QKeySequence::Quit);
	connect(quitAction, &QAction::triggered, this, &QMainWindow::close);
	_fileMenu->addAction(quitAction);
}

void MainWindow::setupHmmDock()
{
	_hmmDock = new QDockWidget("HMM Train", this);
	_hmmDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	QWidget *hmmWidget = new QWidget();
	QVBoxLayout *hmmLayout = new QVBoxLayout(hmmWidget);

	// Start point section
	_startPointLabel = new QLabel("Start: [not set]");
	hmmLayout->addWidget(_startPointLabel);

	_setStartBtn = new QPushButton("Set Start Point");
	hmmLayout->addWidget(_setStartBtn);

	// End point section
	_endPointLabel = new QLabel("End: [not set]");
	hmmLayout->addWidget(_endPointLabel);

	_setEndBtn = new QPushButton("Set End Point");
	_setEndBtn->setEnabled(false);
	hmmLayout->addWidget(_setEndBtn);

	// Separator
	QFrame *line1 = new QFrame();
	line1->setFrameShape(QFrame::HLine);
	hmmLayout->addWidget(line1);

	// Input window size
	QHBoxLayout *inputLayout = new QHBoxLayout();
	inputLayout->addWidget(new QLabel("Input window:"));
	_inputWindowSpin = new QSpinBox();
	_inputWindowSpin->setRange(1, 1000);
	_inputWindowSpin->setValue(10);
	_inputWindowSpin->setToolTip("Number of candlesticks as input (10 fields each)");
	inputLayout->addWidget(_inputWindowSpin);
	hmmLayout->addLayout(inputLayout);

	// Output window size
	QHBoxLayout *outputLayout = new QHBoxLayout();
	outputLayout->addWidget(new QLabel("Output window:"));
	_outputWindowSpin = new QSpinBox();
	_outputWindowSpin->setRange(1, 100);
	_outputWindowSpin->setValue(1);
	_outputWindowSpin->setToolTip("Number of candlesticks to predict (4 fields each)");
	outputLayout->addWidget(_outputWindowSpin);
	hmmLayout->addLayout(outputLayout);

	// Number of hidden states (regimes)
	QHBoxLayout *statesLayout = new QHBoxLayout();
	statesLayout->addWidget(new QLabel("Regimes:"));
	_hiddenStatesSpin = new QSpinBox();
	_hiddenStatesSpin->setRange(2, 256);
	_hiddenStatesSpin->setValue(16);
	_hiddenStatesSpin->setToolTip("Number of hidden states (market regimes) to discover.\n"
	                              "Rule of thumb: keep N² < number of training samples.\n"
	                              "More states = more expressive but risk overfitting.");
	statesLayout->addWidget(_hiddenStatesSpin);
	hmmLayout->addLayout(statesLayout);

	// Model type selection
	QHBoxLayout *modelLayout = new QHBoxLayout();
	modelLayout->addWidget(new QLabel("Model:"));
	_modelTypeCombo = new QComboBox();
	_modelTypeCombo->addItem("HMM (1st order)", 1);
	_modelTypeCombo->addItem("HMM (2nd order)", 2);
	_modelTypeCombo->addItem("HMM (3rd order)", 3);
	_modelTypeCombo->addItem("HSMM (Semi-Markov)", 10);
	_modelTypeCombo->setToolTip("Model type:\n"
	                            "HMM 1st order - Standard Markov model\n"
	                            "HMM 2nd order - Transitions depend on 2 previous states\n"
	                            "HMM 3rd order - Transitions depend on 3 previous states\n"
	                            "HSMM - Semi-Markov with explicit state durations");
	modelLayout->addWidget(_modelTypeCombo);
	hmmLayout->addLayout(modelLayout);

	// Max duration (for HSMM only)
	QHBoxLayout *durationLayout = new QHBoxLayout();
	_maxDurationLabel = new QLabel("Max duration:");
	durationLayout->addWidget(_maxDurationLabel);
	_maxDurationSpin = new QSpinBox();
	_maxDurationSpin->setRange(2, 100);
	_maxDurationSpin->setValue(20);
	_maxDurationSpin->setToolTip("Maximum state duration for HSMM.\n"
	                              "Higher values allow longer state dwell times\n"
	                              "but require more memory (N × D parameters).");
	durationLayout->addWidget(_maxDurationSpin);
	hmmLayout->addLayout(durationLayout);

	// Initially hide duration controls (HMM default)
	_maxDurationLabel->setVisible(false);
	_maxDurationSpin->setVisible(false);

	// Separator
	QFrame *line2 = new QFrame();
	line2->setFrameShape(QFrame::HLine);
	hmmLayout->addWidget(line2);

	// Train/Stop buttons
	QHBoxLayout *trainBtnLayout = new QHBoxLayout();
	_trainBtn = new QPushButton("Train");
	_trainBtn->setEnabled(false);
	_stopTrainBtn = new QPushButton("Stop");
	_stopTrainBtn->setEnabled(false);
	trainBtnLayout->addWidget(_trainBtn);
	trainBtnLayout->addWidget(_stopTrainBtn);
	hmmLayout->addLayout(trainBtnLayout);

	// Progress bar
	_trainingProgress = new QProgressBar();
	_trainingProgress->setRange(0, 100);
	_trainingProgress->setValue(0);
	hmmLayout->addWidget(_trainingProgress);

	// Status label
	_hmmStatusLabel = new QLabel("Status: Ready");
	hmmLayout->addWidget(_hmmStatusLabel);

	// Add stretch to push everything up
	hmmLayout->addStretch();

	_hmmDock->setWidget(hmmWidget);
	addDockWidget(Qt::RightDockWidgetArea, _hmmDock);

	// Connect HMM buttons
	connect(_setStartBtn, &QPushButton::clicked,
	        this, &MainWindow::onSetStartPoint);
	connect(_setEndBtn, &QPushButton::clicked,
	        this, &MainWindow::onSetEndPoint);
	connect(_trainBtn, &QPushButton::clicked,
	        this, &MainWindow::onStartTraining);
	connect(_stopTrainBtn, &QPushButton::clicked,
	        this, &MainWindow::onStopTraining);

	// Model type selection - show/hide duration controls
	connect(_modelTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &MainWindow::onModelTypeChanged);
}

void MainWindow::onModelTypeChanged(int index)
{
	int modelType = _modelTypeCombo->itemData(index).toInt();
	bool isHSMM = (modelType == 10);  // HSMM model type

	_maxDurationLabel->setVisible(isHSMM);
	_maxDurationSpin->setVisible(isHSMM);
}

void MainWindow::updateHmmButtonStates()
{
	bool hasStart = (_hmmStartIndex >= 0);
	bool hasEnd = (_hmmEndIndex >= 0);
	bool hasValidRange = hasStart && hasEnd && (_hmmEndIndex > _hmmStartIndex);

	// Set Start Point: enabled only when no selection in progress AND (no start yet OR both points set)
	// This keeps it disabled after start is set until end is also set
	_setStartBtn->setEnabled(!_selectingStartPoint && !_selectingEndPoint && !_isTraining &&
	                         (!hasStart || hasValidRange));
	_setEndBtn->setEnabled(hasStart && !hasEnd && !_selectingStartPoint && !_selectingEndPoint && !_isTraining);
	_trainBtn->setEnabled(hasValidRange && !_isTraining && !_selectingStartPoint && !_selectingEndPoint);
	_stopTrainBtn->setEnabled(_isTraining);
}

void MainWindow::onSetStartPoint()
{
	_selectingStartPoint = true;
	_selectingEndPoint = false;
	_hmmStartIndex = -1;
	_hmmEndIndex = -1;
	_startPointLabel->setText("Start: [click on chart...]");
	_endPointLabel->setText("End: [not set]");
	_setStartBtn->setEnabled(false);
	_hmmStatusLabel->setText("Status: Click on chart to set start point");

	// Tell view to enter selection mode
	_view->setSelectionMode(true);

	updateHmmButtonStates();
}

void MainWindow::onSetEndPoint()
{
	_selectingEndPoint = true;
	_selectingStartPoint = false;
	_endPointLabel->setText("End: [click on chart...]");
	_setEndBtn->setEnabled(false);
	_hmmStatusLabel->setText("Status: Click on chart to set end point");

	// Tell view to enter selection mode
	_view->setSelectionMode(true);

	updateHmmButtonStates();
}

void MainWindow::onHmmPointSelected(int candleIndex, int64_t timestamp)
{
	QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestamp, Qt::UTC);
	QString dateStr = dt.toString("yyyy-MM-dd HH:mm");

	if (_selectingStartPoint) {
		_hmmStartIndex = candleIndex;
		_startPointLabel->setText(QString("Start: %1 (idx %2)").arg(dateStr).arg(candleIndex));
		_selectingStartPoint = false;
		_hmmStatusLabel->setText("Status: Start point set. Now set end point.");
	}
	else if (_selectingEndPoint) {
		if (candleIndex <= _hmmStartIndex) {
			_hmmStatusLabel->setText("Status: End must be after start!");
			return;
		}
		_hmmEndIndex = candleIndex;
		_endPointLabel->setText(QString("End: %1 (idx %2)").arg(dateStr).arg(candleIndex));
		_selectingEndPoint = false;

		int numSamples = _hmmEndIndex - _hmmStartIndex;
		_hmmStatusLabel->setText(QString("Status: Ready to train (%1 samples)").arg(numSamples));
	}

	_view->setSelectionMode(false);
	_view->setSelectionRange(_hmmStartIndex, _hmmEndIndex);
	updateHmmButtonStates();
}

void MainWindow::onStartTraining()
{
	// Validate parameters
	int inputWindow = _inputWindowSpin->value();
	int outputWindow = _outputWindowSpin->value();

	if (inputWindow <= 0 || outputWindow <= 0) {
		QMessageBox::warning(this, "Invalid Parameters",
		                     "Input and output window sizes must be > 0");
		return;
	}

	int numSamples = _hmmEndIndex - _hmmStartIndex;
	int minRequired = inputWindow + outputWindow;
	if (numSamples < minRequired) {
		QMessageBox::warning(this, "Insufficient Data",
		                     QString("Need at least %1 samples (input + output windows), but only have %2")
		                     .arg(minRequired).arg(numSamples));
		return;
	}

	// Warn about overfitting risk if N² > T
	int numHiddenStates = _hiddenStatesSpin->value();
	int numTrainingSamples = numSamples - inputWindow + 1;
	if (numHiddenStates * numHiddenStates > numTrainingSamples) {
		auto reply = QMessageBox::question(this, "Overfitting Risk",
		                     QString("You have %1 training samples but %2 regimes.\n"
		                             "The transition matrix has %3 parameters (N²).\n\n"
		                             "This may cause overfitting. Consider using fewer regimes "
		                             "(recommended: %4 or less).\n\n"
		                             "Continue anyway?")
		                     .arg(numTrainingSamples)
		                     .arg(numHiddenStates)
		                     .arg(numHiddenStates * numHiddenStates)
		                     .arg(static_cast<int>(sqrt(numTrainingSamples))),
		                     QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::No) {
			return;
		}
	}

	// Check if we have a data file
	if (_currentDataFile.isEmpty()) {
		QMessageBox::warning(this, "No Data File",
		                     "Please load a .tfx data file first");
		return;
	}

	_isTraining = true;
	_trainBtn->setEnabled(false);
	_stopTrainBtn->setEnabled(true);
	_trainingProgress->setValue(0);
	_hmmStatusLabel->setText("Status: Connecting to training daemon...");

	// Generate model output path
	QString modelOutput = QString("/tmp/model_%1.pmod")
	                      .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

	// Log training start
	Logger::instance().logTrainingStart(_hmmStartIndex, _hmmEndIndex,
	                                    inputWindow, outputWindow);

	// Get model type and max duration
	int modelType = _modelTypeCombo->currentData().toInt();
	int maxDuration = _maxDurationSpin->value();

	// Start training via HMM client
	_hmmClient->startTraining(
		_currentDataFile,
		static_cast<uint32_t>(_hmmStartIndex),
		static_cast<uint32_t>(_hmmEndIndex),
		static_cast<uint32_t>(inputWindow),
		static_cast<uint32_t>(outputWindow),
		static_cast<uint32_t>(numHiddenStates),
		100,  // max_iterations
		modelOutput,
		static_cast<uint8_t>(modelType),
		static_cast<uint32_t>(maxDuration)
	);

	QString modelName = _modelTypeCombo->currentText();
	statusBar()->showMessage(QString("Training %1: samples %2-%3, input=%4, regimes=%5")
	                         .arg(modelName)
	                         .arg(_hmmStartIndex).arg(_hmmEndIndex)
	                         .arg(inputWindow).arg(numHiddenStates));

	updateHmmButtonStates();
}

void MainWindow::onStopTraining()
{
	_hmmClient->stopTraining();
	_hmmStatusLabel->setText("Status: Stopping training...");
	_stopTrainBtn->setEnabled(false);
}

bool MainWindow::loadBinaryFile(const QString &filePath, std::vector<Candle> &candles)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}

	QDataStream in(&file);
	in.setByteOrder(QDataStream::LittleEndian);

	std::vector<DataRecord> records;
	while (!in.atEnd()) {
		DataRecord r;
		in.readRawData(reinterpret_cast<char*>(&r), sizeof(DataRecord));
		if (in.status() != QDataStream::Ok) {
			break;
		}
		records.push_back(r);
	}
	file.close();

	for (const auto& record : records) {
		Candle c;

		// Direct reconstruction from the DataRecord fields (already int32 scaled)
		c.close = record.original_close;
		c.open = c.close - record.close_open_diff;
		c.high = c.open + record.high_open_diff;
		c.low = c.close + record.low_close_diff;

		c.volume = 0;

		QDateTime dt;
		dt.setDate(QDate(record.year_offset + 2000, record.month + 1, record.day + 1));
		dt.setTime(QTime(record.hour, record.minute_interval * 5));
		c.timestamp_ms = dt.toMSecsSinceEpoch();

		candles.push_back(c);
	}

	return !candles.empty();
}

bool MainWindow::loadTextFile(const QString &filePath, std::vector<Candle> &candles)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	while (!file.atEnd()) {
		QByteArray line = file.readLine();
		QList<QByteArray> fields = line.split('\t');

		if (fields.size() < 11) {
			continue;
		}

		Candle c;
		int year = fields[0].toInt();
		int month = fields[1].toInt();
		int day = fields[2].toInt();
		int hour = fields[3].toInt();
		int minute = fields[4].toInt();

		c.open = fields[6].toDouble();
		c.high = fields[7].toDouble();
		c.low = fields[8].toDouble();
		c.close = fields[9].toDouble();
		c.volume = fields[10].toInt();

		QDateTime dt;
		dt.setDate(QDate(year + 2000, month, day));
		dt.setTime(QTime(hour, minute));
		c.timestamp_ms = dt.toMSecsSinceEpoch();

		candles.push_back(c);
	}

	file.close();
	return !candles.empty();
}

bool MainWindow::loadMt4CsvFile(const QString &filePath, std::vector<Candle> &candles, double multiplier)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	while (!file.atEnd()) {
		QByteArray line = file.readLine().trimmed();
		if (line.isEmpty()) continue;

		// Format: YYYY.MM.DD,HH:MM,open,high,low,close,volume
		// Also support tab separator
		QList<QByteArray> fields;
		if (line.contains('\t')) {
			fields = line.split('\t');
		} else {
			fields = line.split(',');
		}

		if (fields.size() < 7) {
			continue;
		}

		// Parse date: YYYY.MM.DD
		QList<QByteArray> dateParts = fields[0].split('.');
		if (dateParts.size() < 3) continue;

		int year = dateParts[0].toInt();
		int month = dateParts[1].toInt();
		int day = dateParts[2].toInt();

		// Parse time: HH:MM
		QList<QByteArray> timeParts = fields[1].split(':');
		if (timeParts.size() < 2) continue;

		int hour = timeParts[0].toInt();
		int minute = timeParts[1].toInt();

		Candle c;
		c.open = fields[2].toDouble() * multiplier;
		c.high = fields[3].toDouble() * multiplier;
		c.low = fields[4].toDouble() * multiplier;
		c.close = fields[5].toDouble() * multiplier;
		c.volume = fields[6].toInt();

		QDateTime dt;
		dt.setDate(QDate(year, month, day));
		dt.setTime(QTime(hour, minute));
		c.timestamp_ms = dt.toMSecsSinceEpoch();

		candles.push_back(c);
	}

	file.close();
	return !candles.empty();
}

void MainWindow::addLayer(const QString &filePath)
{
	std::vector<Candle> candles;
	QFileInfo fileInfo(filePath);
	bool success = false;

	// Detect format by extension
	if (fileInfo.suffix() == "tfx") {
		success = loadBinaryFile(filePath, candles);
		if (success) {
			_currentDataFile = filePath;  // Store for HMM training
		}
	} else if (fileInfo.suffix() == "mt4csv") {
		// Ask for multiplier
		bool ok;
		double multiplier = QInputDialog::getDouble(
			this,
			"Price Multiplier",
			"Enter multiplier for floating point prices:",
			100000.0,  // default
			1.0,       // min
			1e12,      // max
			0,         // decimals
			&ok
		);
		if (!ok) {
			statusBar()->showMessage("Load cancelled");
			return;
		}
		success = loadMt4CsvFile(filePath, candles, multiplier);
	} else {
		// Try text format
		success = loadTextFile(filePath, candles);
	}

	if (!success || candles.empty()) {
		statusBar()->showMessage(QString("Error: Could not load %1").arg(filePath));
		return;
	}

	auto layer = std::make_shared<Layer>(fileInfo.fileName(), candles);
	_layers.push_back(layer);

	updateLayersList();
	updateView();
	_view->recalculateExtent(); // Fit new layer into view

	statusBar()->showMessage(QString("Loaded layer: %1 (%2 candles)").arg(fileInfo.fileName()).arg(candles.size()));
}

void MainWindow::updateLayersList()
{
	_layersListWidget->clear();
	for (const auto &layer : _layers) {
		QListWidgetItem *item = new QListWidgetItem(layer->name());
		item->setCheckState(layer->isVisible() ? Qt::Checked : Qt::Unchecked);
		_layersListWidget->addItem(item);
	}
}

void MainWindow::updateView()
{
	_view->setInstrumentName(_instrumentName);
	_view->setLayers(_layers);

	// Setup scrollbar
	int candleCount = _view->getCandleCount();
	int visibleCount = _view->getVisibleCount();
	if (candleCount > visibleCount) {
		_scrollBar->setRange(0, candleCount - visibleCount);
		_scrollBar->setPageStep(visibleCount);
		_scrollBar->setVisible(true);
	} else {
		_scrollBar->setVisible(false);
	}
}

void MainWindow::updateViewKeepExtent()
{
	// Update view without recalculating extent (for visibility toggles and reordering)
	_view->setInstrumentName(_instrumentName);
	_view->setLayers(_layers);

	// Setup scrollbar
	int candleCount = _view->getCandleCount();
	int visibleCount = _view->getVisibleCount();
	if (candleCount > visibleCount) {
		_scrollBar->setRange(0, candleCount - visibleCount);
		_scrollBar->setPageStep(visibleCount);
		_scrollBar->setVisible(true);
	} else {
		_scrollBar->setVisible(false);
	}
}

void MainWindow::onCandleClicked(const Candle &c, int layerIndex, int candleIndex)
{
	if (layerIndex < 0 || layerIndex >= static_cast<int>(_layers.size())) {
		return;
	}

	const auto &layer = _layers[layerIndex];
	QDateTime dt = QDateTime::fromMSecsSinceEpoch(c.timestamp_ms, Qt::UTC);

	QString info_text = QString("Layer: %1\nCandle Index: %2\nTime (UTC): %3\n\nO: %4\nH: %5\nL: %6\nC: %7\nV: %8")
		.arg(layer->name())
		.arg(candleIndex)
		.arg(dt.toString(Qt::ISODate))
		.arg(c.open, 0, 'f', 5)
		.arg(c.high, 0, 'f', 5)
		.arg(c.low, 0, 'f', 5)
		.arg(c.close, 0, 'f', 5)
		.arg(c.volume);

	_infoLabel->setText(info_text);
	statusBar()->showMessage(QString("Layer %1, Candle %2 clicked").arg(layer->name()).arg(candleIndex));
}

void MainWindow::onViewScrolled(int firstVisibleIndex)
{
	_scrollBar->blockSignals(true);
	_scrollBar->setValue(firstVisibleIndex);
	_scrollBar->blockSignals(false);

	// Update scrollbar range on zoom
	int candleCount = _view->getCandleCount();
	int visibleCount = _view->getVisibleCount();
	if (candleCount > visibleCount) {
		_scrollBar->setRange(0, candleCount - visibleCount);
		_scrollBar->setPageStep(visibleCount);
		_scrollBar->setVisible(true);
	} else {
		_scrollBar->setVisible(false);
	}
}

void MainWindow::onCandleHovered(const Candle &c, int layerIndex, int candleIndex)
{
	if (layerIndex < 0 || layerIndex >= static_cast<int>(_layers.size())) {
		return;
	}

	const auto &layer = _layers[layerIndex];
	QString text = QString("Layer: %1 | Idx: %2 | O: %3, H: %4, L: %5, C: %6")
		.arg(layer->name())
		.arg(candleIndex)
		.arg(c.open, 0, 'f', 5)
		.arg(c.high, 0, 'f', 5)
		.arg(c.low, 0, 'f', 5)
		.arg(c.close, 0, 'f', 5);
	statusBar()->showMessage(text);
}

void MainWindow::onOpenFile()
{
	QString filePath = QFileDialog::getOpenFileName(
		this,
		"Open Data File",
		"/home/mf/mf/bitforce/fdmgr",
		"Data Files (*.tfx);;MT4 CSV (*.mt4csv);;Text Files (*.txt);;All Files (*)"
	);

	if (!filePath.isEmpty()) {
		addLayer(filePath);
	}
}

void MainWindow::onLayerItemClicked(QListWidgetItem *item)
{
	int row = _layersListWidget->row(item);
	if (row >= 0 && row < static_cast<int>(_layers.size())) {
		bool visible = (item->checkState() == Qt::Checked);
		_layers[row]->setVisible(visible);
		updateViewKeepExtent(); // Keep current extent when toggling visibility
	}
}

void MainWindow::onRemoveLayer()
{
	int currentRow = _layersListWidget->currentRow();
	if (currentRow >= 0 && currentRow < static_cast<int>(_layers.size())) {
		_layers.erase(_layers.begin() + currentRow);
		updateLayersList();
		updateView();
		statusBar()->showMessage("Layer removed");
	}
}

void MainWindow::onConfigureLayer()
{
	int currentRow = _layersListWidget->currentRow();
	if (currentRow < 0 || currentRow >= static_cast<int>(_layers.size())) {
		return;
	}

	auto &layer = _layers[currentRow];
	LayerColors colors = layer->colors();

	// Create a simple color configuration dialog
	QDialog dialog(this);
	dialog.setWindowTitle(QString("Configure Layer: %1").arg(layer->name()));
	QVBoxLayout *layout = new QVBoxLayout(&dialog);

	// Up color
	QPushButton *upColorBtn = new QPushButton("Up Color");
	upColorBtn->setStyleSheet(QString("background-color: %1").arg(colors.upColor.name()));
	connect(upColorBtn, &QPushButton::clicked, [&]() {
		QColor color = QColorDialog::getColor(colors.upColor, this, "Select Up Color");
		if (color.isValid()) {
			colors.upColor = color;
			upColorBtn->setStyleSheet(QString("background-color: %1").arg(color.name()));
		}
	});
	layout->addWidget(upColorBtn);

	// Down color
	QPushButton *downColorBtn = new QPushButton("Down Color");
	downColorBtn->setStyleSheet(QString("background-color: %1").arg(colors.downColor.name()));
	connect(downColorBtn, &QPushButton::clicked, [&]() {
		QColor color = QColorDialog::getColor(colors.downColor, this, "Select Down Color");
		if (color.isValid()) {
			colors.downColor = color;
			downColorBtn->setStyleSheet(QString("background-color: %1").arg(color.name()));
		}
	});
	layout->addWidget(downColorBtn);

	// Doji color
	QPushButton *dojiColorBtn = new QPushButton("Doji Color");
	dojiColorBtn->setStyleSheet(QString("background-color: %1").arg(colors.dojiColor.name()));
	connect(dojiColorBtn, &QPushButton::clicked, [&]() {
		QColor color = QColorDialog::getColor(colors.dojiColor, this, "Select Doji Color");
		if (color.isValid()) {
			colors.dojiColor = color;
			dojiColorBtn->setStyleSheet(QString("background-color: %1").arg(color.name()));
		}
	});
	layout->addWidget(dojiColorBtn);

	// OK / Cancel buttons
	QHBoxLayout *btnLayout = new QHBoxLayout();
	QPushButton *okBtn = new QPushButton("OK");
	QPushButton *cancelBtn = new QPushButton("Cancel");
	connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
	connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
	btnLayout->addWidget(okBtn);
	btnLayout->addWidget(cancelBtn);
	layout->addLayout(btnLayout);

	if (dialog.exec() == QDialog::Accepted) {
		layer->setColors(colors);
		updateView();
		statusBar()->showMessage("Layer colors updated");
	}
}

void MainWindow::onMoveLayerUp()
{
	int currentRow = _layersListWidget->currentRow();
	if (currentRow <= 0 || currentRow >= static_cast<int>(_layers.size())) {
		return; // Can't move up if it's the first item or invalid
	}

	// Swap with the item above
	std::swap(_layers[currentRow], _layers[currentRow - 1]);

	// Update UI and maintain selection
	updateLayersList();
	_layersListWidget->setCurrentRow(currentRow - 1);
	updateViewKeepExtent(); // Keep current extent when reordering
	statusBar()->showMessage("Layer moved up");
}

void MainWindow::onMoveLayerDown()
{
	int currentRow = _layersListWidget->currentRow();
	if (currentRow < 0 || currentRow >= static_cast<int>(_layers.size()) - 1) {
		return; // Can't move down if it's the last item or invalid
	}

	// Swap with the item below
	std::swap(_layers[currentRow], _layers[currentRow + 1]);

	// Update UI and maintain selection
	updateLayersList();
	_layersListWidget->setCurrentRow(currentRow + 1);
	updateViewKeepExtent(); // Keep current extent when reordering
	statusBar()->showMessage("Layer moved down");
}

// HMM Client slot implementations

void MainWindow::onHmmConnected()
{
	_hmmStatusLabel->setText("Status: Connected to training daemon");
}

void MainWindow::onHmmDisconnected()
{
	if (_isTraining) {
		_isTraining = false;
		_hmmStatusLabel->setText("Status: Disconnected from daemon");
		updateHmmButtonStates();
	}
}

void MainWindow::onHmmConnectionError(const QString &error)
{
	_isTraining = false;
	_hmmStatusLabel->setText(QString("Status: Connection error"));
	QMessageBox::warning(this, "Connection Error",
	                     QString("Failed to connect to training daemon:\n%1\n\n"
	                             "Make sure pmod_train_daemon is running.").arg(error));
	updateHmmButtonStates();
}

void MainWindow::onHmmTrainingStarted()
{
	_hmmStatusLabel->setText("Status: Training started...");
}

void MainWindow::onHmmTrainingProgress(int iteration, int maxIterations,
                                       double logLikelihood, double improvement,
                                       double elapsedSec)
{
	Q_UNUSED(improvement);

	// Log progress
	Logger::instance().logTrainingProgress(iteration, maxIterations, logLikelihood);

	// Update progress bar
	int progress = (maxIterations > 0) ? (iteration * 100 / maxIterations) : 0;
	_trainingProgress->setValue(progress);

	// Update status label
	_hmmStatusLabel->setText(QString("Status: Iteration %1/%2, LL: %3 (%.1fs)")
	                         .arg(iteration).arg(maxIterations)
	                         .arg(logLikelihood, 0, 'f', 4)
	                         .arg(elapsedSec));

	statusBar()->showMessage(QString("Training: iter %1/%2, LL=%3")
	                         .arg(iteration).arg(maxIterations)
	                         .arg(logLikelihood, 0, 'f', 4));
}

void MainWindow::onHmmTrainingComplete(bool success, int totalIterations,
                                       double finalLogLikelihood, double trainTime,
                                       const QString &modelPath)
{
	_isTraining = false;
	_trainingProgress->setValue(100);

	// Log completion
	Logger::instance().logTrainingComplete(totalIterations, finalLogLikelihood, trainTime);

	if (success) {
		_currentModelPath = modelPath;
		_hmmStatusLabel->setText(QString("Status: Complete! %1 iters, LL=%2")
		                         .arg(totalIterations)
		                         .arg(finalLogLikelihood, 0, 'f', 4));

		QMessageBox::information(this, "Training Complete",
		                         QString("Training completed successfully!\n\n"
		                                 "Iterations: %1\n"
		                                 "Final LL: %2\n"
		                                 "Time: %3 seconds\n"
		                                 "Model saved to: %4")
		                         .arg(totalIterations)
		                         .arg(finalLogLikelihood, 0, 'f', 6)
		                         .arg(trainTime, 0, 'f', 1)
		                         .arg(modelPath));

		/* Request predictions for candles after training range */
		int numPredictions = _outputWindowSpin->value();
		if (numPredictions > 0 && !_currentDataFile.isEmpty()) {
			_hmmStatusLabel->setText("Status: Running inference...");
			_hmmClient->requestInference(
				modelPath,
				_currentDataFile,
				_hmmEndIndex - _inputWindowSpin->value(),  /* Start of input window */
				_inputWindowSpin->value(),                 /* Input window size */
				numPredictions
			);
		}
	} else {
		_hmmStatusLabel->setText(QString("Status: Stopped after %1 iters")
		                         .arg(totalIterations));
	}

	updateHmmButtonStates();
}

void MainWindow::onHmmTrainingError(const QString &error)
{
	_isTraining = false;
	_trainingProgress->setValue(0);
	_hmmStatusLabel->setText("Status: Training error");

	// Log error
	Logger::instance().logTrainingError(error);

	QMessageBox::critical(this, "Training Error",
	                      QString("Training failed:\n%1").arg(error));

	updateHmmButtonStates();
}

void MainWindow::onLoadHmmModel()
{
	QString filePath = QFileDialog::getOpenFileName(
		this,
		"Load HMM Model",
		"/tmp",
		"PMOD Files (*.pmod);;All Files (*)"
	);

	if (filePath.isEmpty()) {
		return;
	}

	// Check if file exists and is readable
	QFile file(filePath);
	if (!file.exists()) {
		QMessageBox::warning(this, "File Not Found",
		                     QString("Model file not found:\n%1").arg(filePath));
		return;
	}

	_currentModelPath = filePath;
	_saveModelAction->setEnabled(true);

	Logger::instance().info(QString("Loaded HMM model: %1").arg(filePath));
	_hmmStatusLabel->setText(QString("Model: %1").arg(QFileInfo(filePath).fileName()));
	statusBar()->showMessage(QString("Model loaded: %1").arg(filePath));

	// TODO: Connect to inference server for predictions
	// For now, just store the model path
}

void MainWindow::onSaveHmmModel()
{
	if (_currentModelPath.isEmpty()) {
		QMessageBox::warning(this, "No Model",
		                     "No HMM model is currently loaded or trained.");
		return;
	}

	QString defaultName = QFileInfo(_currentModelPath).fileName();
	QString filePath = QFileDialog::getSaveFileName(
		this,
		"Save HMM Model",
		QString("/tmp/%1").arg(defaultName),
		"PMOD Files (*.pmod);;All Files (*)"
	);

	if (filePath.isEmpty()) {
		return;
	}

	// Copy the model file to the new location
	if (_currentModelPath != filePath) {
		QFile::remove(filePath);  // Remove if exists
		if (!QFile::copy(_currentModelPath, filePath)) {
			QMessageBox::critical(this, "Save Failed",
			                      QString("Failed to save model to:\n%1").arg(filePath));
			return;
		}
	}

	_currentModelPath = filePath;
	Logger::instance().info(QString("Saved HMM model: %1").arg(filePath));
	statusBar()->showMessage(QString("Model saved: %1").arg(filePath));
}

void MainWindow::onPredictionsReceived(int startIndex, const QVector<HmmPredictedCandle> &predictions)
{
	Logger::instance().info(QString("Received %1 predictions starting at index %2")
	                        .arg(predictions.size())
	                        .arg(startIndex));

	/* Convert from HmmPredictedCandle to PredictedCandle (prediction_layer.h) */
	std::vector<PredictedCandle> viewPredictions;
	viewPredictions.reserve(predictions.size());

	for (const auto &pred : predictions) {
		PredictedCandle candle;
		candle.high_open_diff = pred.high_open_diff;
		candle.close_open_diff = pred.close_open_diff;
		candle.low_close_diff = pred.low_close_diff;
		candle.high_low_diff = pred.high_low_diff;
		candle.confidence = static_cast<double>(pred.confidence);
		viewPredictions.push_back(candle);
	}

	_view->setPredictions(startIndex, viewPredictions);
	_hmmStatusLabel->setText(QString("Status: Ready (%1 predictions)").arg(predictions.size()));
	statusBar()->showMessage(QString("Displaying %1 predicted candles").arg(predictions.size()));
}

void MainWindow::onInferenceError(const QString &error)
{
	Logger::instance().logTrainingError(QString("Inference error: %1").arg(error));
	_hmmStatusLabel->setText("Status: Inference failed");
	statusBar()->showMessage(QString("Inference error: %1").arg(error));
}
