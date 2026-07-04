#include "MainWindow.h"

#include "MetricsPanelWidget.h"
#include "ParameterPanelWidget.h"
#include "PriceChartWidget.h"
#include "SimulationControlWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QStringList>
#include <QMessageBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, simulator_(new MarketSimulator(this))
	, chart_widget_(new PriceChartWidget)
	, parameter_panel_(new ParameterPanelWidget)
	, metrics_panel_(new MetricsPanelWidget)
	, control_widget_(new SimulationControlWidget)
	, timer_(new QTimer(this))
{
	setWindowTitle(QStringLiteral("BubbleSimulator"));
	resize(1400, 840);

	QWidget *central = new QWidget;
	setCentralWidget(central);

	QSplitter *splitter = new QSplitter(Qt::Horizontal);

	QWidget *right_panel = new QWidget;
	QVBoxLayout *right_layout = new QVBoxLayout(right_panel);
	right_layout->addWidget(parameter_panel_, 4);
	right_layout->addWidget(metrics_panel_, 1);
	right_layout->addWidget(control_widget_);

	splitter->addWidget(chart_widget_);
	splitter->addWidget(right_panel);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);

	QVBoxLayout *central_layout = new QVBoxLayout(central);
	central_layout->addWidget(splitter);

	timer_->setInterval(control_widget_->timer_interval_ms());

	connect(timer_, &QTimer::timeout, simulator_, &MarketSimulator::step);
	connect(control_widget_, &SimulationControlWidget::start_requested, this, &MainWindow::on_start_requested);
	connect(control_widget_, &SimulationControlWidget::pause_requested, this, &MainWindow::on_pause_requested);
	connect(control_widget_, &SimulationControlWidget::copy_params_requested, this, &MainWindow::on_copy_params_requested);
	connect(control_widget_, &SimulationControlWidget::reset_requested, this, &MainWindow::on_reset_requested);
	connect(control_widget_, &SimulationControlWidget::step_requested, this, &MainWindow::on_step_requested);
	connect(control_widget_, &SimulationControlWidget::run_100_requested, this, &MainWindow::on_run_100_requested);
	connect(control_widget_, &SimulationControlWidget::interval_changed, timer_, qOverload<int>(&QTimer::setInterval));
	connect(simulator_, &MarketSimulator::simulation_updated, this, &MainWindow::refresh_view);
	connect(simulator_, &MarketSimulator::simulation_finished, this, &MainWindow::on_pause_requested);

	parameter_panel_->set_params(simulator_->params());
	refresh_view();
}

void MainWindow::on_start_requested()
{
	if (!apply_params_from_panel(false)) {
		return;
	}
	timer_->start();
}

void MainWindow::on_pause_requested()
{
	timer_->stop();
}

void MainWindow::on_copy_params_requested()
{
	const SimulationParams candidate = parameter_panel_->params();
	QApplication::clipboard()->setText(format_params_for_clipboard(candidate));
}

void MainWindow::on_reset_requested()
{
	timer_->stop();
	apply_params_from_panel(true);
}

void MainWindow::on_step_requested()
{
	timer_->stop();
	if (!apply_params_from_panel(false)) {
		return;
	}
	simulator_->step();
}

void MainWindow::on_run_100_requested()
{
	timer_->stop();
	if (!apply_params_from_panel(false)) {
		return;
	}
	simulator_->run_steps(100);
}

void MainWindow::refresh_view()
{
	chart_widget_->set_history(simulator_->price_history(), simulator_->liquidity_history(), simulator_->state_history());
	chart_widget_->set_metrics(simulator_->metrics());
	metrics_panel_->set_metrics(simulator_->metrics());
}

bool MainWindow::apply_params_from_panel(bool reset_after_apply)
{
	const SimulationParams candidate = parameter_panel_->params();
	const QString validation_error = simulator_->validate_params(candidate);
	if (!validation_error.isEmpty()) {
		QMessageBox::warning(this, QStringLiteral("Invalid Parameters"), validation_error);
		return false;
	}

	if (candidate.max_ticks <= simulator_->metrics().tick) {
		QMessageBox::warning(this, QStringLiteral("Invalid Parameters"),
			QStringLiteral("Max ticks must exceed the current simulation tick, or reset the simulation."));
		return false;
	}

	simulator_->set_params(candidate);
	if (reset_after_apply) {
		simulator_->reset();
	}
	return true;
}

