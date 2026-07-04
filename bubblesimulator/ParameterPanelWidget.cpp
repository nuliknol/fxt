#include "ParameterPanelWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
QDoubleSpinBox *make_double_spin(double min, double max, double step, int decimals)
{
	QDoubleSpinBox *spin = new QDoubleSpinBox;
	spin->setRange(min, max);
	spin->setDecimals(decimals);
	spin->setSingleStep(step);
	return spin;
}

QSpinBox *make_int_spin(int min, int max, int step)
{
	QSpinBox *spin = new QSpinBox;
	spin->setRange(min, max);
	spin->setSingleStep(step);
	return spin;
}
}

ParameterPanelWidget::ParameterPanelWidget(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);
	outer_layout->setContentsMargins(0, 0, 0, 0);

	QScrollArea *scroll_area = new QScrollArea;
	scroll_area->setWidgetResizable(true);
	outer_layout->addWidget(scroll_area);

	QWidget *content = new QWidget;
	scroll_area->setWidget(content);

	QVBoxLayout *content_layout = new QVBoxLayout(content);

	asset_symbol_edit_ = new QLineEdit;
	asset_name_edit_ = new QLineEdit;
	participant_count_spin_ = make_int_spin(1, 500000, 100);
	initial_price_spin_ = make_double_spin(0.0001, 1000000.0, 1.0, 4);
	min_initial_balance_spin_ = make_double_spin(1.0, 1000000000.0, 100.0, 2);
	max_initial_balance_spin_ = make_double_spin(1.0, 1000000000.0, 100.0, 2);
	trend_population_spin_ = make_double_spin(0.0, 100.0, 1.0, 4);
	contrarian_population_spin_ = make_double_spin(0.0, 100.0, 1.0, 4);
	neutral_population_spin_ = make_double_spin(0.0, 100.0, 1.0, 4);
	leverage_enabled_check_ = new QCheckBox(QStringLiteral("Enable leverage"));
	average_leverage_spin_ = make_double_spin(1.0, 100.0, 0.1, 2);
	max_leverage_spin_ = make_double_spin(1.0, 100.0, 0.1, 2);
	maintenance_margin_spin_ = make_double_spin(0.01, 0.99, 0.01, 4);
	base_liquidity_spin_ = make_double_spin(1.0, 1000000000000.0, 100000.0, 2);
	liquidity_collapse_multiplier_spin_ = make_double_spin(0.01, 1.0, 0.01, 4);
	liquidity_sensitivity_to_volatility_spin_ = make_double_spin(0.0, 5.0, 0.05, 4);
	liquidity_sensitivity_to_order_imbalance_spin_ = make_double_spin(0.0, 5.0, 0.05, 4);
	price_impact_spin_ = make_double_spin(0.0001, 10.0, 0.001, 4);
	random_noise_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	max_random_drop_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	momentum_window_spin_ = make_int_spin(1, 1000, 1);
	momentum_response_strength_spin_ = make_double_spin(0.0, 20.0, 0.1, 4);
	pnl_feedback_strength_spin_ = make_double_spin(0.0, 20.0, 0.1, 4);
	herding_strength_spin_ = make_double_spin(0.0, 20.0, 0.1, 4);
	drawdown_fear_strength_spin_ = make_double_spin(0.0, 20.0, 0.1, 4);
	neutral_to_trend_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	trend_to_panic_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	contrarian_to_trend_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	external_shock_probability_spin_ = make_double_spin(0.0, 1.0, 0.0001, 4);
	external_shock_min_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	external_shock_max_spin_ = make_double_spin(0.0, 1.0, 0.001, 4);
	buyer_exhaustion_spin_ = make_double_spin(0.0, 1.0, 0.01, 4);
	panic_drawdown_spin_ = make_double_spin(0.0, 1.0, 0.01, 4);
	forced_selling_cascade_spin_ = make_double_spin(0.1, 10.0, 0.01, 4);
	profit_taking_threshold_spin_ = make_double_spin(0.0, 10.0, 0.05, 4);
	smart_money_population_spin_ = make_double_spin(0.0, 50.0, 0.1, 4);
	smart_money_distribution_spin_ = make_double_spin(0.0, 1.0, 0.01, 4);
	max_ticks_spin_ = make_int_spin(1, 1000000, 100);
	random_seed_spin_ = make_int_spin(0, 2147483647, 1);

	QGroupBox *identity_group = new QGroupBox(QStringLiteral("Asset"));
	QFormLayout *identity_form = new QFormLayout(identity_group);
	identity_form->addRow(QStringLiteral("Symbol"), asset_symbol_edit_);
	identity_form->addRow(QStringLiteral("Name"), asset_name_edit_);
	identity_form->addRow(QStringLiteral("Participants"), participant_count_spin_);
	identity_form->addRow(QStringLiteral("Initial price"), initial_price_spin_);

	QGroupBox *capital_group = new QGroupBox(QStringLiteral("Capital"));
	QFormLayout *capital_form = new QFormLayout(capital_group);
	capital_form->addRow(QStringLiteral("Min balance"), min_initial_balance_spin_);
	capital_form->addRow(QStringLiteral("Max balance"), max_initial_balance_spin_);
	capital_form->addRow(QStringLiteral("Trend %"), trend_population_spin_);
	capital_form->addRow(QStringLiteral("Contrarian %"), contrarian_population_spin_);
	capital_form->addRow(QStringLiteral("Neutral %"), neutral_population_spin_);
	capital_form->addRow(QString(), leverage_enabled_check_);
	capital_form->addRow(QStringLiteral("Average leverage"), average_leverage_spin_);
	capital_form->addRow(QStringLiteral("Max leverage"), max_leverage_spin_);
	capital_form->addRow(QStringLiteral("Maintenance margin"), maintenance_margin_spin_);

	QGroupBox *liquidity_group = new QGroupBox(QStringLiteral("Liquidity"));
	QFormLayout *liquidity_form = new QFormLayout(liquidity_group);
	liquidity_form->addRow(QStringLiteral("Base liquidity"), base_liquidity_spin_);
	liquidity_form->addRow(QStringLiteral("Collapse multiplier"), liquidity_collapse_multiplier_spin_);
	liquidity_form->addRow(QStringLiteral("Volatility sensitivity"), liquidity_sensitivity_to_volatility_spin_);
	liquidity_form->addRow(QStringLiteral("Imbalance sensitivity"), liquidity_sensitivity_to_order_imbalance_spin_);
	liquidity_form->addRow(QStringLiteral("Price impact"), price_impact_spin_);
	liquidity_form->addRow(QStringLiteral("Noise level"), random_noise_spin_);
	liquidity_form->addRow(QStringLiteral("Max random drop"), max_random_drop_spin_);

	QGroupBox *behavior_group = new QGroupBox(QStringLiteral("Behavior"));
	QFormLayout *behavior_form = new QFormLayout(behavior_group);
	behavior_form->addRow(QStringLiteral("Momentum window"), momentum_window_spin_);
	behavior_form->addRow(QStringLiteral("Momentum strength"), momentum_response_strength_spin_);
	behavior_form->addRow(QStringLiteral("PnL feedback"), pnl_feedback_strength_spin_);
	behavior_form->addRow(QStringLiteral("Herding strength"), herding_strength_spin_);
	behavior_form->addRow(QStringLiteral("Drawdown fear"), drawdown_fear_strength_spin_);
	behavior_form->addRow(QStringLiteral("Neutral -> trend"), neutral_to_trend_spin_);
	behavior_form->addRow(QStringLiteral("Trend -> panic"), trend_to_panic_spin_);
	behavior_form->addRow(QStringLiteral("Contrarian -> trend"), contrarian_to_trend_spin_);

	QGroupBox *events_group = new QGroupBox(QStringLiteral("Events"));
	QFormLayout *events_form = new QFormLayout(events_group);
	events_form->addRow(QStringLiteral("Shock probability"), external_shock_probability_spin_);
	events_form->addRow(QStringLiteral("Shock min pct"), external_shock_min_spin_);
	events_form->addRow(QStringLiteral("Shock max pct"), external_shock_max_spin_);
	events_form->addRow(QStringLiteral("Buyer exhaustion"), buyer_exhaustion_spin_);
	events_form->addRow(QStringLiteral("Panic drawdown"), panic_drawdown_spin_);
	events_form->addRow(QStringLiteral("Forced sell cascade"), forced_selling_cascade_spin_);
	events_form->addRow(QStringLiteral("Profit taking threshold"), profit_taking_threshold_spin_);
	events_form->addRow(QStringLiteral("Smart money %"), smart_money_population_spin_);
	events_form->addRow(QStringLiteral("Smart distribution"), smart_money_distribution_spin_);
	events_form->addRow(QStringLiteral("Max ticks"), max_ticks_spin_);
	events_form->addRow(QStringLiteral("Random seed"), random_seed_spin_);

	content_layout->addWidget(identity_group);
	content_layout->addWidget(capital_group);
	content_layout->addWidget(liquidity_group);
	content_layout->addWidget(behavior_group);
	content_layout->addWidget(events_group);
	content_layout->addStretch(1);

	set_params(SimulationParams());
}

