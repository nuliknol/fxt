#include "MarketSimulator.h"

#include <QtMath>
#include <algorithm>

MarketSimulator::MarketSimulator(QObject *parent)
	: QObject(parent)
{
	reset();
}

void MarketSimulator::set_params(const SimulationParams &new_params)
{
	params_ = new_params;
	random_.seed(params_.random_seed);
}

const SimulationParams &MarketSimulator::params() const
{
	return params_;
}

QString MarketSimulator::validate_params(const SimulationParams &candidate) const
{
	const double sum = candidate.trend_population_pct
		+ candidate.contrarian_population_pct
		+ candidate.neutral_population_pct;

	if (qAbs(sum - 100.0) >= 0.0001) {
		return QStringLiteral("Trader population percentages must sum to 100.");
	}
	if (candidate.participant_count <= 0) {
		return QStringLiteral("Participant count must be positive.");
	}
	if (candidate.initial_price <= 0.0) {
		return QStringLiteral("Initial price must be positive.");
	}
	if (candidate.min_initial_balance <= 0.0) {
		return QStringLiteral("Minimum initial balance must be positive.");
	}
	if (candidate.max_initial_balance < candidate.min_initial_balance) {
		return QStringLiteral("Maximum initial balance must be at least the minimum.");
	}
	if (candidate.base_liquidity <= 0.0) {
		return QStringLiteral("Base liquidity must be positive.");
	}
	if (candidate.maintenance_margin_rate <= 0.0 || candidate.maintenance_margin_rate >= 1.0) {
		return QStringLiteral("Maintenance margin rate must be between 0 and 1.");
	}
	if (candidate.average_leverage < 1.0) {
		return QStringLiteral("Average leverage must be at least 1.");
	}
	if (candidate.max_leverage < candidate.average_leverage) {
		return QStringLiteral("Max leverage must be at least average leverage.");
	}
	if (candidate.max_ticks <= 0) {
		return QStringLiteral("Max ticks must be positive.");
	}
	return QString();
}

void MarketSimulator::reset()
{
	random_.seed(params_.random_seed);
	price_ = params_.initial_price;
	peak_price_ = params_.initial_price;
	liquidity_ = params_.base_liquidity;
	tick_return_ = 0.0;
	tick_ = 0;
	state_ = STATE_ACCUMULATION;
	prices_.clear();
	liquidities_.clear();
	states_.clear();
	initialize_traders();
	update_trader_equity();
	OrderAccumulator orders;
	update_metrics(orders);
	prices_.append(price_);
	liquidities_.append(liquidity_);
	states_.append(static_cast<int>(state_));
	emit simulation_reset();
	emit simulation_updated();
}

void MarketSimulator::run_steps(int count)
{
	for (int i = 0; i < count && tick_ < params_.max_ticks; ++i) {
		step();
	}
}

void MarketSimulator::step()
{
	if (tick_ >= params_.max_ticks) {
		emit simulation_finished();
		return;
	}

	update_trader_equity();

	OrderAccumulator orders;
	compute_orders(&orders);
	apply_liquidations(&orders);
	update_price(orders);
	update_market_state(orders);
	update_trader_equity();

	++tick_;
	prices_.append(price_);
	liquidities_.append(liquidity_);
	states_.append(static_cast<int>(state_));
	update_metrics(orders);

	emit simulation_updated();

	if (tick_ >= params_.max_ticks) {
		emit simulation_finished();
	}
}

const QVector<double> &MarketSimulator::price_history() const
{
	return prices_;
}

const QVector<double> &MarketSimulator::liquidity_history() const
{
	return liquidities_;
}

const QVector<int> &MarketSimulator::state_history() const
{
	return states_;
}

SimulationMetrics MarketSimulator::metrics() const
{
	return last_metrics_;
}

