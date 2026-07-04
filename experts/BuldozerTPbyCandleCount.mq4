#property strict

enum trade_direction_t
{
	BUY_ONLY = 0,
	SELL_ONLY = 1
};

enum instrument_type_t
{
	INSTRUMENT_UNKNOWN = 0,
	INSTRUMENT_FX = 1,
	INSTRUMENT_GOLD = 2,
	INSTRUMENT_INDEX = 3
};

enum pause_reason_t
{
	PAUSE_NONE = 0,
	PAUSE_LOSS_BUDGET = 1,
	PAUSE_MAX_VOLATILITY = 2,
	PAUSE_REMAINING_PIPS = 3,
	PAUSE_BUDGET_FOR_NEW = 4,
	PAUSE_LOT_NORMALIZATION = 5,
	PAUSE_TRADE_BLOCKED = 6
};

enum slot_entry_mode_t
{
	SLOT_ENTRY_FIRST_TOUCH = 0,
	SLOT_ENTRY_BEST_IN_SLOT = 1,
	SLOT_ENTRY_OFFSET = 2
};

input trade_direction_t trade_direction = BUY_ONLY;
input double initial_order_lot_size = 0.01;
input int grid_step_pips = 10;
input int maximum_volatility_pips = 200;
input double loss_budget_initial = 100.0;
input bool only_take_profit_mode = false;
input bool only_manage_my_own_orders = false;
input double protective_stop_after_pips = -1.0;
input double protective_stop_delta = 0.0;
input int log_level = 1;
input string ignore_tickets_csv = "";
input slot_entry_mode_t slot_entry_mode = SLOT_ENTRY_FIRST_TOUCH;
input int slot_entry_offset_pips = 0;
input bool enable_tp_structural_break = true;
input int trend_run_candles_to_arm_tp = 3;
input int directional_reference_depth = 2;
input bool enable_tp_low_volatility = true;
input int low_volatility_lookback_candles = 4;
input double low_volatility_total_body_pips_threshold = 3.0;
input bool enable_tp_wick_expansion = false;
input int wick_expansion_lookback_candles = 3;
input int min_profitable_orders_to_close = 1;

instrument_type_t g_instrument_type = INSTRUMENT_UNKNOWN;
double g_pip_size = 0.0;
double g_pip_value_per_lot = 0.0;
bool g_pip_value_dynamic = false;

double g_loss_budget = 0.0;
bool g_entries_paused = false;
double g_grid_order_size_active = 0.0;
double g_grid_order_size_last_valid = 0.0;
int g_prev_slot_id = 0;
bool g_prev_slot_id_initialized = false;
pause_reason_t g_pause_reason = PAUSE_NONE;
bool g_state_ready = false;
datetime g_last_tp_signal_bar_time = 0;

int g_filled_slots[];
int g_pending_slots[];
bool g_scan_has_orders = false;
int g_scan_p_extreme = 0;
double g_scan_existing_orders_lot_size = 0.0;
double g_scan_current_loss_usd = 0.0;
bool g_scan_extreme_order_profitable = false;
datetime g_last_timer_scan_time = 0;
bool g_timer_available = false;
datetime g_last_maintenance_run_time = 0;

int g_trade_retries = 10;
int g_trade_retry_sleep_ms = 250;
int g_trade_slippage_points = 20;
int g_ea_magic = 20260224;
int g_max_gap_orders_per_tick = 5;
int g_timer_period_seconds = 60;
int g_ignore_tickets[];

int adverse_retracement_from_extreme(const int p_now);
int adverse_distance_from_extreme(const int p_now);
double price_distance_to_pips(const double price_distance);
double pips_to_price(const double pips);
bool is_directional_candle(const int shift);
bool is_opposite_direction_candle(const int shift);
bool structural_break_tp_signal(string &reason_out);
bool low_volatility_tp_signal(string &reason_out);
bool wick_expansion_tp_signal(string &reason_out);
bool should_take_profits_now(string &reason_out);
bool tp_signal_already_used_for_current_bar(void);
bool close_breaks_structure_level(const double close_price, const double break_level);
double minimum_take_profit_pips(void);
double selected_order_profit_pips(void);
bool selected_order_meets_take_profit_floor(void);
void apply_protective_stops(void);
bool trade_open_precheck(const int type, const double lots, int &error_out, string &detail_out);
bool order_send_error_is_hard_stop(const int err);
int matching_order_type(void);

void log_msg(const int level, const string msg)
{
	if(log_level >= level)
		Print("Buldozer: " + msg);
}

string direction_to_string(void)
{
	if(trade_direction == BUY_ONLY)
		return "BUY_ONLY";
	return "SELL_ONLY";
}

string pause_reason_to_string(const pause_reason_t reason)
{
	if(reason == PAUSE_LOSS_BUDGET)
		return "loss_budget_exceeded";
	if(reason == PAUSE_MAX_VOLATILITY)
		return "max_volatility_exceeded";
	if(reason == PAUSE_REMAINING_PIPS)
		return "remaining_pips_non_positive";
	if(reason == PAUSE_BUDGET_FOR_NEW)
		return "budget_for_new_non_positive";
	if(reason == PAUSE_LOT_NORMALIZATION)
		return "lot_normalization_failed";
	if(reason == PAUSE_TRADE_BLOCKED)
		return "trade_open_blocked";
	return "none";
}

string slot_entry_mode_to_string(const slot_entry_mode_t mode)
{
	if(mode == SLOT_ENTRY_BEST_IN_SLOT)
		return "BEST_IN_SLOT";
	if(mode == SLOT_ENTRY_OFFSET)
		return "OFFSET";
	return "FIRST_TOUCH";
}

string trim_spaces(const string src)
{
	int len = StringLen(src);
	if(len <= 0)
		return "";

	int left = 0;
	int right = len - 1;
	while(left < len)
	{
		int ch_left = StringGetCharacter(src, left);
		if(ch_left != ' ' && ch_left != '\t')
			break;
		left++;
	}
	while(right >= left)
	{
		int ch_right = StringGetCharacter(src, right);
		if(ch_right != ' ' && ch_right != '\t')
			break;
		right--;
	}
	if(right < left)
		return "";
	return StringSubstr(src, left, right - left + 1);
}

