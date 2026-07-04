#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "MarketSimulator.h"

class MetricsPanelWidget;
class ParameterPanelWidget;
class PriceChartWidget;
class QTimer;
class SimulationControlWidget;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);

private slots:
	void on_start_requested();
	void on_pause_requested();
	void on_copy_params_requested();
	void on_reset_requested();
	void on_step_requested();
	void on_run_100_requested();
	void refresh_view();

private:
	bool apply_params_from_panel(bool reset_after_apply);
	QString format_params_for_clipboard(const SimulationParams &params) const;

	MarketSimulator *simulator_;
	PriceChartWidget *chart_widget_;
	ParameterPanelWidget *parameter_panel_;
	MetricsPanelWidget *metrics_panel_;
	SimulationControlWidget *control_widget_;
	QTimer *timer_;
};

#endif