void MarketSimulator::initialize_traders()
{
	traders_.clear();
	traders_.reserve(params_.participant_count);

	const int smart_money_count = qRound(params_.participant_count * params_.smart_money_population_pct / 100.0);
	const int trend_count = qRound(params_.participant_count * params_.trend_population_pct / 100.0);
	const int contrarian_count = qRound(params_.participant_count * params_.contrarian_population_pct / 100.0);

	for (int i = 0; i < params_.participant_count; ++i) {
		Trader trader;
		trader.id = i;
		trader.cash = rand_range(params_.min_initial_balance, params_.max_initial_balance);
		trader.initial_cash = trader.cash;
		trader.equity = trader.cash;
		trader.leverage = params_.leverage_enabled
			? rand_range(1.0, params_.max_leverage)
			: 1.0;
		trader.leverage = 0.5 * trader.leverage + 0.5 * params_.average_leverage;
		trader.leverage = clamp(trader.leverage, 1.0, params_.max_leverage);
		trader.confidence = rand_range(0.2, 0.6);
		trader.fear = rand_range(0.1, 0.4);
		trader.panic_threshold = rand_range(params_.panic_drawdown_threshold * 0.6, params_.panic_drawdown_threshold * 1.4);
		trader.profit_taking_threshold = rand_range(params_.profit_taking_threshold * 0.7, params_.profit_taking_threshold * 1.3);

		if (i < smart_money_count) {
			trader.type = TRADER_SMART_MONEY;
			trader.confidence = rand_range(0.55, 0.80);
		} else if (i < smart_money_count + trend_count) {
			trader.type = TRADER_TREND;
		} else if (i < smart_money_count + trend_count + contrarian_count) {
			trader.type = TRADER_CONTRARIAN;
		} else {
			trader.type = TRADER_NEUTRAL;
		}

		traders_.append(trader);
	}
}

void MarketSimulator::update_trader_equity()
{
	for (Trader &trader : traders_) {
		trader.margin_called = false;

		if (trader.direction == POS_FLAT || trader.asset_units <= 0.0) {
			trader.unrealized_pnl = 0.0;
			trader.equity = trader.cash;
		} else if (trader.direction == POS_LONG) {
			const double position_value = trader.asset_units * price_;
			trader.unrealized_pnl = (price_ - trader.entry_price) * trader.asset_units;
			trader.equity = trader.cash + position_value - trader.debt;
		} else {
			const double short_value = trader.asset_units * price_;
			trader.unrealized_pnl = (trader.entry_price - price_) * trader.asset_units;
			trader.equity = trader.cash + trader.debt - short_value;
		}

		if (trader.equity <= 0.0) {
			trader.bankrupt = true;
		}
	}
}