bool try_parse_int(const string token, int &value_out)
{
	string s = trim_spaces(token);
	int len = StringLen(s);
	if(len <= 0)
		return false;

	int start = 0;
	bool negative = false;
	if(StringGetCharacter(s, 0) == '-')
	{
		negative = true;
		start = 1;
		if(len == 1)
			return false;
	}

	int v = 0;
	for(int i = start; i < len; ++i)
	{
		int ch = StringGetCharacter(s, i);
		if(ch < '0' || ch > '9')
			return false;
		v = (v * 10) + (ch - '0');
	}

	value_out = (negative ? -v : v);
	return true;
}

bool try_parse_positive_int(const string token, int &value_out)
{
	int v = 0;
	if(!try_parse_int(token, v))
		return false;
	if(v <= 0)
		return false;
	value_out = v;
	return true;
}

bool symbol_is_one_of(const string sym, const string a, const string b = "", const string c = "", const string d = "", const string e = "")
{
	if(sym == a)
		return true;
	if(b != "" && sym == b)
		return true;
	if(c != "" && sym == c)
		return true;
	if(d != "" && sym == d)
		return true;
	if(e != "" && sym == e)
		return true;
	return false;
}

bool ignore_ticket_contains(const int ticket)
{
	int n = ArraySize(g_ignore_tickets);
	for(int i = 0; i < n; ++i)
	{
		if(g_ignore_tickets[i] == ticket)
			return true;
	}
	return false;
}

string normalize_symbol_name(const string sym)
{
	int len = StringLen(sym);
	if(len > 2 && StringSubstr(sym, len - 2) == "_i")
		return StringSubstr(sym, 0, len - 2);
	return sym;
}

void parse_ignore_tickets(void)
{
	ArrayResize(g_ignore_tickets, 0);

	string src = ignore_tickets_csv;
	if(StringLen(src) <= 0)
	{
		log_msg(1, "Ignore order list active: 0 tickets");
		return;
	}

	string parts[];
	int cnt = StringSplit(src, ',', parts);
	if(cnt <= 0)
	{
		log_msg(1, "Ignore order list active: 0 tickets");
		return;
	}

	for(int i = 0; i < cnt; ++i)
	{
		int ticket = 0;
		if(!try_parse_positive_int(parts[i], ticket))
		{
			if(log_level >= 2)
				log_msg(2, "Ignore list token skipped: '" + parts[i] + "'");
			continue;
		}

		if(ignore_ticket_contains(ticket))
			continue;

		int n = ArraySize(g_ignore_tickets);
		ArrayResize(g_ignore_tickets, n + 1);
		g_ignore_tickets[n] = ticket;
	}

	log_msg(1, "Ignore order list active: " + IntegerToString(ArraySize(g_ignore_tickets)) + " tickets");
}

bool derive_pip_value_per_lot_from_broker(const double pip_size, double &value_out)
{
	value_out = 0.0;
	double tick_value = MarketInfo(Symbol(), MODE_TICKVALUE);
	double tick_size = MarketInfo(Symbol(), MODE_TICKSIZE);
	if(tick_value <= 0.0 || tick_size <= 0.0 || pip_size <= 0.0)
		return false;

	value_out = tick_value * (pip_size / tick_size);
	return (value_out > 0.0);
}

bool refresh_dynamic_pip_value_if_needed(void)
{
	if(!g_pip_value_dynamic)
		return true;

	double derived = 0.0;
	if(!derive_pip_value_per_lot_from_broker(g_pip_size, derived))
	{
		log_msg(1, "Failed to refresh dynamic pip_value_per_lot from broker");
		return false;
	}

	g_pip_value_per_lot = derived;
	return true;
}

bool detect_supported_symbol(void)
{
	string sym = normalize_symbol_name(Symbol());
	g_instrument_type = INSTRUMENT_UNKNOWN;
	g_pip_size = 0.0;
	g_pip_value_per_lot = 0.0;
	g_pip_value_dynamic = false;

	if(sym == "XAUUSD")
	{
		g_instrument_type = INSTRUMENT_GOLD;
		g_pip_size = 1.0;
		g_pip_value_per_lot = 100.0;
		g_pip_value_dynamic = false;
		return true;
	}

	// Constant USD pip value (10 USD/pip/lot) for common USD-quoted FX majors.
	if(symbol_is_one_of(sym, "EURUSD", "GBPUSD", "AUDUSD", "NZDUSD"))
	{
		g_instrument_type = INSTRUMENT_FX;
		g_pip_size = 0.0001;
		g_pip_value_per_lot = 10.0;
		g_pip_value_dynamic = false;
		return true;
	}

	// USDMXN uses 0.01 as one pip in this EA, so 10 pips means 0.10 in price.
	if(sym == "USDMXN")
	{
		g_instrument_type = INSTRUMENT_FX;
		g_pip_size = 0.01;
		g_pip_value_dynamic = true;
		if(!refresh_dynamic_pip_value_if_needed())
			return false;
		return true;
	}

	// USD base FX pairs requested by user: pip value varies with price, so derive from broker tick metadata.
	if(symbol_is_one_of(sym, "USDCHF", "USDCAD"))
	{
		g_instrument_type = INSTRUMENT_FX;
		g_pip_size = 0.0001;
		g_pip_value_dynamic = true;
		if(!refresh_dynamic_pip_value_if_needed())
			return false;
		return true;
	}

	// Nasdaq 100 CFDs/indices requested by user. Grid unit is 1 whole index point.
	// User calibration: 24900 -> 24800 yields +10 USD at 0.10 lot, so:
	// 100 points => 100 USD at 1.0 lot => 1 USD per point per 1.0 lot.
	if(symbol_is_one_of(sym, "US100", "NDXUSD", ".USTECHCash"))
	{
		g_instrument_type = INSTRUMENT_INDEX;
		g_pip_size = 1.0;
		g_pip_value_per_lot = 1.0;
		g_pip_value_dynamic = false;
		return true;
	}

	return false;
}

double mid_price(void)
{
	return (Bid + Ask) * 0.5;
}

int price_to_p(const double price)
{
	if(g_instrument_type == INSTRUMENT_GOLD || g_instrument_type == INSTRUMENT_INDEX)
		return (int)MathFloor(price + 1e-9);

	// FX prices may be quoted with fractional pips (5th/3rd digit). For grid logic
	// we intentionally ignore fractional pip noise and truncate to whole pips.
	double scaled = price / g_pip_size;
	if(scaled >= 0.0)
		return (int)MathFloor(scaled + 1e-9);
	return (int)MathCeil(scaled - 1e-9);
}

int p_to_slot_id(const int p)
{
	if(grid_step_pips <= 0)
		return p;
	return p / grid_step_pips;
}

