#include "micro_harvest_widget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
static const int job_type_wave_inference = 6;

static void set_table_headers(QTableWidget *table, const QStringList &headers)
{
	table->setColumnCount(headers.size());
	table->setHorizontalHeaderLabels(headers);
	table->verticalHeader()->setVisible(false);
}

static QString job_status_name(int status)
{
	switch (status) {
	case 1:
		return "queued";
	case 2:
		return "running";
	case 3:
		return "completed";
	case 4:
		return "failed";
	case 5:
		return "cancelled";
	default:
		return QString("status_%1").arg(status);
	}
}

static QString json_value_brief(const QJsonValue &value)
{
	if (value.isBool()) {
		return value.toBool() ? "true" : "false";
	}
	if (value.isDouble()) {
		return QString::number(value.toDouble(), 'g', 15);
	}
	if (value.isString()) {
		return value.toString();
	}
	if (value.isNull()) {
		return "null";
	}
	return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}
}

micro_harvest_widget::micro_harvest_widget(QWidget *parent)
	: QWidget(parent),
	  m_dataset_combo(new QComboBox(this)),
	  m_wave_job_combo(new QComboBox(this)),
	  m_symbol_combo(new QComboBox(this)),
	  m_timeframe_combo(new QComboBox(this)),
	  m_bias_combo(new QComboBox(this)),
	  m_lookback_days_spin(new QSpinBox(this)),
	  m_spread_pips_spin(new QSpinBox(this)),
	  m_max_orders_min_spin(new QSpinBox(this)),
	  m_max_orders_max_spin(new QSpinBox(this)),
	  m_spacing_min_spin(new QSpinBox(this)),
	  m_spacing_max_spin(new QSpinBox(this)),
	  m_spacing_step_spin(new QSpinBox(this)),
	  m_take_profit_min_spin(new QSpinBox(this)),
	  m_take_profit_max_spin(new QSpinBox(this)),
	  m_take_profit_step_spin(new QSpinBox(this)),
	  m_entry_edge_min_spin(new QSpinBox(this)),
	  m_entry_edge_max_spin(new QSpinBox(this)),
	  m_entry_edge_step_spin(new QSpinBox(this)),
	  m_min_height_spin(new QSpinBox(this)),
	  m_max_height_spin(new QSpinBox(this)),
	  m_refresh_datasets_btn(new QPushButton("Refresh Datasets", this)),
	  m_refresh_wave_jobs_btn(new QPushButton("Refresh Wave Jobs", this)),
	  m_refresh_streams_btn(new QPushButton("Refresh Streams", this)),
	  m_run_btn(new QPushButton("Run Replay Optimizer", this)),
	  m_load_latest_btn(new QPushButton("Load Latest", this)),
	  m_status_label(new QLabel("Micro harvest: idle", this)),
	  m_params_table(new QTableWidget(this)),
	  m_summary_table(new QTableWidget(this)),
	  m_islands_table(new QTableWidget(this)),
	  m_trades_table(new QTableWidget(this)),
	  m_chart(new cluster_chart_widget(this)),
	  m_poll_timer(new QTimer(this)),
	  m_active_job_id(0),
	  m_chart_fetch_start(0)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	QGridLayout *controls = new QGridLayout();
	QSplitter *split = new QSplitter(Qt::Horizontal, this);
	QWidget *left_panel = new QWidget(this);
	QVBoxLayout *left_layout = new QVBoxLayout(left_panel);
	QSplitter *left_split = new QSplitter(Qt::Vertical, left_panel);
	QWidget *chart_panel = new QWidget(this);
	QVBoxLayout *chart_layout = new QVBoxLayout(chart_panel);

	m_bias_combo->addItem("BUY");
	m_bias_combo->addItem("SELL");
	m_lookback_days_spin->setRange(1, 90);
	m_lookback_days_spin->setValue(14);
	m_spread_pips_spin->setRange(0, 200);
	m_spread_pips_spin->setValue(50);
	m_max_orders_min_spin->setRange(1, 32);
	m_max_orders_min_spin->setValue(1);
	m_max_orders_max_spin->setRange(1, 32);
	m_max_orders_max_spin->setValue(4);
	m_spacing_min_spin->setRange(1, 1000);
	m_spacing_min_spin->setValue(5);
	m_spacing_max_spin->setRange(1, 1000);
	m_spacing_max_spin->setValue(25);
	m_spacing_step_spin->setRange(1, 1000);
	m_spacing_step_spin->setValue(5);
	m_take_profit_min_spin->setRange(1, 1000);
	m_take_profit_min_spin->setValue(50);
	m_take_profit_max_spin->setRange(1, 1000);
	m_take_profit_max_spin->setValue(200);
	m_take_profit_step_spin->setRange(1, 1000);
	m_take_profit_step_spin->setValue(10);
	m_entry_edge_min_spin->setRange(0, 1000);
	m_entry_edge_min_spin->setValue(600);
	m_entry_edge_max_spin->setRange(0, 1000);
	m_entry_edge_max_spin->setValue(900);
	m_entry_edge_step_spin->setRange(1, 1000);
	m_entry_edge_step_spin->setValue(50);
	m_min_height_spin->setRange(1, 5000);
	m_min_height_spin->setValue(300);
	m_max_height_spin->setRange(1, 20000);
	m_max_height_spin->setValue(10000);
	m_dataset_combo->setMinimumContentsLength(24);
	m_wave_job_combo->setMinimumContentsLength(28);

	set_table_headers(m_params_table, {"parameter", "value"});
	set_table_headers(m_summary_table, {"metric", "value"});
	set_table_headers(m_islands_table, {"island", "selected", "start", "end", "height", "profit", "closed", "ignored"});
	set_table_headers(m_trades_table, {"island", "dir", "open", "close", "open_px", "close_px", "tp", "profit", "closed"});
	m_params_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_params_table->horizontalHeader()->setStretchLastSection(true);
	m_summary_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_summary_table->horizontalHeader()->setStretchLastSection(true);
	m_islands_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_trades_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_islands_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_islands_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_trades_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_trades_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_chart->setMinimumHeight(420);

	controls->addWidget(new QLabel("Dataset", this), 0, 0);
	controls->addWidget(m_dataset_combo, 0, 1, 1, 2);
	controls->addWidget(m_refresh_datasets_btn, 0, 3);
	controls->addWidget(new QLabel("Wave Job", this), 0, 4);
	controls->addWidget(m_wave_job_combo, 0, 5, 1, 2);
	controls->addWidget(m_refresh_wave_jobs_btn, 0, 7);
	controls->addWidget(new QLabel("Symbol", this), 0, 8);
	controls->addWidget(m_symbol_combo, 0, 9);
	controls->addWidget(new QLabel("Timeframe", this), 0, 10);
	controls->addWidget(m_timeframe_combo, 0, 11);
	controls->addWidget(m_refresh_streams_btn, 0, 12);
	controls->addWidget(new QLabel("Bias", this), 0, 13);
	controls->addWidget(m_bias_combo, 0, 14);
	controls->addWidget(new QLabel("Lookback Days", this), 0, 15);
	controls->addWidget(m_lookback_days_spin, 0, 16);
	controls->addWidget(new QLabel("Spread", this), 0, 17);
	controls->addWidget(m_spread_pips_spin, 0, 18);
	controls->addWidget(m_run_btn, 0, 19);
	controls->addWidget(m_load_latest_btn, 0, 20);

	controls->addWidget(new QLabel("Max Orders Min", this), 1, 0);
	controls->addWidget(m_max_orders_min_spin, 1, 1);
	controls->addWidget(new QLabel("Max Orders Max", this), 1, 2);
	controls->addWidget(m_max_orders_max_spin, 1, 3);
	controls->addWidget(new QLabel("Spacing Min", this), 1, 4);
	controls->addWidget(m_spacing_min_spin, 1, 5);
	controls->addWidget(new QLabel("Spacing Max", this), 1, 6);
	controls->addWidget(m_spacing_max_spin, 1, 7);
	controls->addWidget(new QLabel("Spacing Step", this), 1, 8);
	controls->addWidget(m_spacing_step_spin, 1, 9);

	controls->addWidget(new QLabel("TP Min", this), 2, 0);
	controls->addWidget(m_take_profit_min_spin, 2, 1);
	controls->addWidget(new QLabel("TP Max", this), 2, 2);
	controls->addWidget(m_take_profit_max_spin, 2, 3);
	controls->addWidget(new QLabel("TP Step", this), 2, 4);
	controls->addWidget(m_take_profit_step_spin, 2, 5);
	controls->addWidget(new QLabel("Entry Edge Min", this), 2, 6);
	controls->addWidget(m_entry_edge_min_spin, 2, 7);
	controls->addWidget(new QLabel("Entry Edge Max", this), 2, 8);
	controls->addWidget(m_entry_edge_max_spin, 2, 9);
	controls->addWidget(new QLabel("Entry Edge Step", this), 2, 10);
	controls->addWidget(m_entry_edge_step_spin, 2, 11);
	controls->addWidget(new QLabel("Min Height", this), 2, 12);
	controls->addWidget(m_min_height_spin, 2, 13);
	controls->addWidget(new QLabel("Max Height", this), 2, 14);
	controls->addWidget(m_max_height_spin, 2, 15);

	left_layout->setContentsMargins(0, 0, 0, 0);
	left_split->addWidget(m_params_table);
	left_split->addWidget(m_summary_table);
	left_split->addWidget(m_islands_table);
	left_split->addWidget(m_trades_table);
	left_split->setStretchFactor(0, 1);
	left_split->setStretchFactor(1, 1);
	left_split->setStretchFactor(2, 2);
	left_split->setStretchFactor(3, 3);
	left_layout->addWidget(left_split, 1);
	left_layout->addWidget(m_status_label);

	chart_layout->setContentsMargins(0, 0, 0, 0);
	chart_layout->addWidget(new QLabel("Micro Harvest Overlay", this));
	chart_layout->addWidget(m_chart, 1);

	split->addWidget(left_panel);
	split->addWidget(chart_panel);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 4);

	layout->addLayout(controls);
	layout->addWidget(split, 1);

	m_poll_timer->setInterval(1000);
	connect(m_refresh_datasets_btn, &QPushButton::clicked, this, &micro_harvest_widget::refresh_datasets);
	connect(m_refresh_wave_jobs_btn, &QPushButton::clicked, this, &micro_harvest_widget::refresh_wave_jobs);
	connect(m_refresh_streams_btn, &QPushButton::clicked, this, &micro_harvest_widget::refresh_streams);
	connect(m_dataset_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &micro_harvest_widget::on_dataset_changed);
	connect(m_wave_job_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &micro_harvest_widget::on_wave_job_changed);
	connect(m_symbol_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &micro_harvest_widget::on_symbol_changed);
	connect(m_run_btn, &QPushButton::clicked, this, &micro_harvest_widget::on_run_optimizer);
	connect(m_load_latest_btn, &QPushButton::clicked, this, &micro_harvest_widget::on_load_latest);
	connect(m_poll_timer, &QTimer::timeout, this, &micro_harvest_widget::poll_job_status);
	connect(m_islands_table, &QTableWidget::itemSelectionChanged, this, &micro_harvest_widget::on_island_selection_changed);
}

