#ifndef PRICECHARTWIDGET_H
#define PRICECHARTWIDGET_H

#include <QWidget>
#include <QVector>

#include "MarketSimulator.h"

class PriceChartWidget : public QWidget
{
	Q_OBJECT

public:
	explicit PriceChartWidget(QWidget *parent = nullptr);

	void set_history(const QVector<double> &price_history, const QVector<double> &liquidity_history, const QVector<int> &state_history);
	void set_metrics(const SimulationMetrics &metrics);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QVector<double> prices_;
	QVector<double> liquidities_;
	QVector<int> states_;
	SimulationMetrics metrics_;
};

#endif
