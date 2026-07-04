#include "PriceChartWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

PriceChartWidget::PriceChartWidget(QWidget *parent)
	: QWidget(parent)
{
	setMinimumSize(640, 420);
	setAutoFillBackground(true);
}

void PriceChartWidget::set_history(const QVector<double> &price_history, const QVector<double> &liquidity_history, const QVector<int> &state_history)
{
	prices_ = price_history;
	liquidities_ = liquidity_history;
	states_ = state_history;
	update();
}

void PriceChartWidget::set_metrics(const SimulationMetrics &metrics)
{
	metrics_ = metrics;
	update();
}

void PriceChartWidget::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event)

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), QColor(18, 22, 28));

	const QRect chart_rect = rect().adjusted(52, 24, -24, -48);
	painter.setPen(QColor(80, 88, 100));
	painter.drawRect(chart_rect);

	if (prices_.isEmpty()) {
		painter.setPen(Qt::white);
		painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No simulation data yet"));
		return;
	}

	double min_price = prices_.first();
	double max_price = prices_.first();
	for (double price : prices_) {
		min_price = qMin(min_price, price);
		max_price = qMax(max_price, price);
	}
	if (qFuzzyCompare(min_price, max_price)) {
		min_price *= 0.95;
		max_price *= 1.05;
	}

	const auto to_point = [&](int index, double value) {
		const double x_ratio = prices_.size() > 1 ? static_cast<double>(index) / (prices_.size() - 1) : 0.0;
		const double y_ratio = (value - min_price) / qMax(max_price - min_price, 0.000001);
		const double x = chart_rect.left() + x_ratio * chart_rect.width();
		const double y = chart_rect.bottom() - y_ratio * chart_rect.height();
		return QPointF(x, y);
	};

	for (int i = 1; i < states_.size(); ++i) {
		if (states_.at(i) != states_.at(i - 1)) {
			const QPointF marker = to_point(i, min_price);
			painter.setPen(QPen(QColor(255, 180, 80, 120), 1, Qt::DashLine));
			painter.drawLine(QPointF(marker.x(), chart_rect.top()), QPointF(marker.x(), chart_rect.bottom()));
		}
	}

	if (metrics_.state == STATE_COLLAPSE || metrics_.state == STATE_CAPITULATION) {
		painter.fillRect(chart_rect, QColor(120, 20, 20, 28));
	}

	QPainterPath path;
	path.moveTo(to_point(0, prices_.first()));
	for (int i = 1; i < prices_.size(); ++i) {
		path.lineTo(to_point(i, prices_.at(i)));
	}

	painter.setPen(QPen(QColor(90, 220, 160), 2.2));
	painter.drawPath(path);

	int peak_index = 0;
	double peak_value = prices_.first();
	for (int i = 1; i < prices_.size(); ++i) {
		if (prices_.at(i) > peak_value) {
			peak_value = prices_.at(i);
			peak_index = i;
		}
	}

	const QPointF peak_point = to_point(peak_index, peak_value);
	painter.setPen(QPen(QColor(255, 210, 90), 1.5));
	painter.setBrush(QColor(255, 210, 90));
	painter.drawEllipse(peak_point, 4.0, 4.0);
	painter.drawText(peak_point + QPointF(8.0, -8.0), QStringLiteral("Peak %1").arg(peak_value, 0, 'f', 2));

	painter.setPen(QColor(220, 228, 235));
	painter.drawText(QRect(chart_rect.left(), 4, chart_rect.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
		QStringLiteral("%1 (%2)").arg(metrics_.price, 0, 'f', 4).arg(market_state_to_string(metrics_.state)));
	painter.drawText(QRect(chart_rect.left(), chart_rect.bottom() + 12, chart_rect.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
		QStringLiteral("Ticks: %1    Liquidity: %2    Drawdown: %3%")
			.arg(metrics_.tick)
			.arg(metrics_.liquidity, 0, 'f', 0)
			.arg(metrics_.drawdown * 100.0, 0, 'f', 2));

	painter.drawText(QRect(6, chart_rect.top() - 8, 40, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(max_price, 'f', 2));
	painter.drawText(QRect(6, chart_rect.bottom() - 8, 40, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(min_price, 'f', 2));
}