int slot_low_p(const int slot_id)
{
	return slot_id * grid_step_pips;
}

int slot_high_p(const int slot_id)
{
	return slot_low_p(slot_id) + grid_step_pips - 1;
}

int clamped_slot_entry_offset(void)
{
	int max_offset = grid_step_pips - 1;
	if(max_offset < 0)
		max_offset = 0;
	if(slot_entry_offset_pips < 0)
		return 0;
	if(slot_entry_offset_pips > max_offset)
		return max_offset;
	return slot_entry_offset_pips;
}

int slot_target_p(const int slot_id)
{
	int low = slot_low_p(slot_id);
	int high = slot_high_p(slot_id);

	if(slot_entry_mode == SLOT_ENTRY_BEST_IN_SLOT)
		return (trade_direction == BUY_ONLY ? low : high);

	if(slot_entry_mode == SLOT_ENTRY_OFFSET)
	{
		int offset = clamped_slot_entry_offset();
		if(trade_direction == BUY_ONLY)
			return low + offset;
		return high - offset;
	}

	return (trade_direction == BUY_ONLY ? low : high);
}

bool slot_target_reached(const int slot_id, const int p_now)
{
	if(slot_entry_mode == SLOT_ENTRY_FIRST_TOUCH)
		return true;

	int target_p = slot_target_p(slot_id);
	if(trade_direction == BUY_ONLY)
		return (p_now <= target_p);
	return (p_now >= target_p);
}

bool order_type_matches_direction(const int type)
{
	if(type != OP_BUY && type != OP_SELL)
		return false;
	if(trade_direction == BUY_ONLY)
		return (type == OP_BUY);
	return (type == OP_SELL);
}

bool selected_order_is_managed_by_ea(void)
{
	if(!only_manage_my_own_orders)
		return true;
	return (OrderMagicNumber() == g_ea_magic);
}

bool included_selected_order(void)
{
	if(OrderSymbol() != Symbol())
		return false;
	if(!selected_order_is_managed_by_ea())
		return false;
	return order_type_matches_direction(OrderType());
}

bool int_array_contains(const int &arr[], const int value)
{
	int n = ArraySize(arr);
	for(int i = 0; i < n; ++i)
	{
		if(arr[i] == value)
			return true;
	}
	return false;
}

void int_array_add_unique(int &arr[], const int value)
{
	if(int_array_contains(arr, value))
		return;
	int n = ArraySize(arr);
	ArrayResize(arr, n + 1);
	arr[n] = value;
}

bool int_array_remove_value(int &arr[], const int value)
{
	int n = ArraySize(arr);
	for(int i = 0; i < n; ++i)
	{
		if(arr[i] != value)
			continue;
		for(int j = i; j < n - 1; ++j)
			arr[j] = arr[j + 1];
		ArrayResize(arr, n - 1);
		return true;
	}
	return false;
}

void reset_scan_cache(void)
{
	ArrayResize(g_filled_slots, 0);
	g_scan_has_orders = false;
	g_scan_p_extreme = 0;
	g_scan_existing_orders_lot_size = 0.0;
	g_scan_current_loss_usd = 0.0;
	g_scan_extreme_order_profitable = false;
}

void prune_pending_slots_against_filled(void)
{
	for(int i = ArraySize(g_pending_slots) - 1; i >= 0; --i)
	{
		if(int_array_contains(g_filled_slots, g_pending_slots[i]))
			int_array_remove_value(g_pending_slots, g_pending_slots[i]);
	}
}

void scan_open_orders(void)
{
	reset_scan_cache();

	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!included_selected_order())
			continue;

		double profit = OrderProfit();
		double lots = OrderLots();
		int p_order = price_to_p(OrderOpenPrice());
		int slot_id_order = p_to_slot_id(p_order);

		int_array_add_unique(g_filled_slots, slot_id_order);
		g_scan_existing_orders_lot_size += lots;
		if(profit < 0.0)
			g_scan_current_loss_usd += (-profit);

		if(!g_scan_has_orders)
		{
			g_scan_has_orders = true;
			g_scan_p_extreme = p_order;
			g_scan_extreme_order_profitable = (profit > 0.0);
		}
		else
		{
			if(trade_direction == BUY_ONLY)
			{
				if(p_order > g_scan_p_extreme)
				{
					g_scan_p_extreme = p_order;
					g_scan_extreme_order_profitable = (profit > 0.0);
				}
				else if(p_order == g_scan_p_extreme && profit > 0.0)
				{
					g_scan_extreme_order_profitable = true;
				}
			}
			else
			{
				if(p_order < g_scan_p_extreme)
				{
					g_scan_p_extreme = p_order;
					g_scan_extreme_order_profitable = (profit > 0.0);
				}
				else if(p_order == g_scan_p_extreme && profit > 0.0)
				{
					g_scan_extreme_order_profitable = true;
				}
			}
		}
	}

	g_last_timer_scan_time = TimeCurrent();
	prune_pending_slots_against_filled();
}

int lot_digits_from_step(const double step)
{
	if(step <= 0.0)
		return 2;
	double s = step;
	int digits = 0;
	while(digits < 8 && MathAbs(s - MathRound(s)) > 1e-8)
	{
		s *= 10.0;
		digits++;
	}
	return digits;
}

bool normalize_lot_size(const double lots_in, double &lots_out)
{
	lots_out = 0.0;
	if(lots_in <= 0.0)
		return false;

	double min_lot = MarketInfo(Symbol(), MODE_MINLOT);
	double max_lot = MarketInfo(Symbol(), MODE_MAXLOT);
	double lot_step = MarketInfo(Symbol(), MODE_LOTSTEP);
	if(min_lot <= 0.0)
		min_lot = 0.01;
	if(max_lot <= 0.0)
		max_lot = 100.0;
	if(lot_step <= 0.0)
		lot_step = min_lot;

	double candidate = MathMin(lots_in, max_lot);
	if(candidate < min_lot)
		candidate = min_lot;

	double steps = MathFloor((candidate - min_lot + 1e-12) / lot_step);
	double normalized = min_lot + steps * lot_step;
	if(normalized + 1e-12 < min_lot)
		normalized = min_lot;

	int digits = lot_digits_from_step(lot_step);
	lots_out = NormalizeDouble(normalized, digits);
	return (lots_out > 0.0);
}