void MarketSimulator::compute_orders(OrderAccumulator *orders)
{
	const double momentum = recent_return();
	const double drawdown = drawdown_from_peak();
	const double buy_pressure = recent_buy_pressure();

	for (Trader &trader : traders_) {
		if (trader.bankrupt) {
			continue;
		}

		if (trader.type == TRADER_NEUTRAL && momentum > 0.01 && rand01() < params_.neutral_to_trend_conversion_rate) {
			trader.type = TRADER_TREND;
		}
		if (trader.type == TRADER_CONTRARIAN && momentum > 0.025 && rand01() < params_.contrarian_to_trend_conversion_rate) {
			trader.type = TRADER_TREND;
		}
		if (trader.type == TRADER_TREND && drawdown > trader.panic_threshold && rand01() < params_.trend_to_panic_conversion_rate) {
			trader.type = TRADER_PANIC;
		}

		const double pnl_ratio = trader.initial_cash > 0.0 ? trader.unrealized_pnl / trader.initial_cash : 0.0;
		trader.confidence = clamp(0.5 + pnl_ratio * 0.6 + momentum * 3.0 - drawdown * 2.5, 0.0, 1.0);
		trader.fear = clamp(0.2 + drawdown * 4.0 - pnl_ratio * 0.4, 0.0, 1.0);
		const double risk_fraction = 0.01 + 0.09 * trader.confidence;
		const double desired_notional = trader.cash * risk_fraction * trader.leverage;

		switch (trader.type) {
		case TRADER_TREND: {
			const double buy_score =
				params_.momentum_response_strength * momentum
				+ params_.pnl_feedback_strength * pnl_ratio
				+ params_.herding_strength * buy_pressure
				- params_.drawdown_fear_strength * drawdown;
			const double buy_probability = sigmoid(buy_score);
			if (rand01() < buy_probability) {
				open_long_position(&trader, desired_notional, price_, &orders->organic_buy);
			} else if (trader.direction == POS_LONG && drawdown > 0.02 && rand01() < drawdown * 3.0) {
				reduce_long_position(&trader, clamp(drawdown * 2.0, 0.05, 0.35), price_, &orders->organic_sell);
			}
			break;
		}
		case TRADER_CONTRARIAN:
			if (momentum > 0.002) {
				open_short_position(&trader, desired_notional, price_, &orders->organic_sell);
			} else if (momentum < -0.002) {
				open_long_position(&trader, desired_notional * 0.7, price_, &orders->organic_buy);
			}
			break;
		case TRADER_NEUTRAL:
			if (rand01() < 0.5) {
				open_long_position(&trader, desired_notional * 0.4, price_, &orders->organic_buy);
			} else if (trader.direction == POS_LONG) {
				reduce_long_position(&trader, 0.10, price_, &orders->organic_sell);
			} else {
				open_short_position(&trader, desired_notional * 0.3, price_, &orders->organic_sell);
			}
			break;
		case TRADER_SMART_MONEY:
			if (tick_ < params_.momentum_window * 3 && momentum >= -0.003) {
				open_long_position(&trader, desired_notional * 0.8, price_, &orders->organic_buy);
			}
			if (pnl_ratio > trader.profit_taking_threshold) {
				const double fraction = clamp(params_.smart_money_distribution_rate, 0.01, 0.5);
				const double before_units = trader.asset_units;
				reduce_long_position(&trader, fraction, price_, &orders->organic_sell);
				if (before_units > trader.asset_units) {
					orders->smart_money_sell += (before_units - trader.asset_units) * price_;
				}
			}
			break;
		case TRADER_PANIC:
			if (trader.direction == POS_LONG) {
				reduce_long_position(&trader, clamp(0.25 + drawdown * 2.0, 0.2, 1.0), price_, &orders->organic_sell);
			} else if (drawdown > 0.03) {
				open_short_position(&trader, desired_notional * 0.5, price_, &orders->organic_sell);
			}
			break;
		}
	}
}

void MarketSimulator::apply_liquidations(OrderAccumulator *orders)
{
	for (Trader &trader : traders_) {
		if (trader.bankrupt || trader.direction == POS_FLAT || trader.asset_units <= 0.0) {
			continue;
		}

		if (trader.direction == POS_LONG) {
			const double position_value = trader.asset_units * price_;
			const double equity = trader.cash + position_value - trader.debt;
			const double maintenance_required = position_value * params_.maintenance_margin_rate;

			if (equity < maintenance_required) {
				orders->forced_sell += position_value;
				trader.cash = qMax(equity, 0.0);
				trader.asset_units = 0.0;
				trader.entry_price = 0.0;
				trader.debt = 0.0;
				trader.direction = POS_FLAT;
				trader.margin_called = true;
				if (trader.cash <= 0.0) {
					trader.bankrupt = true;
				}
			}
		} else if (trader.direction == POS_SHORT) {
			const double short_value = trader.asset_units * price_;
			const double equity = trader.cash + trader.debt - short_value;
			const double maintenance_required = short_value * params_.maintenance_margin_rate;

			if (equity < maintenance_required) {
				orders->forced_buy += short_value;
				trader.cash = qMax(equity, 0.0);
				trader.asset_units = 0.0;
				trader.entry_price = 0.0;
				trader.debt = 0.0;
				trader.direction = POS_FLAT;
				trader.margin_called = true;
				if (trader.cash <= 0.0) {
					trader.bankrupt = true;
				}
			}
		}
	}
}