void micro_harvest_widget::set_base_url(const QString &base_url)
{
	m_base_url = base_url.trimmed();
	m_api.set_base_url(m_base_url);
}

void micro_harvest_widget::set_status(const QString &text)
{
	m_status_label->setText(text);
}

QString micro_harvest_widget::current_symbol() const
{
	return m_symbol_combo->currentText().trimmed();
}

QString micro_harvest_widget::current_timeframe() const
{
	return m_timeframe_combo->currentText().trimmed();
}

int micro_harvest_widget::current_dataset_id() const
{
	bool ok = false;
	int dataset_id = m_dataset_combo->currentData().toInt(&ok);

	if (ok && dataset_id > 0) {
		return dataset_id;
	}
	QString symbol = current_symbol();
	QString timeframe = current_timeframe();

	for (const stream_item &stream : m_streams) {
		if (stream.symbol == symbol && stream.timeframe == timeframe) {
			return stream.dataset_id;
		}
	}
	return 0;
}

const micro_harvest_widget::dataset_item *micro_harvest_widget::current_dataset() const
{
	int dataset_id = current_dataset_id();

	for (const dataset_item &dataset : m_datasets) {
		if (dataset.dataset_id == dataset_id) {
			return &dataset;
		}
	}
	return nullptr;
}

