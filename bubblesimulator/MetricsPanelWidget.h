#ifndef METRICSPANELWIDGET_H
#define METRICSPANELWIDGET_H

#include <QWidget>

#include "MarketSimulator.h"

class QLabel;

class MetricsPanelWidget : public QWidget
{
	Q_OBJECT

public:
	explicit MetricsPanelWidget(QWidget *parent = nullptr);

	void set_metrics(const SimulationMetrics &metrics);

private:
	QLabel *summary_label_;
	QLabel *flow_label_;
	QLabel *exposure_label_;
	QLabel *counts_label_;
};

#endif