void MarketSimulator::update_price(const OrderAccumulator &orders)
{
	const double total_flow = orders.organic_buy + orders.organic_sell + orders.forced_buy + orders.forced_sell;
	const double net_flow = orders.organic_buy + orders.forced_buy - orders.organic_sell - orders.forced_sell;
	const double imbalance = qAbs(net_flow) / qMax(total_flow, 1.0);
	const double volatility_penalty = qMin(qAbs(tick_return_) * params_.liquidity_sensitivity_to_volatility, 0.95);
	const double imbalance_penalty = qMin(params_.liquidity_sensitivity_to_order_imbalance * imbalance, 0.95);

	liquidity_ = params_.base_liquidity;
	if (state_ == STATE_COLLAPSE || state_ == STATE_CAPITULATION) {
		liquidity_ *= params_.liquidity_collapse_multiplier;
	}
	liquidity_ *= (1.0 - volatility_penalty);
	liquidity_ *= (1.0 - imbalance_penalty);
	liquidity_ = qMax(liquidity_, params_.base_liquidity * 0.05);

	const double flow_ratio = net_flow / qMax(liquidity_, 1.0);
	double shock = 0.0;
	if (rand01() < params_.external_shock_probability) {
		shock = -rand_range(params_.external_shock_min_pct, params_.external_shock_max_pct);
	}
	const double noise = rand_range(-params_.random_noise_level, params_.random_noise_level);
	const double impact = params_.price_impact_coefficient * std::tanh(flow_ratio);
	const double previous_price = price_;
	price_ *= qExp(impact + noise + shock);

	if (state_ == STATE_COLLAPSE || state_ == STATE_CAPITULATION) {
		const double random_drop = rand_range(0.0, params_.max_random_drop_pct * 0.1);
		price_ *= (1.0 - random_drop);
	}

	price_ = qMax(price_, 0.0001);
	peak_price_ = qMax(peak_price_, price_);
	tick_return_ = previous_price > 0.0 ? (price_ - previous_price) / previous_price : 0.0;
}

void MarketSimulator::update_market_state(const OrderAccumulator &orders)
{
	const double recent = recent_return();
	const double drawdown = drawdown_from_peak();
	const double cash_ratio = trend_cash_remaining_ratio();
	const bool buyer_exhausted = cash_ratio < params_.buyer_exhaustion_threshold;
	const bool panic_drawdown = drawdown > params_.panic_drawdown_threshold;
	const bool forced_selling_dominates = orders.forced_sell > orders.organic_buy * params_.forced_selling_cascade_threshold;

	switch (state_) {
	case STATE_ACCUMULATION:
		if (recent > 0.003) {
			state_ = STATE_MARKUP;
		}
		break;
	case STATE_MARKUP:
		if (price_ / params_.initial_price > 2.0 && recent > 0.01) {
			state_ = STATE_EUPHORIA;
		}
		break;
	case STATE_EUPHORIA:
		if (orders.smart_money_sell > orders.organic_buy * 0.15 || cash_ratio < 0.4) {
			state_ = STATE_DISTRIBUTION;
		}
		break;
	case STATE_DISTRIBUTION:
		if (buyer_exhausted && panic_drawdown && forced_selling_dominates) {
			state_ = STATE_COLLAPSE;
		}
		break;
	case STATE_COLLAPSE:
		if (last_metrics_.bankrupt_count > params_.participant_count / 50 || orders.forced_sell < last_metrics_.forced_sell) {
			state_ = STATE_CAPITULATION;
		}
		break;
	case STATE_CAPITULATION:
		if (qAbs(tick_return_) < 0.003 && orders.forced_sell < params_.base_liquidity * 0.0005) {
			state_ = STATE_STABILIZATION;
		}
		break;
	case STATE_STABILIZATION:
		break;
	}
}