const micro_harvest_widget::wave_job_item *micro_harvest_widget::current_wave_job() const
{
	bool ok = false;
	qint64 job_id = m_wave_job_combo->currentData().toLongLong(&ok);

	if (!ok || job_id <= 0) {
		return nullptr;
	}
	for (const wave_job_item &job : m_wave_jobs) {
		if (job.job_id == job_id) {
			return &job;
		}
	}
	return nullptr;
}

void micro_harvest_widget::rebuild_dataset_combo()
{
	int previous_id = current_dataset_id();

	m_dataset_combo->blockSignals(true);
	m_dataset_combo->clear();
	for (const dataset_item &dataset : m_datasets) {
		QString wave_text = dataset.has_waves
			? QString("waves=%1/%2").arg(dataset.wave_model).arg(dataset.wave_token_count)
			: QString("waves=none");

		m_dataset_combo->addItem(QString("%1  id=%2  candles=%3  %4")
				.arg(dataset.name)
				.arg(dataset.dataset_id)
				.arg(dataset.candle_count)
				.arg(wave_text),
			dataset.dataset_id);
	}
	if (previous_id > 0) {
		int index = m_dataset_combo->findData(previous_id);

		if (index >= 0) {
			m_dataset_combo->setCurrentIndex(index);
		}
	}
	m_dataset_combo->blockSignals(false);
	on_dataset_changed();
}