SimulationParams ParameterPanelWidget::params() const
{
	SimulationParams result;
	result.asset_symbol = asset_symbol_edit_->text();
	result.asset_name = asset_name_edit_->text();
	result.participant_count = participant_count_spin_->value();
	result.initial_price = initial_price_spin_->value();
	result.min_initial_balance = min_initial_balance_spin_->value();
	result.max_initial_balance = max_initial_balance_spin_->value();
	result.trend_population_pct = trend_population_spin_->value();
	result.contrarian_population_pct = contrarian_population_spin_->value();
	result.neutral_population_pct = neutral_population_spin_->value();
	result.leverage_enabled = leverage_enabled_check_->isChecked();
	result.average_leverage = average_leverage_spin_->value();
	result.max_leverage = max_leverage_spin_->value();
	result.maintenance_margin_rate = maintenance_margin_spin_->value();
	result.base_liquidity = base_liquidity_spin_->value();
	result.liquidity_collapse_multiplier = liquidity_collapse_multiplier_spin_->value();
	result.liquidity_sensitivity_to_volatility = liquidity_sensitivity_to_volatility_spin_->value();
	result.liquidity_sensitivity_to_order_imbalance = liquidity_sensitivity_to_order_imbalance_spin_->value();
	result.price_impact_coefficient = price_impact_spin_->value();
	result.random_noise_level = random_noise_spin_->value();
	result.max_random_drop_pct = max_random_drop_spin_->value();
	result.momentum_window = momentum_window_spin_->value();
	result.momentum_response_strength = momentum_response_strength_spin_->value();
	result.pnl_feedback_strength = pnl_feedback_strength_spin_->value();
	result.herding_strength = herding_strength_spin_->value();
	result.drawdown_fear_strength = drawdown_fear_strength_spin_->value();
	result.neutral_to_trend_conversion_rate = neutral_to_trend_spin_->value();
	result.trend_to_panic_conversion_rate = trend_to_panic_spin_->value();
	result.contrarian_to_trend_conversion_rate = contrarian_to_trend_spin_->value();
	result.external_shock_probability = external_shock_probability_spin_->value();
	result.external_shock_min_pct = external_shock_min_spin_->value();
	result.external_shock_max_pct = external_shock_max_spin_->value();
	result.buyer_exhaustion_threshold = buyer_exhaustion_spin_->value();
	result.panic_drawdown_threshold = panic_drawdown_spin_->value();
	result.forced_selling_cascade_threshold = forced_selling_cascade_spin_->value();
	result.profit_taking_threshold = profit_taking_threshold_spin_->value();
	result.smart_money_population_pct = smart_money_population_spin_->value();
	result.smart_money_distribution_rate = smart_money_distribution_spin_->value();
	result.max_ticks = max_ticks_spin_->value();
	result.random_seed = static_cast<unsigned int>(random_seed_spin_->value());
	return result;
}