void MarketSimulator::update_metrics(const OrderAccumulator &orders)
{
	SimulationMetrics metrics;
	metrics.tick = tick_;
	metrics.price = price_;
	metrics.peak_price = peak_price_;
	metrics.drawdown = drawdown_from_peak();
	metrics.liquidity = liquidity_;
	metrics.organic_buy = orders.organic_buy;
	metrics.organic_sell = orders.organic_sell;
	metrics.forced_buy = orders.forced_buy;
	metrics.forced_sell = orders.forced_sell;
	metrics.trend_cash_remaining_ratio = trend_cash_remaining_ratio();
	metrics.state = state_;

	double total_leverage_sum = 0.0;
	int leveraged_count = 0;

	for (const Trader &trader : traders_) {
		switch (trader.type) {
		case TRADER_TREND:
			++metrics.trend_count;
			break;
		case TRADER_CONTRARIAN:
			++metrics.contrarian_count;
			break;
		case TRADER_NEUTRAL:
			++metrics.neutral_count;
			break;
		case TRADER_PANIC:
			++metrics.panic_count;
			break;
		case TRADER_SMART_MONEY:
			break;
		}

		if (trader.margin_called) {
			++metrics.margin_call_count;
		}
		if (trader.bankrupt) {
			++metrics.bankrupt_count;
		}
		if (trader.direction == POS_LONG) {
			metrics.total_long_exposure += trader.asset_units * price_;
		} else if (trader.direction == POS_SHORT) {
			metrics.total_short_exposure += trader.asset_units * price_;
		}
		if (trader.asset_units > 0.0) {
			total_leverage_sum += trader.leverage;
			++leveraged_count;
		}
	}

	metrics.total_leverage = leveraged_count > 0 ? total_leverage_sum / leveraged_count : 0.0;
	last_metrics_ = metrics;
}

double MarketSimulator::recent_return() const
{
	if (prices_.size() < 2) {
		return 0.0;
	}
	const int start = qMax(0, prices_.size() - 1 - params_.momentum_window);
	const double start_price = prices_.at(start);
	const double end_price = prices_.last();
	if (start_price <= 0.0) {
		return 0.0;
	}
	return (end_price - start_price) / start_price;
}

double MarketSimulator::drawdown_from_peak() const
{
	if (peak_price_ <= 0.0) {
		return 0.0;
	}
	return qMax(0.0, (peak_price_ - price_) / peak_price_);
}

double MarketSimulator::trend_cash_remaining_ratio() const
{
	double current_cash = 0.0;
	double initial_cash = 0.0;

	for (const Trader &trader : traders_) {
		if (trader.type == TRADER_TREND || trader.type == TRADER_PANIC) {
			current_cash += qMax(trader.cash, 0.0);
			initial_cash += trader.initial_cash;
		}
	}

	if (initial_cash <= 0.0) {
		return 0.0;
	}
	return current_cash / initial_cash;
}

double MarketSimulator::recent_buy_pressure() const
{
	if (last_metrics_.organic_buy <= 0.0 && last_metrics_.organic_sell <= 0.0) {
		return 0.0;
	}
	const double total = last_metrics_.organic_buy + last_metrics_.organic_sell;
	if (total <= 0.0) {
		return 0.0;
	}
	return (last_metrics_.organic_buy - last_metrics_.organic_sell) / total;
}

double MarketSimulator::rand01()
{
	return random_.generateDouble();
}

double MarketSimulator::rand_range(double a, double b)
{
	return a + (b - a) * rand01();
}

double MarketSimulator::sigmoid(double value) const
{
	return 1.0 / (1.0 + qExp(-value));
}

double MarketSimulator::clamp(double value, double low, double high) const
{
	return std::max(low, std::min(value, high));
}

void MarketSimulator::open_long_position(Trader *trader, double desired_notional, double execution_price, double *organic_buy)
{
	if (!trader || trader->bankrupt || execution_price <= 0.0) {
		return;
	}

	if (trader->direction == POS_SHORT) {
		reduce_short_position(trader, 0.25, execution_price, organic_buy);
	}

	const double leverage = qMax(trader->leverage, 1.0);
	const double max_margin = trader->cash * 0.35;
	double margin_cash = qMin(max_margin, desired_notional / leverage);
	margin_cash = qMin(margin_cash, trader->cash);

	if (margin_cash <= 1.0) {
		return;
	}

	const double notional = margin_cash * leverage;
	const double units = notional / execution_price;
	if (units <= 0.0) {
		return;
	}

	if (trader->direction == POS_LONG && trader->asset_units > 0.0) {
		trader->entry_price =
			(trader->asset_units * trader->entry_price + units * execution_price)
			/ (trader->asset_units + units);
	} else {
		trader->entry_price = execution_price;
	}

	trader->direction = POS_LONG;
	trader->cash -= margin_cash;
	trader->asset_units += units;
	trader->debt += (notional - margin_cash);
	*organic_buy += notional;
}