QString MainWindow::format_params_for_clipboard(const SimulationParams &params) const
{
	const auto bool_text = [](bool value) {
		return value ? QStringLiteral("true") : QStringLiteral("false");
	};

	QStringList lines;
	lines
		<< QStringLiteral("participant_count = %1").arg(params.participant_count)
		<< QStringLiteral("asset_symbol = \"%1\"").arg(params.asset_symbol)
		<< QStringLiteral("asset_name = \"%1\"").arg(params.asset_name)
		<< QStringLiteral("initial_price = %1").arg(params.initial_price, 0, 'f', 4)
		<< QStringLiteral("min_initial_balance = %1").arg(params.min_initial_balance, 0, 'f', 2)
		<< QStringLiteral("max_initial_balance = %1").arg(params.max_initial_balance, 0, 'f', 2)
		<< QStringLiteral("trend_population_pct = %1").arg(params.trend_population_pct, 0, 'f', 4)
		<< QStringLiteral("contrarian_population_pct = %1").arg(params.contrarian_population_pct, 0, 'f', 4)
		<< QStringLiteral("neutral_population_pct = %1").arg(params.neutral_population_pct, 0, 'f', 4)
		<< QStringLiteral("leverage_enabled = %1").arg(bool_text(params.leverage_enabled))
		<< QStringLiteral("average_leverage = %1").arg(params.average_leverage, 0, 'f', 4)
		<< QStringLiteral("max_leverage = %1").arg(params.max_leverage, 0, 'f', 4)
		<< QStringLiteral("maintenance_margin_rate = %1").arg(params.maintenance_margin_rate, 0, 'f', 4)
		<< QStringLiteral("base_liquidity = %1").arg(params.base_liquidity, 0, 'f', 2)
		<< QStringLiteral("liquidity_collapse_multiplier = %1").arg(params.liquidity_collapse_multiplier, 0, 'f', 4)
		<< QStringLiteral("liquidity_sensitivity_to_volatility = %1").arg(params.liquidity_sensitivity_to_volatility, 0, 'f', 4)
		<< QStringLiteral("liquidity_sensitivity_to_order_imbalance = %1").arg(params.liquidity_sensitivity_to_order_imbalance, 0, 'f', 4)
		<< QStringLiteral("price_impact_coefficient = %1").arg(params.price_impact_coefficient, 0, 'f', 4)
		<< QStringLiteral("random_noise_level = %1").arg(params.random_noise_level, 0, 'f', 4)
		<< QStringLiteral("max_random_drop_pct = %1").arg(params.max_random_drop_pct, 0, 'f', 4)
		<< QStringLiteral("momentum_window = %1").arg(params.momentum_window)
		<< QStringLiteral("momentum_response_strength = %1").arg(params.momentum_response_strength, 0, 'f', 4)
		<< QStringLiteral("pnl_feedback_strength = %1").arg(params.pnl_feedback_strength, 0, 'f', 4)
		<< QStringLiteral("herding_strength = %1").arg(params.herding_strength, 0, 'f', 4)
		<< QStringLiteral("drawdown_fear_strength = %1").arg(params.drawdown_fear_strength, 0, 'f', 4)
		<< QStringLiteral("neutral_to_trend_conversion_rate = %1").arg(params.neutral_to_trend_conversion_rate, 0, 'f', 4)
		<< QStringLiteral("trend_to_panic_conversion_rate = %1").arg(params.trend_to_panic_conversion_rate, 0, 'f', 4)
		<< QStringLiteral("contrarian_to_trend_conversion_rate = %1").arg(params.contrarian_to_trend_conversion_rate, 0, 'f', 4)
		<< QStringLiteral("external_shock_probability = %1").arg(params.external_shock_probability, 0, 'f', 4)
		<< QStringLiteral("external_shock_min_pct = %1").arg(params.external_shock_min_pct, 0, 'f', 4)
		<< QStringLiteral("external_shock_max_pct = %1").arg(params.external_shock_max_pct, 0, 'f', 4)
		<< QStringLiteral("buyer_exhaustion_threshold = %1").arg(params.buyer_exhaustion_threshold, 0, 'f', 4)
		<< QStringLiteral("panic_drawdown_threshold = %1").arg(params.panic_drawdown_threshold, 0, 'f', 4)
		<< QStringLiteral("forced_selling_cascade_threshold = %1").arg(params.forced_selling_cascade_threshold, 0, 'f', 4)
		<< QStringLiteral("profit_taking_threshold = %1").arg(params.profit_taking_threshold, 0, 'f', 4)
		<< QStringLiteral("smart_money_population_pct = %1").arg(params.smart_money_population_pct, 0, 'f', 4)
		<< QStringLiteral("smart_money_distribution_rate = %1").arg(params.smart_money_distribution_rate, 0, 'f', 4)
		<< QStringLiteral("max_ticks = %1").arg(params.max_ticks)
		<< QStringLiteral("random_seed = %1").arg(params.random_seed);

	return lines.join(QLatin1Char('\n'));
}
