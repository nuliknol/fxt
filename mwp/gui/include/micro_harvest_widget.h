#ifndef MICRO_HARVEST_WIDGET_H
#define MICRO_HARVEST_WIDGET_H

#include "api_client.h"
#include "cluster_chart_widget.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class micro_harvest_widget : public QWidget
{
	Q_OBJECT

public:
	explicit micro_harvest_widget(QWidget *parent = nullptr);

	void set_base_url(const QString &base_url);

private slots:
	void refresh_streams();
	void refresh_datasets();
	void refresh_wave_jobs();
	void on_symbol_changed();
	void on_dataset_changed();
	void on_wave_job_changed();
	void on_run_optimizer();
	void on_load_latest();
	void poll_job_status();
	void on_island_selection_changed();

private:
	struct stream_item {
		QString symbol;
		QString timeframe;
		int dataset_id;
		int candle_count;
	};

	struct dataset_item {
		int dataset_id;
		QString name;
		int candle_count;
		bool has_waves;
		QString wave_model;
		int wave_token_count;
	};

	struct wave_job_item {
		qint64 job_id;
		int dataset_id;
		int status;
		QString error;
	};

	void rebuild_symbol_combo();
	void rebuild_timeframe_combo();
	void rebuild_dataset_combo();
	void rebuild_wave_job_combo();
	int current_dataset_id() const;
	const dataset_item *current_dataset() const;
	const wave_job_item *current_wave_job() const;
	QString current_symbol() const;
	QString current_timeframe() const;
	void set_status(const QString &text);
	void load_result_doc(const QJsonDocument &doc);
	void load_chart_for_result();

	api_client m_api;
	QString m_base_url;
	QVector<stream_item> m_streams;
	QVector<dataset_item> m_datasets;
	QVector<wave_job_item> m_wave_jobs;
	QJsonObject m_result_obj;
	QJsonArray m_result_islands;
	QJsonArray m_result_trades;
	QComboBox *m_dataset_combo;
	QComboBox *m_wave_job_combo;
	QComboBox *m_symbol_combo;
	QComboBox *m_timeframe_combo;
	QComboBox *m_bias_combo;
	QSpinBox *m_lookback_days_spin;
	QSpinBox *m_spread_pips_spin;
	QSpinBox *m_max_orders_min_spin;
	QSpinBox *m_max_orders_max_spin;
	QSpinBox *m_spacing_min_spin;
	QSpinBox *m_spacing_max_spin;
	QSpinBox *m_spacing_step_spin;
	QSpinBox *m_take_profit_min_spin;
	QSpinBox *m_take_profit_max_spin;
	QSpinBox *m_take_profit_step_spin;
	QSpinBox *m_entry_edge_min_spin;
	QSpinBox *m_entry_edge_max_spin;
	QSpinBox *m_entry_edge_step_spin;
	QSpinBox *m_min_height_spin;
	QSpinBox *m_max_height_spin;
	QPushButton *m_refresh_datasets_btn;
	QPushButton *m_refresh_wave_jobs_btn;
	QPushButton *m_refresh_streams_btn;
	QPushButton *m_run_btn;
	QPushButton *m_load_latest_btn;
	QLabel *m_status_label;
	QTableWidget *m_params_table;
	QTableWidget *m_summary_table;
	QTableWidget *m_islands_table;
	QTableWidget *m_trades_table;
	cluster_chart_widget *m_chart;
	QTimer *m_poll_timer;
	int m_active_job_id;
	int m_chart_fetch_start;
};

#endif