void set_pause_state(const bool pause, const pause_reason_t reason, const string context_msg = "")
{
	bool changed = (g_entries_paused != pause);
	bool reason_changed = (g_pause_reason != reason);

	g_entries_paused = pause;
	g_pause_reason = (pause ? reason : PAUSE_NONE);

	if(!changed && !reason_changed)
		return;

	if(pause)
	{
		string msg = "Entries paused: reason=" + pause_reason_to_string(reason);
		if(context_msg != "")
			msg += ", " + context_msg;
		log_msg(1, msg);
	}
	else
	{
		string msg2 = "Entries resumed";
		if(context_msg != "")
			msg2 += ": " + context_msg;
		log_msg(1, msg2);
	}
}

void update_prev_slot_seed_if_needed(void)
{
	if(g_prev_slot_id_initialized)
		return;
	int p_now = price_to_p(mid_price());
	g_prev_slot_id = p_to_slot_id(p_now);
	g_prev_slot_id_initialized = true;
}

void log_debug_metrics(const string prefix, const int p_now, const int slot_id_now)
{
	if(log_level < 2)
		return;

	int p_extreme = p_now;
	if(g_scan_has_orders)
		p_extreme = g_scan_p_extreme;
	int dist_pips = adverse_retracement_from_extreme(p_now);
	int remaining_pips = maximum_volatility_pips - dist_pips;

	log_msg(2,
		prefix +
		" P_now=" + IntegerToString(p_now) +
		", slot_id_now=" + IntegerToString(slot_id_now) +
			", P_extreme=" + IntegerToString(p_extreme) +
			", dist_pips=" + IntegerToString(dist_pips) +
			", remaining_pips=" + IntegerToString(remaining_pips) +
			", current_loss_usd=" + DoubleToString(g_scan_current_loss_usd, 2) +
			", loss_budget=" + DoubleToString(g_loss_budget, 2) +
				", grid_order_size_active=" + DoubleToString(g_grid_order_size_active, 2));
}

int adverse_retracement_from_extreme(const int p_now)
{
	if(!g_scan_has_orders)
		return 0;

	if(trade_direction == BUY_ONLY)
	{
		if(p_now < g_scan_p_extreme)
			return (g_scan_p_extreme - p_now);
		return 0;
	}

	if(p_now > g_scan_p_extreme)
		return (p_now - g_scan_p_extreme);
	return 0;
}

int adverse_distance_from_extreme(const int p_now)
{
	return adverse_retracement_from_extreme(p_now);
}

double price_distance_to_pips(const double price_distance)
{
	if(g_pip_size <= 0.0)
		return 0.0;
	return price_distance / g_pip_size;
}

double pips_to_price(const double pips)
{
	return pips * g_pip_size;
}

double normalize_price_value(const double price)
{
	return NormalizeDouble(price, Digits);
}

double minimum_stop_distance_price(void)
{
	double stop_level_points = MarketInfo(Symbol(), MODE_STOPLEVEL);
	double freeze_level_points = MarketInfo(Symbol(), MODE_FREEZELEVEL);
	double min_points = MathMax(stop_level_points, freeze_level_points);
	if(min_points < 0.0)
		min_points = 0.0;
	return min_points * Point;
}

bool trade_modify_stop_with_retries(const int ticket, const double stop_loss)
{
	for(int attempt = 1; attempt <= g_trade_retries; ++attempt)
	{
		if(!OrderSelect(ticket, SELECT_BY_TICKET, MODE_TRADES))
		{
			log_msg(1, "OrderModify select failed ticket=" + IntegerToString(ticket));
			return false;
		}

		ResetLastError();
		if(OrderModify(ticket, OrderOpenPrice(), stop_loss, OrderTakeProfit(), 0, clrNONE))
			return true;

		int err = GetLastError();
		log_msg(1,
			"OrderModify failed ticket=" + IntegerToString(ticket) +
			", attempt=" + IntegerToString(attempt) +
			", err=" + IntegerToString(err));
		if(attempt < g_trade_retries)
			Sleep(g_trade_retry_sleep_ms);
	}
	return false;
}

bool protective_stop_enabled(void)
{
	return (protective_stop_after_pips >= 0.0);
}

double selected_order_favorable_move_pips(void)
{
	int type = OrderType();
	double market_price = 0.0;
	if(type == OP_BUY)
		market_price = Bid;
	else if(type == OP_SELL)
		market_price = Ask;
	else
		return 0.0;

	if(type == OP_BUY)
		return price_distance_to_pips(market_price - OrderOpenPrice());
	return price_distance_to_pips(OrderOpenPrice() - market_price);
}

bool selected_order_protective_stop_triggered(void)
{
	if(!protective_stop_enabled())
		return false;
	return (selected_order_favorable_move_pips() >= protective_stop_after_pips);
}

bool selected_order_protective_stop_price(double &stop_loss_out)
{
	stop_loss_out = 0.0;
	int type = OrderType();
	if(type != OP_BUY && type != OP_SELL)
		return false;

	double open_price = OrderOpenPrice();
	double candidate = open_price;
	if(type == OP_BUY)
		candidate += pips_to_price(protective_stop_delta);
	else
		candidate -= pips_to_price(protective_stop_delta);

	double min_stop_distance = minimum_stop_distance_price();
	if(type == OP_BUY)
	{
		double highest_allowed = normalize_price_value(Bid - min_stop_distance);
		if(candidate > highest_allowed)
			candidate = highest_allowed;
		if(candidate <= open_price)
			return false;
	}
	else
	{
		double lowest_allowed = normalize_price_value(Ask + min_stop_distance);
		if(candidate < lowest_allowed)
			candidate = lowest_allowed;
		if(candidate >= open_price)
			return false;
	}

	stop_loss_out = normalize_price_value(candidate);
	return true;
}

void apply_protective_stops(void)
{
	if(!protective_stop_enabled())
		return;

	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!included_selected_order())
			continue;
		if(!selected_order_protective_stop_triggered())
			continue;

		double stop_loss_target = 0.0;
		if(!selected_order_protective_stop_price(stop_loss_target))
			continue;

		double current_stop_loss = OrderStopLoss();
		if(OrderType() == OP_BUY)
		{
			if(current_stop_loss > 0.0 && current_stop_loss >= stop_loss_target)
				continue;
		}
		else
		{
			if(current_stop_loss > 0.0 && current_stop_loss <= stop_loss_target)
				continue;
		}

		int ticket = OrderTicket();
		double favorable_move_pips = selected_order_favorable_move_pips();
		if(trade_modify_stop_with_retries(ticket, stop_loss_target))
		{
			log_msg(1,
				"Protective stop set ticket=" + IntegerToString(ticket) +
				", favorable_move_pips=" + DoubleToString(favorable_move_pips, 2) +
				", stop_loss=" + DoubleToString(stop_loss_target, Digits));
		}
	}
}