void ParameterPanelWidget::set_params(const SimulationParams &params)
{
	asset_symbol_edit_->setText(params.asset_symbol);
	asset_name_edit_->setText(params.asset_name);
	participant_count_spin_->setValue(params.participant_count);
	initial_price_spin_->setValue(params.initial_price);
	min_initial_balance_spin_->setValue(params.min_initial_balance);
	max_initial_balance_spin_->setValue(params.max_initial_balance);
	trend_population_spin_->setValue(params.trend_population_pct);
	contrarian_population_spin_->setValue(params.contrarian_population_pct);
	neutral_population_spin_->setValue(params.neutral_population_pct);
	leverage_enabled_check_->setChecked(params.leverage_enabled);
	average_leverage_spin_->setValue(params.average_leverage);
	max_leverage_spin_->setValue(params.max_leverage);
	maintenance_margin_spin_->setValue(params.maintenance_margin_rate);
	base_liquidity_spin_->setValue(params.base_liquidity);
	liquidity_collapse_multiplier_spin_->setValue(params.liquidity_collapse_multiplier);
	liquidity_sensitivity_to_volatility_spin_->setValue(params.liquidity_sensitivity_to_volatility);
	liquidity_sensitivity_to_order_imbalance_spin_->setValue(params.liquidity_sensitivity_to_order_imbalance);
	price_impact_spin_->setValue(params.price_impact_coefficient);
	random_noise_spin_->setValue(params.random_noise_level);
	max_random_drop_spin_->setValue(params.max_random_drop_pct);
	momentum_window_spin_->setValue(params.momentum_window);
	momentum_response_strength_spin_->setValue(params.momentum_response_strength);
	pnl_feedback_strength_spin_->setValue(params.pnl_feedback_strength);
	herding_strength_spin_->setValue(params.herding_strength);
	drawdown_fear_strength_spin_->setValue(params.drawdown_fear_strength);
	neutral_to_trend_spin_->setValue(params.neutral_to_trend_conversion_rate);
	trend_to_panic_spin_->setValue(params.trend_to_panic_conversion_rate);
	contrarian_to_trend_spin_->setValue(params.contrarian_to_trend_conversion_rate);
	external_shock_probability_spin_->setValue(params.external_shock_probability);
	external_shock_min_spin_->setValue(params.external_shock_min_pct);
	external_shock_max_spin_->setValue(params.external_shock_max_pct);
	buyer_exhaustion_spin_->setValue(params.buyer_exhaustion_threshold);
	panic_drawdown_spin_->setValue(params.panic_drawdown_threshold);
	forced_selling_cascade_spin_->setValue(params.forced_selling_cascade_threshold);
	profit_taking_threshold_spin_->setValue(params.profit_taking_threshold);
	smart_money_population_spin_->setValue(params.smart_money_population_pct);
	smart_money_distribution_spin_->setValue(params.smart_money_distribution_rate);
	max_ticks_spin_->setValue(params.max_ticks);
	random_seed_spin_->setValue(static_cast<int>(params.random_seed));
}