void micro_harvest_widget::rebuild_wave_job_combo()
{
	const wave_job_item *current_job = current_wave_job();
	qint64 previous_job_id = current_job != nullptr ? current_job->job_id : 0;

	m_wave_job_combo->blockSignals(true);
	m_wave_job_combo->clear();
	m_wave_job_combo->addItem("Select wave job", 0);
	for (const wave_job_item &job : m_wave_jobs) {
		m_wave_job_combo->addItem(QString("job=%1  dataset=%2  %3")
				.arg(job.job_id)
				.arg(job.dataset_id)
				.arg(job_status_name(job.status)),
			job.job_id);
	}
	if (previous_job_id > 0) {
		int index = m_wave_job_combo->findData(previous_job_id);

		if (index >= 0) {
			m_wave_job_combo->setCurrentIndex(index);
		}
	}
	m_wave_job_combo->blockSignals(false);
}

void micro_harvest_widget::rebuild_symbol_combo()
{
	QString current = m_symbol_combo->currentText();
	QStringList seen;

	m_symbol_combo->blockSignals(true);
	m_symbol_combo->clear();
	for (const stream_item &stream : m_streams) {
		if (!seen.contains(stream.symbol)) {
			seen.push_back(stream.symbol);
		}
	}
	seen.sort();
	m_symbol_combo->addItems(seen);
	if (!current.isEmpty()) {
		int index = m_symbol_combo->findText(current);

		if (index >= 0) {
			m_symbol_combo->setCurrentIndex(index);
		}
	}
	m_symbol_combo->blockSignals(false);
	rebuild_timeframe_combo();
}

void micro_harvest_widget::rebuild_timeframe_combo()
{
	QString symbol = current_symbol();
	QString current = m_timeframe_combo->currentText();
	QStringList timeframes;

	m_timeframe_combo->blockSignals(true);
	m_timeframe_combo->clear();
	for (const stream_item &stream : m_streams) {
		if (stream.symbol == symbol && !timeframes.contains(stream.timeframe)) {
			timeframes.push_back(stream.timeframe);
		}
	}
	timeframes.sort();
	m_timeframe_combo->addItems(timeframes);
	if (!current.isEmpty()) {
		int index = m_timeframe_combo->findText(current);

		if (index >= 0) {
			m_timeframe_combo->setCurrentIndex(index);
		}
	}
	m_timeframe_combo->blockSignals(false);
}

void micro_harvest_widget::refresh_streams()
{
	QJsonDocument doc;
	QString err;
	QJsonArray streams;

	m_api.set_base_url(m_base_url);
	if (!m_api.get("/live/status", &doc, &err)) {
		QMessageBox::warning(this, "Micro Harvest", "Unable to load live streams: " + err);
		return;
	}
	m_streams.clear();
	streams = doc.object().value("streams").toArray();
	for (const QJsonValue &value : streams) {
		QJsonObject obj = value.toObject();
		stream_item item;

		item.symbol = obj.value("symbol").toString();
		item.timeframe = obj.value("timeframe").toString();
		item.dataset_id = obj.value("dataset_id").toInt();
		item.candle_count = obj.value("candle_count").toInt();
		if (!item.symbol.isEmpty() && !item.timeframe.isEmpty() && item.dataset_id > 0) {
			m_streams.push_back(item);
		}
	}
	rebuild_symbol_combo();
	set_status(QString("Micro harvest: loaded %1 live streams").arg(m_streams.size()));
}