bool is_directional_candle(const int shift)
{
	if(shift <= 0 || shift >= Bars)
		return false;

	if(trade_direction == BUY_ONLY)
		return (Close[shift] > Open[shift]);
	return (Close[shift] < Open[shift]);
}

bool is_opposite_direction_candle(const int shift)
{
	if(shift <= 0 || shift >= Bars)
		return false;

	if(trade_direction == BUY_ONLY)
		return (Close[shift] < Open[shift]);
	return (Close[shift] > Open[shift]);
}

bool close_breaks_structure_level(const double close_price, const double break_level)
{
	if(trade_direction == BUY_ONLY)
		return (close_price < break_level);
	return (close_price > break_level);
}

bool structural_break_tp_signal(string &reason_out)
{
	reason_out = "";
	if(!enable_tp_structural_break)
		return false;
	if(Bars <= 2)
		return false;

	int directional_shifts[];
	ArrayResize(directional_shifts, 0);

	double break_level = 0.0;
	bool break_level_valid = false;
	int valid_directional_count = 0;

	for(int shift = 2; shift < Bars; ++shift)
	{
		if(is_directional_candle(shift))
		{
			int n = ArraySize(directional_shifts);
			ArrayResize(directional_shifts, n + 1);
			directional_shifts[n] = shift;
			int directional_count = ArraySize(directional_shifts);
			if(directional_count >= directional_reference_depth)
			{
				int reference_shift = directional_shifts[directional_reference_depth - 1];
				break_level = Open[reference_shift];
				break_level_valid = true;
				valid_directional_count = directional_count;
			}
			continue;
		}

		if(!break_level_valid)
			continue;

		// Older pullback candles terminate the active run only after the structural
		// reference level has been established from the most recent directional candles.
		if(is_opposite_direction_candle(shift) && close_breaks_structure_level(Close[shift], break_level))
			break;
	}

	if(!break_level_valid)
		return false;
	if(valid_directional_count < trend_run_candles_to_arm_tp)
		return false;
	if(!close_breaks_structure_level(Close[1], break_level))
		return false;

	reason_out = "structural_break";
	return true;
}

bool low_volatility_tp_signal(string &reason_out)
{
	reason_out = "";
	if(!enable_tp_low_volatility)
		return false;
	if(Bars <= low_volatility_lookback_candles)
		return false;

	double sum_body_pips = 0.0;
	for(int shift = 1; shift <= low_volatility_lookback_candles; ++shift)
		sum_body_pips += MathAbs(price_distance_to_pips(Close[shift] - Open[shift]));

	if(sum_body_pips >= low_volatility_total_body_pips_threshold)
		return false;

	reason_out = "low_volatility";
	return true;
}

bool wick_expansion_tp_signal(string &reason_out)
{
	reason_out = "";
	if(!enable_tp_wick_expansion)
		return false;
	if(Bars <= wick_expansion_lookback_candles)
		return false;

	double sum_wickiness_pips = 0.0;
	double sum_body_pips = 0.0;
	for(int shift = 1; shift <= wick_expansion_lookback_candles; ++shift)
	{
		double range_pips = MathAbs(price_distance_to_pips(High[shift] - Low[shift]));
		double body_pips = MathAbs(price_distance_to_pips(Close[shift] - Open[shift]));
		sum_wickiness_pips += (range_pips - body_pips);
		sum_body_pips += body_pips;
	}

	if(sum_wickiness_pips <= sum_body_pips)
		return false;

	reason_out = "wick_expansion";
	return true;
}

bool should_take_profits_now(string &reason_out)
{
	reason_out = "";
	if(!g_scan_has_orders)
		return false;

	int profitable_orders = 0;
	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!included_selected_order())
			continue;
		if(OrderProfit() > 0.0 && selected_order_meets_take_profit_floor())
			profitable_orders++;
	}

	if(profitable_orders < min_profitable_orders_to_close)
		return false;

	if(structural_break_tp_signal(reason_out))
		return true;
	if(low_volatility_tp_signal(reason_out))
		return true;
	if(wick_expansion_tp_signal(reason_out))
		return true;

	return false;
}

bool tp_signal_already_used_for_current_bar(void)
{
	if(Bars <= 1)
		return false;
	return (g_last_tp_signal_bar_time == Time[1]);
}

double minimum_take_profit_pips(void)
{
	return ((double)grid_step_pips) * 0.5;
}

double selected_order_profit_pips(void)
{
	int type = OrderType();
	double close_price = 0.0;
	if(type == OP_BUY)
		close_price = Bid;
	else if(type == OP_SELL)
		close_price = Ask;
	else
		return 0.0;

	if(type == OP_BUY)
		return price_distance_to_pips(close_price - OrderOpenPrice());
	return price_distance_to_pips(OrderOpenPrice() - close_price);
}

bool selected_order_meets_take_profit_floor(void)
{
	return (selected_order_profit_pips() >= minimum_take_profit_pips());
}

bool trade_open_precheck(const int type, const double lots, int &error_out, string &detail_out)
{
	error_out = 0;
	detail_out = "";

	if(IsTesting())
		return true;

	if(!IsTradeAllowed())
	{
		error_out = 133;
		detail_out = "trade_not_allowed";
		return false;
	}

	if(MarketInfo(Symbol(), MODE_TRADEALLOWED) <= 0.0)
	{
		error_out = 133;
		detail_out = "symbol_trade_disabled";
		return false;
	}

	ResetLastError();
	double free_margin_after = AccountFreeMarginCheck(Symbol(), type, lots);
	int margin_err = GetLastError();
	if(margin_err == 134 || free_margin_after <= 0.0)
	{
		error_out = (margin_err != 0 ? margin_err : 134);
		detail_out = "free_margin_after=" + DoubleToString(free_margin_after, 2);
		return false;
	}

	return true;
}

bool order_send_error_is_hard_stop(const int err)
{
	return (err == 133 || err == 134);
}

