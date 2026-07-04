#include "MetricsPanelWidget.h"

#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

MetricsPanelWidget::MetricsPanelWidget(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	summary_label_ = new QLabel;
	flow_label_ = new QLabel;
	exposure_label_ = new QLabel;
	counts_label_ = new QLabel;

	for (QLabel *label : {summary_label_, flow_label_, exposure_label_, counts_label_}) {
		label->setWordWrap(true);
		label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		layout->addWidget(label);
	}
	layout->addStretch(1);
}

void MetricsPanelWidget::set_metrics(const SimulationMetrics &metrics)
{
	const QLocale locale = QLocale::system();

	summary_label_->setText(
		QStringLiteral("Tick %1 | Price %2 | Peak %3 | Drawdown %4% | Liquidity %5 | State %6")
			.arg(metrics.tick)
			.arg(metrics.price, 0, 'f', 4)
			.arg(metrics.peak_price, 0, 'f', 4)
			.arg(metrics.drawdown * 100.0, 0, 'f', 2)
			.arg(locale.toString(qRound64(metrics.liquidity)))
			.arg(market_state_to_string(metrics.state)));

	flow_label_->setText(
		QStringLiteral("Organic buy %1 | Organic sell %2 | Forced buy %3 | Forced sell %4")
			.arg(metrics.organic_buy, 0, 'f', 0)
			.arg(metrics.organic_sell, 0, 'f', 0)
			.arg(metrics.forced_buy, 0, 'f', 0)
			.arg(metrics.forced_sell, 0, 'f', 0));

	exposure_label_->setText(
		QStringLiteral("Trend cash ratio %1 | Long exposure %2 | Short exposure %3 | Avg leverage %4")
			.arg(metrics.trend_cash_remaining_ratio, 0, 'f', 3)
			.arg(metrics.total_long_exposure, 0, 'f', 0)
			.arg(metrics.total_short_exposure, 0, 'f', 0)
			.arg(metrics.total_leverage, 0, 'f', 2));

	counts_label_->setText(
		QStringLiteral("Margin calls %1 | Bankrupt %2 | Trend %3 | Contrarian %4 | Neutral %5 | Panic %6")
			.arg(metrics.margin_call_count)
			.arg(metrics.bankrupt_count)
			.arg(metrics.trend_count)
			.arg(metrics.contrarian_count)
			.arg(metrics.neutral_count)
			.arg(metrics.panic_count));
}