void micro_harvest_widget::refresh_datasets()
{
	QJsonDocument doc;
	QString err;
	QJsonArray datasets;

	m_api.set_base_url(m_base_url);
	if (!m_api.get("/datasets", &doc, &err)) {
		QMessageBox::warning(this, "Micro Harvest", "Unable to load datasets: " + err);
		return;
	}
	m_datasets.clear();
	datasets = doc.object().value("datasets").toArray();
	for (const QJsonValue &value : datasets) {
		QJsonObject obj = value.toObject();
		dataset_item item;

		item.dataset_id = obj.value("dataset_id").toInt();
		item.name = obj.value("name").toString();
		item.candle_count = obj.value("candles").toInt();
		item.has_waves = obj.value("has_waves").toBool();
		item.wave_model = obj.value("wave_model").toString();
		item.wave_token_count = obj.value("wave_token_count").toInt();
		if (item.dataset_id > 0) {
			m_datasets.push_back(item);
		}
	}
	rebuild_dataset_combo();
	set_status(QString("Micro harvest: loaded %1 datasets").arg(m_datasets.size()));
}

void micro_harvest_widget::refresh_wave_jobs()
{
	QJsonDocument doc;
	QString err;
	QJsonArray jobs;

	m_api.set_base_url(m_base_url);
	if (!m_api.get("/jobs", &doc, &err)) {
		QMessageBox::warning(this, "Micro Harvest", "Unable to load jobs: " + err);
		return;
	}
	m_wave_jobs.clear();
	jobs = doc.object().value("jobs").toArray();
	for (const QJsonValue &value : jobs) {
		QJsonObject obj = value.toObject();
		wave_job_item item;

		if (obj.value("job_type").toInt() != job_type_wave_inference) {
			continue;
		}
		item.job_id = obj.value("job_id").toVariant().toLongLong();
		item.dataset_id = obj.value("dataset_id").toInt();
		item.status = obj.value("status").toInt();
		item.error = obj.value("error").toString();
		if (item.job_id > 0 && item.dataset_id > 0) {
			m_wave_jobs.push_back(item);
		}
	}
	rebuild_wave_job_combo();
	set_status(QString("Micro harvest: loaded %1 wave jobs").arg(m_wave_jobs.size()));
}

void micro_harvest_widget::on_symbol_changed()
{
	rebuild_timeframe_combo();
}

void micro_harvest_widget::on_dataset_changed()
{
	const dataset_item *dataset = current_dataset();
	const wave_job_item *selected_job = current_wave_job();

	if (dataset != nullptr && selected_job != nullptr && selected_job->dataset_id != dataset->dataset_id) {
		m_wave_job_combo->blockSignals(true);
		m_wave_job_combo->setCurrentIndex(0);
		m_wave_job_combo->blockSignals(false);
	}

	if (dataset == nullptr) {
		set_status("Micro harvest: select a dataset");
		return;
	}
	if (!dataset->has_waves) {
		set_status(QString("Micro harvest: dataset %1 has no wave inference yet").arg(dataset->dataset_id));
		return;
	}
	if (dataset->wave_model != "layered_waves") {
		set_status(QString("Micro harvest: dataset %1 waves=%2, layered_waves required")
			.arg(dataset->dataset_id)
			.arg(dataset->wave_model));
		return;
	}
	set_status(QString("Micro harvest: dataset %1 ready, waves=%2 tokens=%3")
		.arg(dataset->dataset_id)
		.arg(dataset->wave_model)
		.arg(dataset->wave_token_count));
}

void micro_harvest_widget::on_wave_job_changed()
{
	const wave_job_item *job = current_wave_job();

	if (job == nullptr) {
		on_dataset_changed();
		return;
	}
	if (job->status != 3) {
		set_status(QString("Micro harvest: wave job %1 is %2")
			.arg(job->job_id)
			.arg(job_status_name(job->status)));
		if (!job->error.isEmpty()) {
			set_status(QString("Micro harvest: wave job %1 %2: %3")
				.arg(job->job_id)
				.arg(job_status_name(job->status))
				.arg(job->error));
		}
	}
	{
		int dataset_index = m_dataset_combo->findData(job->dataset_id);

		if (dataset_index >= 0 && m_dataset_combo->currentIndex() != dataset_index) {
			m_dataset_combo->setCurrentIndex(dataset_index);
			return;
		}
	}
	on_dataset_changed();
}