bool trade_send_with_retries(const int type, const double lots, int &ticket_out)
{
	ticket_out = -1;
	int precheck_err = 0;
	string precheck_detail = "";
	if(!trade_open_precheck(type, lots, precheck_err, precheck_detail))
	{
		set_pause_state(true, PAUSE_TRADE_BLOCKED,
			"err=" + IntegerToString(precheck_err) +
			", " + precheck_detail);
		return false;
	}

	for(int attempt = 1; attempt <= g_trade_retries; ++attempt)
	{
		RefreshRates();
		double price = (type == OP_BUY ? Ask : Bid);
		ResetLastError();
		int ticket = OrderSend(Symbol(), type, lots, price, g_trade_slippage_points, 0.0, 0.0, "Buldozer", g_ea_magic, 0, clrNONE);
		if(ticket > 0)
		{
			ticket_out = ticket;
			return true;
		}

		int err = GetLastError();
		if(order_send_error_is_hard_stop(err))
		{
			set_pause_state(true, PAUSE_TRADE_BLOCKED,
				"err=" + IntegerToString(err) +
				", attempt=" + IntegerToString(attempt));
			return false;
		}

		log_msg(1, "OrderSend failed attempt=" + IntegerToString(attempt) + ", err=" + IntegerToString(err));
		if(attempt < g_trade_retries)
			Sleep(g_trade_retry_sleep_ms);
	}
	return false;
}

bool trade_close_with_retries(const int ticket, const double lots, const int type)
{
	for(int attempt = 1; attempt <= g_trade_retries; ++attempt)
	{
		if(!OrderSelect(ticket, SELECT_BY_TICKET, MODE_TRADES))
		{
			log_msg(1, "OrderClose select failed ticket=" + IntegerToString(ticket));
			return false;
		}

		RefreshRates();
		double close_price = (type == OP_BUY ? Bid : Ask);
		ResetLastError();
		if(OrderClose(ticket, lots, close_price, g_trade_slippage_points, clrNONE))
			return true;

		int err = GetLastError();
		log_msg(1,
			"OrderClose failed ticket=" + IntegerToString(ticket) +
			", attempt=" + IntegerToString(attempt) +
			", err=" + IntegerToString(err));
		if(attempt < g_trade_retries)
			Sleep(g_trade_retry_sleep_ms);
	}
	return false;
}

void recompute_pause_from_current_market(void)
{
	double mid = mid_price();
	int p_now = price_to_p(mid);
	int p_extreme = p_now;
	if(g_scan_has_orders)
		p_extreme = g_scan_p_extreme;

	double existing_lots = g_scan_existing_orders_lot_size;
	double current_loss_usd = g_scan_current_loss_usd;
	int dist_pips = adverse_retracement_from_extreme(p_now);
	int remaining_pips = maximum_volatility_pips - dist_pips;
	double budget_for_new_positions = g_loss_budget - ((double)remaining_pips) * g_pip_value_per_lot * existing_lots;
	bool on_counter_trend_side = (dist_pips > 0);

	if(log_level >= 2)
	{
		log_msg(2,
			"Timer metrics: P_now=" + IntegerToString(p_now) +
			", P_extreme=" + IntegerToString(p_extreme) +
			", dist_pips=" + IntegerToString(dist_pips) +
			", remaining_pips=" + IntegerToString(remaining_pips) +
			", current_loss_usd=" + DoubleToString(current_loss_usd, 2) +
			", loss_budget=" + DoubleToString(g_loss_budget, 2) +
			", existing_lots=" + DoubleToString(existing_lots, 2) +
			", budget_for_new_positions=" + DoubleToString(budget_for_new_positions, 2));
	}

	if(current_loss_usd > g_loss_budget)
	{
		set_pause_state(true, PAUSE_LOSS_BUDGET,
			"current_loss_usd=" + DoubleToString(current_loss_usd, 2) +
			", loss_budget=" + DoubleToString(g_loss_budget, 2));
		return;
	}
	if(dist_pips > maximum_volatility_pips)
	{
		set_pause_state(true, PAUSE_MAX_VOLATILITY,
			"dist_pips=" + IntegerToString(dist_pips) +
			", max=" + IntegerToString(maximum_volatility_pips));
		return;
	}
	if(remaining_pips <= 0)
	{
		set_pause_state(true, PAUSE_REMAINING_PIPS,
			"remaining_pips=" + IntegerToString(remaining_pips));
		return;
	}
	if(on_counter_trend_side && budget_for_new_positions <= 0.0)
	{
		set_pause_state(true, PAUSE_BUDGET_FOR_NEW,
			"budget_for_new_positions=" + DoubleToString(budget_for_new_positions, 2) +
			", mid_price=" + DoubleToString(mid, Digits));
		return;
	}

	int trade_precheck_err = 0;
	string trade_precheck_detail = "";
	if(!trade_open_precheck(matching_order_type(), g_grid_order_size_active, trade_precheck_err, trade_precheck_detail))
	{
		set_pause_state(true, PAUSE_TRADE_BLOCKED,
			"err=" + IntegerToString(trade_precheck_err) +
			", " + trade_precheck_detail);
		return;
	}

	set_pause_state(false, PAUSE_NONE,
		"size=" + DoubleToString(g_grid_order_size_active, 2) +
		", mid_price=" + DoubleToString(mid, Digits));
}

