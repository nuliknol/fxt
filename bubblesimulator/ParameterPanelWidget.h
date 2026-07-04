#ifndef PARAMETERPANELWIDGET_H
#define PARAMETERPANELWIDGET_H

#include <QWidget>

#include "MarketSimulator.h"

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QScrollArea;
class QSpinBox;

class ParameterPanelWidget : public QWidget
{
	Q_OBJECT

public:
	explicit ParameterPanelWidget(QWidget *parent = nullptr);

	SimulationParams params() const;
	void set_params(const SimulationParams &params);

private:
	QLineEdit *asset_symbol_edit_;
	QLineEdit *asset_name_edit_;
	QSpinBox *participant_count_spin_;
	QDoubleSpinBox *initial_price_spin_;
	QDoubleSpinBox *min_initial_balance_spin_;
	QDoubleSpinBox *max_initial_balance_spin_;
	QDoubleSpinBox *trend_population_spin_;
	QDoubleSpinBox *contrarian_population_spin_;
	QDoubleSpinBox *neutral_population_spin_;
	QCheckBox *leverage_enabled_check_;
	QDoubleSpinBox *average_leverage_spin_;
	QDoubleSpinBox *max_leverage_spin_;
	QDoubleSpinBox *maintenance_margin_spin_;
	QDoubleSpinBox *base_liquidity_spin_;
	QDoubleSpinBox *liquidity_collapse_multiplier_spin_;
	QDoubleSpinBox *liquidity_sensitivity_to_volatility_spin_;
	QDoubleSpinBox *liquidity_sensitivity_to_order_imbalance_spin_;
	QDoubleSpinBox *price_impact_spin_;
	QDoubleSpinBox *random_noise_spin_;
	QDoubleSpinBox *max_random_drop_spin_;
	QSpinBox *momentum_window_spin_;
	QDoubleSpinBox *momentum_response_strength_spin_;
	QDoubleSpinBox *pnl_feedback_strength_spin_;
	QDoubleSpinBox *herding_strength_spin_;
	QDoubleSpinBox *drawdown_fear_strength_spin_;
	QDoubleSpinBox *neutral_to_trend_spin_;
	QDoubleSpinBox *trend_to_panic_spin_;
	QDoubleSpinBox *contrarian_to_trend_spin_;
	QDoubleSpinBox *external_shock_probability_spin_;
	QDoubleSpinBox *external_shock_min_spin_;
	QDoubleSpinBox *external_shock_max_spin_;
	QDoubleSpinBox *buyer_exhaustion_spin_;
	QDoubleSpinBox *panic_drawdown_spin_;
	QDoubleSpinBox *forced_selling_cascade_spin_;
	QDoubleSpinBox *profit_taking_threshold_spin_;
	QDoubleSpinBox *smart_money_population_spin_;
	QDoubleSpinBox *smart_money_distribution_spin_;
	QSpinBox *max_ticks_spin_;
	QSpinBox *random_seed_spin_;
};

#endif