void micro_harvest_widget::on_run_optimizer()
{
	QJsonDocument reply_doc;
	QJsonObject body;
	QString err;
	int dataset_id = current_dataset_id();
	const dataset_item *dataset = current_dataset();
	QJsonObject search;

	if (dataset_id <= 0) {
		QMessageBox::information(this, "Micro Harvest", "Select a dataset first.");
		return;
	}
	if (dataset == nullptr || !dataset->has_waves) {
		QMessageBox::information(this, "Micro Harvest", "Selected dataset has no wave inference yet.");
		return;
	}
	if (dataset->wave_model != "layered_waves") {
		QMessageBox::information(this,
			"Micro Harvest",
			QString("Selected dataset uses %1. Micro harvest requires layered_waves.")
				.arg(dataset->wave_model.isEmpty() ? "no wave model" : dataset->wave_model));
		return;
	}
	body.insert("dataset_id", dataset_id);
	body.insert("bias", m_bias_combo->currentText());
	body.insert("lookback_days", m_lookback_days_spin->value());
	body.insert("spread_pips", m_spread_pips_spin->value());
	search.insert("max_orders_min", m_max_orders_min_spin->value());
	search.insert("max_orders_max", m_max_orders_max_spin->value());
	search.insert("spacing_min_pips", m_spacing_min_spin->value());
	search.insert("spacing_max_pips", m_spacing_max_spin->value());
	search.insert("spacing_step_pips", m_spacing_step_spin->value());
	search.insert("take_profit_min_pips", m_take_profit_min_spin->value());
	search.insert("take_profit_max_pips", m_take_profit_max_spin->value());
	search.insert("take_profit_step_pips", m_take_profit_step_spin->value());
	search.insert("entry_edge_min", m_entry_edge_min_spin->value());
	search.insert("entry_edge_max", m_entry_edge_max_spin->value());
	search.insert("entry_edge_step", m_entry_edge_step_spin->value());
	search.insert("min_island_height_pips", m_min_height_spin->value());
	search.insert("max_island_height_pips", m_max_height_spin->value());
	for (auto it = search.begin(); it != search.end(); ++it) {
		body.insert(it.key(), it.value());
	}
	m_api.set_base_url(m_base_url);
	if (!m_api.post_json("/jobs/micro-harvest", QJsonDocument(body), &reply_doc, &err)) {
		QMessageBox::warning(this, "Micro Harvest", "Unable to start optimizer: " + err);
		return;
	}
	m_active_job_id = reply_doc.object().value("job_id").toInt();
	if (m_active_job_id <= 0) {
		QMessageBox::warning(this, "Micro Harvest", "Optimizer did not return a valid job id.");
		return;
	}
	m_poll_timer->start();
	set_status(QString("Micro harvest: job %1 queued").arg(m_active_job_id));
}

void micro_harvest_widget::poll_job_status()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = current_dataset_id();

	if (m_active_job_id <= 0) {
		m_poll_timer->stop();
		return;
	}
	m_api.set_base_url(m_base_url);
	if (!m_api.get(QString("/jobs/%1").arg(m_active_job_id), &doc, &err)) {
		set_status("Micro harvest: job poll failed: " + err);
		return;
	}
	{
		QJsonObject obj = doc.object();
		int status = obj.value("status").toInt();

		set_status(QString("Micro harvest: job %1 status=%2 progress=%3")
			.arg(m_active_job_id)
			.arg(status)
			.arg(obj.value("progress").toDouble(), 0, 'f', 3));
		if (status == 3) {
			m_poll_timer->stop();
			m_active_job_id = 0;
			if (dataset_id > 0) {
				on_load_latest();
			}
		} else if (status == 4 || status == 5) {
			m_poll_timer->stop();
			m_active_job_id = 0;
			QMessageBox::warning(this, "Micro Harvest", "Optimizer failed: " + obj.value("error").toString());
		}
	}
}