void execute_incremental_reset(void)
{
	double profit_closed = 0.0;
	int total_before = OrdersTotal();

	for(int i = total_before - 1; i >= 0; --i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!included_selected_order())
			continue;
		if(OrderProfit() <= 0.0)
			continue;
		if(!selected_order_meets_take_profit_floor())
			continue;

		int ticket = OrderTicket();
		if(ignore_ticket_contains(ticket))
		{
			log_msg(1, "order [" + IntegerToString(ticket) + "] is on ignore list, will not close it during incremental trade reset");
			continue;
		}

		int type = OrderType();
		double lots = OrderLots();
		double profit_now = OrderProfit();
		double profit_pips_now = selected_order_profit_pips();

		if(trade_close_with_retries(ticket, lots, type))
		{
			profit_closed += profit_now;
			log_msg(1,
				"Reset closed winner ticket=" + IntegerToString(ticket) +
				", profit=" + DoubleToString(profit_now, 2) +
				", profit_pips=" + DoubleToString(profit_pips_now, 1));
		}
	}

	g_loss_budget += profit_closed;

	scan_open_orders();

	int p_now = price_to_p(mid_price());
	int p_extreme = p_now;
	double existing_lots = 0.0;
	if(g_scan_has_orders)
	{
		p_extreme = g_scan_p_extreme;
		existing_lots = g_scan_existing_orders_lot_size;
	}

	int dist_pips = adverse_retracement_from_extreme(p_now);
	int remaining_pips = maximum_volatility_pips - dist_pips;
	double budget_for_new_positions = g_loss_budget - ((double)remaining_pips) * g_pip_value_per_lot * existing_lots;

	bool feasible = (remaining_pips > 0 && budget_for_new_positions > 0.0);
	pause_reason_t reset_pause_reason = PAUSE_NONE;

	if(!feasible && remaining_pips <= 0)
	{
		reset_pause_reason = PAUSE_REMAINING_PIPS;
	}
	else if(!feasible)
	{
		reset_pause_reason = PAUSE_BUDGET_FOR_NEW;
	}

	if(!feasible)
		set_pause_state(true, reset_pause_reason, "reset recompute infeasible");

	string summary =
		"Reset summary: profit_closed=" + DoubleToString(profit_closed, 2) +
		", loss_budget=" + DoubleToString(g_loss_budget, 2) +
		", remaining_pips=" + IntegerToString(remaining_pips) +
		", existing_orders_lot_size=" + DoubleToString(existing_lots, 2) +
		", size_static=" + DoubleToString(g_grid_order_size_active, 2) +
		", entries_paused=" + (g_entries_paused ? "true" : "false");

	log_msg(1, summary);
}

void run_timer_maintenance(void)
{
	if(!g_state_ready)
		return;

	g_last_maintenance_run_time = TimeCurrent();
	RefreshRates();
	if(!refresh_dynamic_pip_value_if_needed())
		log_msg(1, "Proceeding with last pip_value_per_lot=" + DoubleToString(g_pip_value_per_lot, 4));
	scan_open_orders();
	apply_protective_stops();

	string tp_reason = "";
	if(should_take_profits_now(tp_reason) && !tp_signal_already_used_for_current_bar())
	{
		log_msg(1,
			"TP triggered by " + tp_reason +
			", bar_time=" + TimeToString(Time[1], TIME_DATE | TIME_MINUTES));
		execute_incremental_reset();
		g_last_tp_signal_bar_time = Time[1];
	}

	recompute_pause_from_current_market();
}

bool fallback_timer_due(void)
{
	datetime now_t = TimeCurrent();
	if(now_t <= 0)
		return false;

	if(g_last_maintenance_run_time <= 0)
		return true;

	if((now_t - g_last_maintenance_run_time) >= g_timer_period_seconds)
		return true;

	return false;
}

void maybe_run_timer_fallback_from_tick(void)
{
	if(g_timer_available)
		return;
	if(!g_state_ready)
		return;
	if(!fallback_timer_due())
		return;

	run_timer_maintenance();
}

bool stop_gate_blocks_entry(const int p_now)
{
	if(g_scan_current_loss_usd > g_loss_budget)
	{
		set_pause_state(true, PAUSE_LOSS_BUDGET,
			"current_loss_usd=" + DoubleToString(g_scan_current_loss_usd, 2) +
			", loss_budget=" + DoubleToString(g_loss_budget, 2));
		return true;
	}

	if(g_scan_has_orders)
	{
		int dist_pips = adverse_retracement_from_extreme(p_now);
		if(dist_pips > maximum_volatility_pips)
		{
			set_pause_state(true, PAUSE_MAX_VOLATILITY,
				"dist_pips=" + IntegerToString(dist_pips) +
				", max=" + IntegerToString(maximum_volatility_pips));
			return true;
		}
	}

	return false;
}

int matching_order_type(void)
{
	if(trade_direction == BUY_ONLY)
		return OP_BUY;
	return OP_SELL;
}

void maybe_update_cache_after_successful_open(const int slot_id)
{
	int_array_add_unique(g_filled_slots, slot_id);
}

void run_entry_engine(void)
{
	if(!g_state_ready)
		return;
	if(only_take_profit_mode)
		return;
	if(g_entries_paused)
		return;
	if(g_max_gap_orders_per_tick <= 0)
		return;
	if(grid_step_pips <= 0)
		return;
	if(g_grid_order_size_active <= 0.0)
		return;

	RefreshRates();
	int p_now = price_to_p(mid_price());
	int slot_id_now = p_to_slot_id(p_now);
	log_debug_metrics("Tick", p_now, slot_id_now);

	if(!g_prev_slot_id_initialized)
	{
		g_prev_slot_id = slot_id_now;
		g_prev_slot_id_initialized = true;
		return;
	}

	if(slot_id_now != g_prev_slot_id)
	{
		int slot_cursor = g_prev_slot_id;
		int direction_step = (slot_id_now > g_prev_slot_id ? 1 : -1);
		while(slot_cursor != slot_id_now)
		{
			slot_cursor += direction_step;
			if(int_array_contains(g_filled_slots, slot_cursor))
				continue;
			int_array_add_unique(g_pending_slots, slot_cursor);
		}
	}

	int opened_slots_this_tick[];
	ArrayResize(opened_slots_this_tick, 0);
	int opened_count = 0;

	for(int i = 0; i < ArraySize(g_pending_slots); ++i)
	{
		int slot_candidate = g_pending_slots[i];

			if(stop_gate_blocks_entry(p_now))
				break;

			if(int_array_contains(g_filled_slots, slot_candidate) || int_array_contains(opened_slots_this_tick, slot_candidate))
				continue;
			if(!slot_target_reached(slot_candidate, p_now))
				continue;

			int maximum_volatility_remaining_pips = maximum_volatility_pips;
			if(g_scan_has_orders)
			{
				int dist_pips = adverse_retracement_from_extreme(p_now);
				maximum_volatility_remaining_pips = maximum_volatility_pips - dist_pips;
			}

			int ticket = -1;
			if(trade_send_with_retries(matching_order_type(), g_grid_order_size_active, ticket))
			{
				maybe_update_cache_after_successful_open(slot_candidate);
				int_array_add_unique(opened_slots_this_tick, slot_candidate);
				opened_count++;

				double fill_price = 0.0;
				if(OrderSelect(ticket, SELECT_BY_TICKET, MODE_TRADES))
					fill_price = OrderOpenPrice();

				string extra = "";
				if(slot_entry_mode != SLOT_ENTRY_FIRST_TOUCH)
				{
					extra =
						", mode=" + slot_entry_mode_to_string(slot_entry_mode) +
						", target_p=" + IntegerToString(slot_target_p(slot_candidate));
				}

				log_msg(1,
					"Opened order: slot_id=" + IntegerToString(slot_candidate) +
					", lots=" + DoubleToString(g_grid_order_size_active, 2) +
					", price=" + DoubleToString((fill_price > 0.0 ? fill_price : (matching_order_type() == OP_BUY ? Ask : Bid)), Digits) +
					extra +
					", maximum_volatility_reminder=" + IntegerToString(maximum_volatility_remaining_pips) + " pips" +
					", sum_of_losses=[" + DoubleToString(g_scan_current_loss_usd, 2) + "]" +
					", loss_budget=[" + DoubleToString(g_loss_budget, 2) + "]" +
					", ticket=" + IntegerToString(ticket));
			}
			else
			{
				log_msg(1, "Order open aborted for slot_id=" + IntegerToString(slot_candidate));
				break;
			}

		if(opened_count >= g_max_gap_orders_per_tick)
		{
			log_msg(1, "Gap capped by g_max_gap_orders_per_tick; remaining crossed/pending slots deferred");
			break;
		}
	}

	for(int j = ArraySize(opened_slots_this_tick) - 1; j >= 0; --j)
		int_array_remove_value(g_pending_slots, opened_slots_this_tick[j]);

	g_prev_slot_id = slot_id_now;
}

