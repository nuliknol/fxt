#ifndef MARKETSIMULATOR_H
#define MARKETSIMULATOR_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QRandomGenerator>

struct SimulationParams
{
	int participant_count = 10000;

	QString asset_symbol = QStringLiteral("XAUUSD");
	QString asset_name = QStringLiteral("Gold Spot");

	double initial_price = 35.0;

	double min_initial_balance = 1000.0;
	double max_initial_balance = 100000.0;

	double trend_population_pct = 75.0;
	double contrarian_population_pct = 10.0;
	double neutral_population_pct = 15.0;

	bool leverage_enabled = true;
	double average_leverage = 3.0;
	double max_leverage = 10.0;
	double maintenance_margin_rate = 0.25;

	double base_liquidity = 10000000.0;
	double liquidity_collapse_multiplier = 0.20;
	double liquidity_sensitivity_to_volatility = 0.50;
	double liquidity_sensitivity_to_order_imbalance = 0.50;

	double price_impact_coefficient = 0.05;
	double random_noise_level = 0.005;
	double max_random_drop_pct = 0.10;

	int momentum_window = 20;
	double momentum_response_strength = 2.0;
	double pnl_feedback_strength = 1.5;
	double herding_strength = 1.0;
	double drawdown_fear_strength = 3.0;

	double neutral_to_trend_conversion_rate = 0.01;
	double trend_to_panic_conversion_rate = 0.02;
	double contrarian_to_trend_conversion_rate = 0.005;

	double external_shock_probability = 0.002;
	double external_shock_min_pct = 0.01;
	double external_shock_max_pct = 0.05;

	double buyer_exhaustion_threshold = 0.20;
	double panic_drawdown_threshold = 0.05;
	double forced_selling_cascade_threshold = 1.20;

	double profit_taking_threshold = 1.00;
	double smart_money_population_pct = 5.0;
	double smart_money_distribution_rate = 0.05;

	int max_ticks = 5000;
	unsigned int random_seed = 1;
};

enum TraderType
{
	TRADER_TREND,
	TRADER_CONTRARIAN,
	TRADER_NEUTRAL,
	TRADER_SMART_MONEY,
	TRADER_PANIC
};

enum PositionDirection
{
	POS_FLAT,
	POS_LONG,
	POS_SHORT
};

struct Trader
{
	int id = 0;
	TraderType type = TRADER_NEUTRAL;
	PositionDirection direction = POS_FLAT;
	double cash = 0.0;
	double initial_cash = 0.0;
	double asset_units = 0.0;
	double entry_price = 0.0;
	double leverage = 1.0;
	double debt = 0.0;
	double equity = 0.0;
	double unrealized_pnl = 0.0;
	double confidence = 0.0;
	double fear = 0.0;
	double panic_threshold = 0.0;
	double profit_taking_threshold = 0.0;
	bool bankrupt = false;
	bool margin_called = false;
};

enum MarketState
{
	STATE_ACCUMULATION,
	STATE_MARKUP,
	STATE_EUPHORIA,
	STATE_DISTRIBUTION,
	STATE_COLLAPSE,
	STATE_CAPITULATION,
	STATE_STABILIZATION
};

struct SimulationMetrics
{
	int tick = 0;
	double price = 0.0;
	double peak_price = 0.0;
	double drawdown = 0.0;
	double liquidity = 0.0;
	double organic_buy = 0.0;
	double organic_sell = 0.0;
	double forced_buy = 0.0;
	double forced_sell = 0.0;
	double trend_cash_remaining_ratio = 0.0;
	double total_long_exposure = 0.0;
	double total_short_exposure = 0.0;
	double total_leverage = 0.0;
	int margin_call_count = 0;
	int bankrupt_count = 0;
	int trend_count = 0;
	int contrarian_count = 0;
	int neutral_count = 0;
	int panic_count = 0;
	MarketState state = STATE_ACCUMULATION;
};

class MarketSimulator : public QObject
{
	Q_OBJECT

public:
	explicit MarketSimulator(QObject *parent = nullptr);

	void set_params(const SimulationParams &new_params);
	const SimulationParams &params() const;
	void reset();
	void step();
	void run_steps(int count);

	const QVector<double> &price_history() const;
	const QVector<double> &liquidity_history() const;
	const QVector<int> &state_history() const;
	SimulationMetrics metrics() const;
	QString validate_params(const SimulationParams &candidate) const;

signals:
	void simulation_updated();
	void simulation_reset();
	void simulation_finished();

private:
	struct OrderAccumulator
	{
		double organic_buy = 0.0;
		double organic_sell = 0.0;
		double forced_buy = 0.0;
		double forced_sell = 0.0;
		double smart_money_sell = 0.0;
	};

	SimulationParams params_;
	QVector<Trader> traders_;
	QRandomGenerator random_;

	double price_ = 0.0;
	double peak_price_ = 0.0;
	double liquidity_ = 0.0;
	double tick_return_ = 0.0;
	int tick_ = 0;
	MarketState state_ = STATE_ACCUMULATION;

	QVector<double> prices_;
	QVector<double> liquidities_;
	QVector<int> states_;

	SimulationMetrics last_metrics_;

	void initialize_traders();
	void update_trader_equity();
	void compute_orders(OrderAccumulator *orders);
	void apply_liquidations(OrderAccumulator *orders);
	void update_price(const OrderAccumulator &orders);
	void update_market_state(const OrderAccumulator &orders);
	void update_metrics(const OrderAccumulator &orders);
	double recent_return() const;
	double drawdown_from_peak() const;
	double trend_cash_remaining_ratio() const;
	double recent_buy_pressure() const;
	double rand01();
	double rand_range(double a, double b);
	double sigmoid(double value) const;
	double clamp(double value, double low, double high) const;
	void open_long_position(Trader *trader, double desired_notional, double execution_price, double *organic_buy);
	void open_short_position(Trader *trader, double desired_notional, double execution_price, double *organic_sell);
	void reduce_long_position(Trader *trader, double fraction, double execution_price, double *sell_flow);
	void reduce_short_position(Trader *trader, double fraction, double execution_price, double *buy_flow);
};

QString market_state_to_string(MarketState state);

#endif