void micro_harvest_widget::on_load_latest()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = current_dataset_id();

	if (dataset_id <= 0) {
		QMessageBox::information(this, "Micro Harvest", "Select a dataset first.");
		return;
	}
	m_api.set_base_url(m_base_url);
	if (!m_api.get(QString("/datasets/%1/micro-harvest/latest").arg(dataset_id), &doc, &err)) {
		QMessageBox::warning(this, "Micro Harvest", "Unable to load latest result: " + err);
		return;
	}
	load_result_doc(doc);
}

void micro_harvest_widget::load_result_doc(const QJsonDocument &doc)
{
	QJsonObject obj = doc.object();
	QJsonObject params = obj.value("recommended_parameters").toObject();
	QJsonObject summary = obj.value("simulation_summary").toObject();
	QStringList param_keys = params.keys();
	QStringList summary_keys = summary.keys();
	int dataset_id = current_dataset_id();

	m_result_obj = obj;
	m_result_islands = obj.value("islands").toArray();
	m_result_trades = obj.value("trades").toArray();
	param_keys.sort();
	summary_keys.sort();

	m_params_table->setRowCount(param_keys.size());
	for (int i = 0; i < param_keys.size(); ++i) {
		m_params_table->setItem(i, 0, new QTableWidgetItem(param_keys.at(i)));
		m_params_table->setItem(i, 1, new QTableWidgetItem(json_value_brief(params.value(param_keys.at(i)))));
	}
	m_summary_table->setRowCount(summary_keys.size());
	for (int i = 0; i < summary_keys.size(); ++i) {
		m_summary_table->setItem(i, 0, new QTableWidgetItem(summary_keys.at(i)));
		m_summary_table->setItem(i, 1, new QTableWidgetItem(json_value_brief(summary.value(summary_keys.at(i)))));
	}
	m_islands_table->setRowCount(m_result_islands.size());
	for (int i = 0; i < m_result_islands.size(); ++i) {
		QJsonObject island = m_result_islands.at(i).toObject();

		m_islands_table->setItem(i, 0, new QTableWidgetItem(QString::number(island.value("index").toInt())));
		m_islands_table->setItem(i, 1, new QTableWidgetItem(island.value("selected").toBool() ? "yes" : "no"));
		m_islands_table->setItem(i, 2, new QTableWidgetItem(QString::number(island.value("start_candle_index").toInt())));
		m_islands_table->setItem(i, 3, new QTableWidgetItem(QString::number(island.value("end_candle_index").toInt())));
		m_islands_table->setItem(i, 4, new QTableWidgetItem(QString::number(island.value("height_pips").toInt())));
		m_islands_table->setItem(i, 5, new QTableWidgetItem(QString::number(island.value("profit_pips").toInt())));
		m_islands_table->setItem(i, 6, new QTableWidgetItem(QString::number(island.value("closed_trade_count").toInt())));
		m_islands_table->setItem(i, 7, new QTableWidgetItem(QString::number(island.value("ignored_open_trade_count").toInt())));
	}
	m_trades_table->setRowCount(m_result_trades.size());
	for (int i = 0; i < m_result_trades.size(); ++i) {
		QJsonObject trade = m_result_trades.at(i).toObject();

		m_trades_table->setItem(i, 0, new QTableWidgetItem(QString::number(trade.value("island_index").toInt())));
		m_trades_table->setItem(i, 1, new QTableWidgetItem(trade.value("direction").toString()));
		m_trades_table->setItem(i, 2, new QTableWidgetItem(QString::number(trade.value("open_candle_index").toInt())));
		m_trades_table->setItem(i, 3, new QTableWidgetItem(QString::number(trade.value("close_candle_index").toInt())));
		m_trades_table->setItem(i, 4, new QTableWidgetItem(QString::number(trade.value("open_price_pips").toInt())));
		m_trades_table->setItem(i, 5, new QTableWidgetItem(QString::number(trade.value("close_price_pips").toInt())));
		m_trades_table->setItem(i, 6, new QTableWidgetItem(QString::number(trade.value("take_profit_pips").toInt())));
		m_trades_table->setItem(i, 7, new QTableWidgetItem(QString::number(trade.value("profit_pips").toInt())));
		m_trades_table->setItem(i, 8, new QTableWidgetItem(trade.value("closed").toBool() ? "yes" : "no"));
	}
	m_params_table->resizeRowsToContents();
	m_params_table->resizeColumnsToContents();
	m_summary_table->resizeRowsToContents();
	m_summary_table->resizeColumnsToContents();
	m_islands_table->resizeRowsToContents();
	m_islands_table->resizeColumnsToContents();
	m_trades_table->resizeRowsToContents();
	m_trades_table->resizeColumnsToContents();
	load_chart_for_result();
	set_status(QString("Micro harvest: loaded %1 result for dataset %2")
		.arg(obj.value("replay_mode").toBool() ? "replay" : "static")
		.arg(dataset_id));
}