int OnInit(void)
{
	if(grid_step_pips <= 0)
	{
		log_msg(1, "Init failed: grid_step_pips must be > 0");
		return INIT_FAILED;
	}
	if(maximum_volatility_pips <= 0)
	{
		log_msg(1, "Init failed: maximum_volatility_pips must be > 0");
		return INIT_FAILED;
	}
	if(initial_order_lot_size <= 0.0)
	{
		log_msg(1, "Init failed: initial_order_lot_size must be > 0");
		return INIT_FAILED;
	}
	if(protective_stop_after_pips < -1.0)
	{
		log_msg(1, "Init failed: protective_stop_after_pips must be >= -1");
		return INIT_FAILED;
	}
	if(protective_stop_after_pips >= 0.0 && protective_stop_delta <= 0.0)
	{
		log_msg(1, "Init failed: protective_stop_delta must be > 0 when protective stop is enabled");
		return INIT_FAILED;
	}
	if(g_timer_period_seconds <= 0)
	{
		log_msg(1, "Init failed: g_timer_period_seconds must be > 0");
		return INIT_FAILED;
	}
	if(trend_run_candles_to_arm_tp < 2)
	{
		log_msg(1, "Init failed: trend_run_candles_to_arm_tp must be >= 2");
		return INIT_FAILED;
	}
	if(directional_reference_depth < 2)
	{
		log_msg(1, "Init failed: directional_reference_depth must be >= 2");
		return INIT_FAILED;
	}
	if(directional_reference_depth > trend_run_candles_to_arm_tp)
	{
		log_msg(1, "Init failed: directional_reference_depth must be <= trend_run_candles_to_arm_tp");
		return INIT_FAILED;
	}
	if(low_volatility_lookback_candles <= 0)
	{
		log_msg(1, "Init failed: low_volatility_lookback_candles must be > 0");
		return INIT_FAILED;
	}
	if(low_volatility_total_body_pips_threshold < 0.0)
	{
		log_msg(1, "Init failed: low_volatility_total_body_pips_threshold must be >= 0");
		return INIT_FAILED;
	}
	if(wick_expansion_lookback_candles <= 0)
	{
		log_msg(1, "Init failed: wick_expansion_lookback_candles must be > 0");
		return INIT_FAILED;
	}
	if(min_profitable_orders_to_close <= 0)
	{
		log_msg(1, "Init failed: min_profitable_orders_to_close must be > 0");
		return INIT_FAILED;
	}

	if(!detect_supported_symbol())
	{
		log_msg(1, "Init failed: unsupported symbol '" + Symbol() + "'");
		return INIT_FAILED;
	}

	double init_lots_normalized = 0.0;
	if(!normalize_lot_size(initial_order_lot_size, init_lots_normalized))
	{
		log_msg(1, "Init failed: initial_order_lot_size cannot be normalized for broker");
		return INIT_FAILED;
	}

	g_loss_budget = loss_budget_initial;
	g_entries_paused = false;
	g_grid_order_size_active = init_lots_normalized;
	g_grid_order_size_last_valid = init_lots_normalized;
	g_last_tp_signal_bar_time = 0;
	g_prev_slot_id_initialized = false;
	g_pause_reason = PAUSE_NONE;
	reset_scan_cache();
	ArrayResize(g_pending_slots, 0);
	parse_ignore_tickets();

	g_timer_available = EventSetTimer(g_timer_period_seconds);
	if(!g_timer_available)
		log_msg(1, "EventSetTimer failed, using OnTick timer fallback (Strategy Tester compatible)");

	g_state_ready = true;

	log_msg(1,
		"Init success: symbol=" + Symbol() +
		", direction=" + direction_to_string() +
		", only_manage_my_own_orders=" + (only_manage_my_own_orders ? "true" : "false") +
		", protective_stop_after_pips=" + DoubleToString(protective_stop_after_pips, 2) +
		", protective_stop_delta=" + DoubleToString(protective_stop_delta, 2) +
		", pip_size=" + DoubleToString(g_pip_size, 5) +
		", pip_value_per_lot=" + DoubleToString(g_pip_value_per_lot, 2) +
		", pip_value_source=" + (g_pip_value_dynamic ? "broker_tickvalue" : "hardcoded") +
		", slot_entry_mode=" + slot_entry_mode_to_string(slot_entry_mode) +
		", slot_entry_offset_pips=" + IntegerToString(slot_entry_offset_pips) +
		", tp_structural_break=" + (enable_tp_structural_break ? "true" : "false") +
		", tp_low_volatility=" + (enable_tp_low_volatility ? "true" : "false") +
		", tp_wick_expansion=" + (enable_tp_wick_expansion ? "true" : "false") +
		", initial_size=" + DoubleToString(g_grid_order_size_active, 2));

	run_timer_maintenance();
	update_prev_slot_seed_if_needed();
	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	if(g_timer_available)
		EventKillTimer();
	log_msg(1, "Deinit reason=" + IntegerToString(reason));
}

void OnTick(void)
{
	maybe_run_timer_fallback_from_tick();
	run_entry_engine();
}

void OnTimer(void)
{
	g_last_maintenance_run_time = TimeCurrent();
	run_timer_maintenance();
}