void MarketSimulator::open_short_position(Trader *trader, double desired_notional, double execution_price, double *organic_sell)
{
	if (!trader || trader->bankrupt || execution_price <= 0.0) {
		return;
	}

	if (trader->direction == POS_LONG) {
		reduce_long_position(trader, 0.25, execution_price, organic_sell);
	}

	const double leverage = qMax(trader->leverage, 1.0);
	const double max_margin = trader->cash * 0.30;
	double margin_cash = qMin(max_margin, desired_notional / leverage);
	margin_cash = qMin(margin_cash, trader->cash);

	if (margin_cash <= 1.0) {
		return;
	}

	const double notional = margin_cash * leverage;
	const double units = notional / execution_price;
	if (units <= 0.0) {
		return;
	}

	if (trader->direction == POS_SHORT && trader->asset_units > 0.0) {
		trader->entry_price =
			(trader->asset_units * trader->entry_price + units * execution_price)
			/ (trader->asset_units + units);
	} else {
		trader->entry_price = execution_price;
	}

	trader->direction = POS_SHORT;
	trader->cash -= margin_cash;
	trader->asset_units += units;
	trader->debt += notional;
	*organic_sell += notional;
}

void MarketSimulator::reduce_long_position(Trader *trader, double fraction, double execution_price, double *sell_flow)
{
	if (!trader || trader->direction != POS_LONG || trader->asset_units <= 0.0) {
		return;
	}

	fraction = clamp(fraction, 0.0, 1.0);
	const double units_to_sell = trader->asset_units * fraction;
	const double proceeds = units_to_sell * execution_price;
	const double debt_share = trader->debt * fraction;
	const double pnl = (execution_price - trader->entry_price) * units_to_sell;

	trader->asset_units -= units_to_sell;
	trader->debt -= debt_share;
	trader->cash += qMax(0.0, proceeds - debt_share);
	trader->cash += qMax(0.0, pnl * 0.1);
	if (trader->asset_units <= 0.0000001) {
		trader->asset_units = 0.0;
		trader->debt = 0.0;
		trader->entry_price = 0.0;
		trader->direction = POS_FLAT;
	}
	*sell_flow += proceeds;
}

void MarketSimulator::reduce_short_position(Trader *trader, double fraction, double execution_price, double *buy_flow)
{
	if (!trader || trader->direction != POS_SHORT || trader->asset_units <= 0.0) {
		return;
	}

	fraction = clamp(fraction, 0.0, 1.0);
	const double units_to_cover = trader->asset_units * fraction;
	const double cover_cost = units_to_cover * execution_price;
	const double debt_share = trader->debt * fraction;
	const double pnl = (trader->entry_price - execution_price) * units_to_cover;

	trader->asset_units -= units_to_cover;
	trader->debt -= debt_share;
	trader->cash += pnl;
	if (trader->cash < 0.0) {
		trader->cash = 0.0;
	}
	if (trader->asset_units <= 0.0000001) {
		trader->asset_units = 0.0;
		trader->debt = 0.0;
		trader->entry_price = 0.0;
		trader->direction = POS_FLAT;
	}
	*buy_flow += cover_cost;
}

QString market_state_to_string(MarketState state)
{
	switch (state) {
	case STATE_ACCUMULATION:
		return QStringLiteral("Accumulation");
	case STATE_MARKUP:
		return QStringLiteral("Markup");
	case STATE_EUPHORIA:
		return QStringLiteral("Euphoria");
	case STATE_DISTRIBUTION:
		return QStringLiteral("Distribution");
	case STATE_COLLAPSE:
		return QStringLiteral("Collapse");
	case STATE_CAPITULATION:
		return QStringLiteral("Capitulation");
	case STATE_STABILIZATION:
		return QStringLiteral("Stabilization");
	}
	return QStringLiteral("Unknown");
}