void micro_harvest_widget::load_chart_for_result()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = current_dataset_id();
	int min_start = -1;
	int max_end = -1;
	QVector<cluster_chart_widget::candle_item> candles;
	QVector<cluster_chart_widget::micro_harvest_island_overlay_item> islands;
	QVector<cluster_chart_widget::micro_harvest_trade_overlay_item> trades;

	if (dataset_id <= 0 || m_result_islands.isEmpty()) {
		m_chart->clear();
		return;
	}
	for (const QJsonValue &value : m_result_islands) {
		QJsonObject island = value.toObject();
		int start = island.value("start_candle_index").toInt();
		int end = island.value("end_candle_index").toInt();

		if (min_start < 0 || start < min_start) {
			min_start = start;
		}
		if (max_end < 0 || end > max_end) {
			max_end = end;
		}
	}
	min_start = qMax(0, min_start - 20);
	max_end += 20;
	m_chart_fetch_start = min_start;
	m_api.set_base_url(m_base_url);
	if (!m_api.get(QString("/datasets/%1/candles?start=%2&count=%3").arg(dataset_id).arg(min_start).arg(max_end - min_start + 1), &doc, &err)) {
		set_status("Micro harvest chart load failed: " + err);
		return;
	}
	{
		QJsonArray rows = doc.object().value("candles").toArray();

		for (int i = 0; i < rows.size(); ++i) {
			QJsonObject o = rows.at(i).toObject();
			cluster_chart_widget::candle_item item;

			item.candle_index = min_start + i;
			item.ts_epoch = o.value("ts_epoch").toInt();
			item.open_pips = o.value("open_pips").toInt();
			item.high_pips = o.value("high_pips").toInt();
			item.low_pips = o.value("low_pips").toInt();
			item.close_pips = o.value("close_pips").toInt();
			candles.push_back(item);
		}
	}
	for (const QJsonValue &value : m_result_islands) {
		QJsonObject island = value.toObject();
		cluster_chart_widget::micro_harvest_island_overlay_item item;

		item.start_index = island.value("start_candle_index").toInt() - min_start;
		item.end_index = island.value("end_candle_index").toInt() - min_start;
		item.low_pips = island.value("low_price_pips").toInt();
		item.high_pips = island.value("high_price_pips").toInt();
		item.selected = island.value("selected").toBool();
		islands.push_back(item);
	}
	for (const QJsonValue &value : m_result_trades) {
		QJsonObject trade = value.toObject();
		cluster_chart_widget::micro_harvest_trade_overlay_item item;

		item.open_index = trade.value("open_candle_index").toInt() - min_start;
		item.close_index = trade.value("close_candle_index").toInt() - min_start;
		item.open_price_pips = trade.value("open_price_pips").toInt();
		item.close_price_pips = trade.value("close_price_pips").toInt();
		item.direction = (trade.value("direction").toString() == "SELL") ? -1 : 1;
		item.closed = trade.value("closed").toBool();
		trades.push_back(item);
	}
	m_chart->set_candles(candles, -1, -1);
	m_chart->set_focus_range(-1, -1);
	m_chart->set_wave_overlay(QVector<cluster_chart_widget::wave_overlay_item>());
	m_chart->set_micro_harvest_overlays(islands, trades);
}

void micro_harvest_widget::on_island_selection_changed()
{
	int row = m_islands_table->currentRow();
	QJsonObject island;

	if (row < 0 || row >= m_result_islands.size()) {
		m_chart->set_focus_range(-1, -1);
		return;
	}
	island = m_result_islands.at(row).toObject();
	m_chart->set_focus_range(island.value("start_candle_index").toInt() - m_chart_fetch_start,
		island.value("end_candle_index").toInt() - m_chart_fetch_start);
}
