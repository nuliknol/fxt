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

enum contraction_mode_t
{
	NONE = 0,
	CANDLES = 1,
	PRICE = 2
};

input trade_direction_t trade_direction = BUY_ONLY;
input double order_lot_size = 0.01;
input int grid_step_pips = 10;
input bool only_take_profit_mode = false;
input bool only_manage_my_own_orders = false;
input string ignore_tickets_csv = "";
input double trailing_stop_pips = 10.0;
input double trailing_stop_contraction_rate = 1.0;
input contraction_mode_t contraction_mode = NONE;
input int contraction_interval = 0;
input int log_level = 1;

instrument_type_t g_instrument_type = INSTRUMENT_UNKNOWN;
double g_pip_size = 0.0;
double g_order_lot_size_active = 0.0;
int g_prev_slot_id = 0;
bool g_prev_slot_id_initialized = false;
bool g_state_ready = false;
int g_sequence_contraction_steps = 0;
double g_effective_trailing_stop_pips = 0.0;
int g_last_contraction_steps = -1;
double g_last_effective_trailing_stop_pips = -1.0;
datetime g_contraction_reset_time = 0;
bool g_contraction_anchor_valid = false;
double g_contraction_anchor_price = 0.0;
datetime g_contraction_anchor_time = 0;
double g_contraction_anchor_net_profit = 0.0;
datetime g_last_history_close_time = 0;
int g_last_history_ticket = 0;

int g_filled_slots[];
int g_pending_slots[];
int g_ignore_tickets[];

int g_trade_retries = 10;
int g_trade_retry_sleep_ms = 250;
int g_trade_slippage_points = 20;
int g_ea_magic = 20260421;
int g_max_gap_orders_per_tick = 5;

void log_msg(const int level, const string msg)
{
	if(log_level >= level)
		Print("Trailer: " + msg);
}

string direction_to_string(void)
{
	if(trade_direction == BUY_ONLY)
		return "BUY_ONLY";
	return "SELL_ONLY";
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

string order_comment_for_slot(const int slot_id)
{
	return "Trailer slot=" + IntegerToString(slot_id);
}

bool try_parse_slot_id_from_comment(const string comment, int &slot_id_out)
{
	slot_id_out = 0;

	int marker_pos = StringFind(comment, "slot=");
	if(marker_pos < 0)
		return false;

	int start = marker_pos + StringLen("slot=");
	int len = StringLen(comment);
	if(start >= len)
		return false;

	int end = start;
	if(StringGetCharacter(comment, end) == '-')
		end++;

	while(end < len)
	{
		int ch = StringGetCharacter(comment, end);
		if(ch < '0' || ch > '9')
			break;
		end++;
	}

	if(end <= start || (end == start + 1 && StringGetCharacter(comment, start) == '-'))
		return false;

	string token = StringSubstr(comment, start, end - start);
	return try_parse_int(token, slot_id_out);
}

bool symbol_is_one_of(const string sym, const string a, const string b = "", const string c = "", const string d = "", const string e = "", const string f = "")
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
	if(f != "" && sym == f)
		return true;
	return false;
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

string normalize_symbol_name(const string sym)
{
	int len = StringLen(sym);
	if(len > 2 && StringSubstr(sym, len - 2) == "_i")
		return StringSubstr(sym, 0, len - 2);
	return sym;
}

bool detect_supported_symbol(void)
{
	string sym = normalize_symbol_name(Symbol());
	g_instrument_type = INSTRUMENT_UNKNOWN;
	g_pip_size = 0.0;

	if(sym == "XAUUSD")
	{
		g_instrument_type = INSTRUMENT_GOLD;
		g_pip_size = 1.0;
		return true;
	}

	// Constant USD pip value symbols from Buldozer. Trailer only needs pip size.
	if(symbol_is_one_of(sym, "EURUSD", "GBPUSD", "AUDUSD", "NZDUSD"))
	{
		g_instrument_type = INSTRUMENT_FX;
		g_pip_size = 0.0001;
		return true;
	}

	// USDMXN uses 0.01 as one pip in Buldozer, so 10 pips means 0.10 in price.
	if(sym == "USDMXN")
	{
		g_instrument_type = INSTRUMENT_FX;
		g_pip_size = 0.01;
		return true;
	}

	// USD base FX pairs from Buldozer.
	if(symbol_is_one_of(sym, "USDCHF", "USDCAD"))
	{
		g_instrument_type = INSTRUMENT_FX;
		g_pip_size = 0.0001;
		return true;
	}

	// Nasdaq 100 CFDs/indices from Buldozer. Grid unit is 1 whole index point.
	if(symbol_is_one_of(sym, "US100", "NDXUSD", ".USTECHCash"))
	{
		g_instrument_type = INSTRUMENT_INDEX;
		g_pip_size = 1.0;
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

int slot_target_p(const int slot_id)
{
	if(trade_direction == BUY_ONLY)
		return slot_low_p(slot_id);
	return slot_high_p(slot_id);
}

bool slot_target_reached(const int slot_id, const int p_now)
{
	int target_p = slot_target_p(slot_id);
	if(trade_direction == BUY_ONLY)
		return (p_now <= target_p);
	return (p_now >= target_p);
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

int matching_order_type(void)
{
	if(trade_direction == BUY_ONLY)
		return OP_BUY;
	return OP_SELL;
}

bool order_type_matches_direction(const int type)
{
	if(type != OP_BUY && type != OP_SELL)
		return false;
	if(trade_direction == BUY_ONLY)
		return (type == OP_BUY);
	return (type == OP_SELL);
}

bool selected_order_is_my_order(void)
{
	return (OrderMagicNumber() == g_ea_magic);
}

bool selected_order_is_managed_by_ea(void)
{
	if(!only_manage_my_own_orders)
		return true;
	return selected_order_is_my_order();
}

bool selected_order_matches_scope(void)
{
	if(OrderSymbol() != Symbol())
		return false;
	if(!selected_order_is_managed_by_ea())
		return false;
	return order_type_matches_direction(OrderType());
}

bool selected_order_is_trailing_managed(void)
{
	if(!selected_order_matches_scope())
		return false;
	return !ignore_ticket_contains(OrderTicket());
}

int selected_order_grid_slot_id(void)
{
	int slot_id = 0;
	if(try_parse_slot_id_from_comment(OrderComment(), slot_id))
		return slot_id;

	return p_to_slot_id(price_to_p(OrderOpenPrice()));
}

void reset_scan_cache(void)
{
	ArrayResize(g_filled_slots, 0);
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
		if(!selected_order_matches_scope())
			continue;

		int_array_add_unique(g_filled_slots, selected_order_grid_slot_id());
	}

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

bool selected_order_stop_locks_profit(const double stop_loss)
{
	int type = OrderType();
	if(type != OP_BUY && type != OP_SELL)
		return false;
	if(stop_loss <= 0.0)
		return false;

	double open_price = OrderOpenPrice();
	double epsilon = Point * 0.5;
	if(type == OP_BUY)
		return (stop_loss > (open_price + epsilon));
	return (stop_loss < (open_price - epsilon));
}

bool selected_order_stop_is_on_valid_market_side(const double stop_loss)
{
	int type = OrderType();
	if(type != OP_BUY && type != OP_SELL)
		return false;
	if(stop_loss <= 0.0)
		return false;

	double min_stop_distance = minimum_stop_distance_price();
	if(type == OP_BUY)
		return (stop_loss < normalize_price_value(Bid - min_stop_distance));
	return (stop_loss > normalize_price_value(Ask + min_stop_distance));
}

bool selected_order_stop_is_valid_protection(const double stop_loss)
{
	return (selected_order_stop_locks_profit(stop_loss) && selected_order_stop_is_on_valid_market_side(stop_loss));
}

bool selected_order_stop_needs_update(const double stop_loss)
{
	if(!selected_order_stop_is_valid_protection(stop_loss))
		return false;

	double current_stop_loss = OrderStopLoss();
	if(current_stop_loss <= 0.0)
		return true;

	if(OrderType() == OP_BUY)
	{
		if(current_stop_loss >= stop_loss)
			return false;
	}
	else
	{
		if(current_stop_loss <= stop_loss)
			return false;
	}

	return true;
}

double selected_order_net_profit(void)
{
	return OrderProfit() + OrderSwap() + OrderCommission();
}

bool selected_order_is_sequence_anchor_candidate(void)
{
	return selected_order_matches_scope();
}

bool most_losing_order_sequence_anchor(double &price_out, datetime &open_time_out, double &net_profit_out)
{
	price_out = 0.0;
	open_time_out = 0;
	net_profit_out = 0.0;
	bool found = false;
	double worst_profit = 0.0;
	datetime worst_open_time = 0;
	int worst_ticket = -1;

	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!selected_order_is_sequence_anchor_candidate())
			continue;

		double net_profit = selected_order_net_profit();
		datetime open_time = OrderOpenTime();
		int ticket = OrderTicket();
		if(found)
		{
			if(net_profit > worst_profit)
				continue;
			if(MathAbs(net_profit - worst_profit) <= 0.0000001 &&
				(open_time > worst_open_time || (open_time == worst_open_time && ticket > worst_ticket)))
				continue;
		}

		found = true;
		worst_profit = net_profit;
		worst_open_time = open_time;
		worst_ticket = ticket;
		price_out = OrderOpenPrice();
		open_time_out = open_time;
		net_profit_out = net_profit;
	}

	return found;
}

bool ensure_contraction_anchor(void)
{
	if(g_contraction_anchor_valid)
	{
		if(trade_direction == BUY_ONLY)
		{
			if(Bid < g_contraction_anchor_price)
			{
				g_contraction_anchor_price = Bid;
				g_contraction_anchor_time = TimeCurrent();
				log_msg(2,
					"Contraction price anchor moved: price=" +
					DoubleToString(g_contraction_anchor_price, Digits));
			}
		}
		else
		{
			if(Ask > g_contraction_anchor_price)
			{
				g_contraction_anchor_price = Ask;
				g_contraction_anchor_time = TimeCurrent();
				log_msg(2,
					"Contraction price anchor moved: price=" +
					DoubleToString(g_contraction_anchor_price, Digits));
			}
		}
		return true;
	}

	double anchor_price = 0.0;
	datetime anchor_time = 0;
	double anchor_net_profit = 0.0;
	if(!most_losing_order_sequence_anchor(anchor_price, anchor_time, anchor_net_profit))
		return false;

	g_contraction_anchor_valid = true;
	g_contraction_anchor_price = anchor_price;
	g_contraction_anchor_time = anchor_time;
	g_contraction_anchor_net_profit = anchor_net_profit;

	if(trade_direction == BUY_ONLY && Bid < g_contraction_anchor_price)
		g_contraction_anchor_price = Bid;
	if(trade_direction == SELL_ONLY && Ask > g_contraction_anchor_price)
		g_contraction_anchor_price = Ask;

	log_msg(1,
		"Contraction price anchor set: price=" + DoubleToString(g_contraction_anchor_price, Digits) +
		", net_profit=" + DoubleToString(g_contraction_anchor_net_profit, 2));
	return true;
}

double sequence_price_progress_pips(void)
{
	if(!ensure_contraction_anchor())
		return 0.0;

	double progress_price = 0.0;
	if(trade_direction == BUY_ONLY)
		progress_price = Bid - g_contraction_anchor_price;
	else
		progress_price = g_contraction_anchor_price - Ask;

	if(progress_price <= 0.0)
		return 0.0;
	return progress_price / g_pip_size;
}

int sequence_elapsed_candles(void)
{
	datetime start_time = g_contraction_reset_time;
	if(start_time <= 0)
		start_time = TimeCurrent();

	if(!sequence_has_open_orders())
		return 0;

	int shift = iBarShift(Symbol(), Period(), start_time, false);
	if(shift < 0)
		return 0;
	return shift;
}

bool sequence_has_open_orders(void)
{
	return (ArraySize(g_filled_slots) > 0);
}

bool history_order_is_newer_than_marker(const datetime close_time, const int ticket)
{
	if(close_time > g_last_history_close_time)
		return true;
	if(close_time == g_last_history_close_time && ticket > g_last_history_ticket)
		return true;
	return false;
}

void reset_contraction_state_after_profit_event(void)
{
	g_sequence_contraction_steps = 0;
	g_effective_trailing_stop_pips = trailing_stop_pips;
	g_last_contraction_steps = -1;
	g_last_effective_trailing_stop_pips = -1.0;
	g_contraction_reset_time = TimeCurrent();
	g_contraction_anchor_valid = false;
	g_contraction_anchor_price = 0.0;
	g_contraction_anchor_time = 0;
	g_contraction_anchor_net_profit = 0.0;

	log_msg(1,
		"Contraction reset after profit-taking event: effective_trailing_stop_pips=" +
		DoubleToString(g_effective_trailing_stop_pips, 2));
}

void initialize_history_marker(void)
{
	g_last_history_close_time = 0;
	g_last_history_ticket = 0;

	int total = OrdersHistoryTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_HISTORY))
			continue;
		if(!selected_order_is_trailing_managed())
			continue;

		datetime close_time = OrderCloseTime();
		if(close_time <= 0)
			continue;

		int ticket = OrderTicket();
		if(history_order_is_newer_than_marker(close_time, ticket))
		{
			g_last_history_close_time = close_time;
			g_last_history_ticket = ticket;
		}
	}
}

void detect_profit_taking_event(void)
{
	bool profit_event = false;
	datetime newest_close_time = g_last_history_close_time;
	int newest_ticket = g_last_history_ticket;

	int total = OrdersHistoryTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_HISTORY))
			continue;
		if(!selected_order_is_trailing_managed())
			continue;

		datetime close_time = OrderCloseTime();
		if(close_time <= 0)
			continue;

		int ticket = OrderTicket();
		if(!history_order_is_newer_than_marker(close_time, ticket))
			continue;

		double net_profit = selected_order_net_profit();
		if(net_profit > 0.0)
		{
			profit_event = true;
			log_msg(1,
				"Profit-taking event detected: ticket=" + IntegerToString(ticket) +
				", net_profit=" + DoubleToString(net_profit, 2));
		}

		if(close_time > newest_close_time || (close_time == newest_close_time && ticket > newest_ticket))
		{
			newest_close_time = close_time;
			newest_ticket = ticket;
		}
	}

	g_last_history_close_time = newest_close_time;
	g_last_history_ticket = newest_ticket;

	if(profit_event)
		reset_contraction_state_after_profit_event();
}

int raw_contraction_steps_count(void)
{
	if(contraction_mode == NONE)
		return 0;
	if(contraction_interval <= 0)
		return 0;
	if(trailing_stop_contraction_rate <= 0.0 || trailing_stop_contraction_rate >= 1.0)
		return 0;

	if(contraction_mode == PRICE)
		return (int)MathFloor(sequence_price_progress_pips() / (double)contraction_interval);
	if(contraction_mode != CANDLES)
		return 0;

	return sequence_elapsed_candles() / contraction_interval;
}

double trailing_pips_after_contraction_steps(const int steps)
{
	double pips = trailing_stop_pips;
	for(int i = 0; i < steps; ++i)
		pips *= trailing_stop_contraction_rate;

	if(pips < 0.0)
		pips = 0.0;
	return pips;
}

void update_contraction_state(void)
{
	if(!sequence_has_open_orders())
	{
		g_sequence_contraction_steps = 0;
		g_effective_trailing_stop_pips = trailing_stop_pips;
		g_contraction_reset_time = TimeCurrent();
		g_contraction_anchor_valid = false;
		g_contraction_anchor_price = 0.0;
		g_contraction_anchor_time = 0;
		g_contraction_anchor_net_profit = 0.0;
		return;
	}

	if(contraction_mode == NONE)
	{
		g_sequence_contraction_steps = 0;
		g_effective_trailing_stop_pips = trailing_stop_pips;
		g_contraction_anchor_valid = false;
		g_contraction_anchor_price = 0.0;
		g_contraction_anchor_time = 0;
		g_contraction_anchor_net_profit = 0.0;
		return;
	}

	int raw_steps = raw_contraction_steps_count();
	if(raw_steps > g_sequence_contraction_steps)
		g_sequence_contraction_steps = raw_steps;

	g_effective_trailing_stop_pips = trailing_pips_after_contraction_steps(g_sequence_contraction_steps);
}

double effective_trailing_stop_pips(void)
{
	update_contraction_state();
	return g_effective_trailing_stop_pips;
}

void log_contraction_update_if_needed(void)
{
	update_contraction_state();
	int steps = g_sequence_contraction_steps;
	double pips = effective_trailing_stop_pips();
	double epsilon = 1e-8;
	if(g_last_contraction_steps == steps && MathAbs(g_last_effective_trailing_stop_pips - pips) <= epsilon)
		return;

	g_last_contraction_steps = steps;
	g_last_effective_trailing_stop_pips = pips;

	if(contraction_mode == NONE)
	{
		log_msg(1,
			"Contraction update: mode=NONE" +
			", steps=" + IntegerToString(steps) +
			", effective_trailing_stop_pips=" + DoubleToString(pips, 2));
		return;
	}

	string mode_name = (contraction_mode == PRICE ? "PRICE" : "CANDLES");
	string progress = "";
	if(contraction_mode == PRICE)
	{
		progress =
			", progress_pips=" + DoubleToString(sequence_price_progress_pips(), 2) +
			", anchor_price=" + DoubleToString(g_contraction_anchor_price, Digits);
	}
	else
		progress = ", elapsed_candles=" + IntegerToString(sequence_elapsed_candles());

	log_msg(1,
		"Contraction update: mode=" + mode_name +
		", steps=" + IntegerToString(steps) +
		", base_trailing_stop_pips=" + DoubleToString(trailing_stop_pips, 2) +
		", effective_trailing_stop_pips=" + DoubleToString(pips, 2) +
		", contraction_rate=" + DoubleToString(trailing_stop_contraction_rate, 4) +
		", interval=" + IntegerToString(contraction_interval) +
		progress);
}

bool current_trailing_stop_price(double &stop_loss_out)
{
	stop_loss_out = 0.0;
	double trailing_pips = effective_trailing_stop_pips();
	if(trailing_pips <= 0.0)
		return false;

	double candidate = 0.0;
	if(trade_direction == BUY_ONLY)
		candidate = Bid - pips_to_price(trailing_pips);
	else
		candidate = Ask + pips_to_price(trailing_pips);

	double min_stop_distance = minimum_stop_distance_price();
	if(trade_direction == BUY_ONLY)
	{
		double highest_allowed = normalize_price_value(Bid - min_stop_distance);
		if(candidate > highest_allowed)
			candidate = highest_allowed;
	}
	else
	{
		double lowest_allowed = normalize_price_value(Ask + min_stop_distance);
		if(candidate < lowest_allowed)
			candidate = lowest_allowed;
	}

	stop_loss_out = normalize_price_value(candidate);
	return (stop_loss_out > 0.0);
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

		RefreshRates();
		if(!selected_order_stop_needs_update(stop_loss))
			return false;

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

void apply_trailing_stops(void)
{
	double stop_loss = 0.0;
	if(!current_trailing_stop_price(stop_loss))
		return;
	double active_trailing_pips = g_effective_trailing_stop_pips;

	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!selected_order_is_trailing_managed())
			continue;
		if(!selected_order_stop_needs_update(stop_loss))
			continue;

		int ticket = OrderTicket();
		if(trade_modify_stop_with_retries(ticket, stop_loss))
		{
			log_msg(1,
				"Trailing stop set ticket=" + IntegerToString(ticket) +
				", stop_loss=" + DoubleToString(stop_loss, Digits) +
				", effective_trailing_stop_pips=" + DoubleToString(active_trailing_pips, 2) +
				", contraction_steps=" + IntegerToString(g_sequence_contraction_steps));
		}
	}
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

bool trade_send_with_retries(const int type, const double lots, const string comment, int &ticket_out)
{
	ticket_out = -1;
	int precheck_err = 0;
	string precheck_detail = "";
	if(!trade_open_precheck(type, lots, precheck_err, precheck_detail))
	{
		log_msg(1,
			"Order open blocked: err=" + IntegerToString(precheck_err) +
			", " + precheck_detail);
		return false;
	}

	for(int attempt = 1; attempt <= g_trade_retries; ++attempt)
	{
		RefreshRates();
		double price = (type == OP_BUY ? Ask : Bid);
		ResetLastError();
		int ticket = OrderSend(Symbol(), type, lots, price, g_trade_slippage_points, 0.0, 0.0, comment, g_ea_magic, 0, clrNONE);
		if(ticket > 0)
		{
			ticket_out = ticket;
			return true;
		}

		int err = GetLastError();
		if(order_send_error_is_hard_stop(err))
		{
			log_msg(1,
				"Order open hard stop: err=" + IntegerToString(err) +
				", attempt=" + IntegerToString(attempt));
			return false;
		}

		log_msg(1, "OrderSend failed attempt=" + IntegerToString(attempt) + ", err=" + IntegerToString(err));
		if(attempt < g_trade_retries)
			Sleep(g_trade_retry_sleep_ms);
	}
	return false;
}

bool open_order_exists_for_slot(const int slot_id)
{
	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!selected_order_matches_scope())
			continue;
		if(selected_order_grid_slot_id() == slot_id)
			return true;
	}

	return false;
}

void queue_current_slot_if_touching_entry(const int slot_id_now, const int p_now)
{
	if(!slot_target_reached(slot_id_now, p_now))
		return;
	if(int_array_contains(g_filled_slots, slot_id_now))
		return;
	if(open_order_exists_for_slot(slot_id_now))
	{
		int_array_add_unique(g_filled_slots, slot_id_now);
		return;
	}

	int_array_add_unique(g_pending_slots, slot_id_now);
}

void maybe_update_cache_after_successful_open(const int slot_id)
{
	int_array_add_unique(g_filled_slots, slot_id);
}

void update_prev_slot_seed_if_needed(void)
{
	if(g_prev_slot_id_initialized)
		return;

	RefreshRates();
	int p_now = price_to_p(mid_price());
	g_prev_slot_id = p_to_slot_id(p_now);
	g_prev_slot_id_initialized = true;
}

void run_entry_engine(void)
{
	if(!g_state_ready)
		return;
	if(only_take_profit_mode)
		return;
	if(g_max_gap_orders_per_tick <= 0)
		return;
	if(grid_step_pips <= 0)
		return;
	if(g_order_lot_size_active <= 0.0)
		return;

	RefreshRates();
	int p_now = price_to_p(mid_price());
	int slot_id_now = p_to_slot_id(p_now);

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
	queue_current_slot_if_touching_entry(slot_id_now, p_now);

	int opened_slots_this_tick[];
	ArrayResize(opened_slots_this_tick, 0);
	int opened_count = 0;

	for(int i = 0; i < ArraySize(g_pending_slots); ++i)
	{
		int slot_candidate = g_pending_slots[i];

		if(int_array_contains(g_filled_slots, slot_candidate) || int_array_contains(opened_slots_this_tick, slot_candidate))
			continue;
		if(open_order_exists_for_slot(slot_candidate))
		{
			maybe_update_cache_after_successful_open(slot_candidate);
			continue;
		}
		if(!slot_target_reached(slot_candidate, p_now))
			continue;

		int ticket = -1;
		string order_comment = order_comment_for_slot(slot_candidate);
		if(trade_send_with_retries(matching_order_type(), g_order_lot_size_active, order_comment, ticket))
		{
			maybe_update_cache_after_successful_open(slot_candidate);
			int_array_add_unique(opened_slots_this_tick, slot_candidate);
			opened_count++;

			double fill_price = 0.0;
			if(OrderSelect(ticket, SELECT_BY_TICKET, MODE_TRADES))
			{
				fill_price = OrderOpenPrice();
				int_array_add_unique(g_filled_slots, selected_order_grid_slot_id());
			}

			log_msg(1,
				"Opened order: slot_id=" + IntegerToString(slot_candidate) +
				", lots=" + DoubleToString(g_order_lot_size_active, 2) +
				", price=" + DoubleToString((fill_price > 0.0 ? fill_price : (matching_order_type() == OP_BUY ? Ask : Bid)), Digits) +
				", mode=BEST_IN_SLOT" +
				", target_p=" + IntegerToString(slot_target_p(slot_candidate)) +
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
	if(order_lot_size <= 0.0)
	{
		log_msg(1, "Init failed: order_lot_size must be > 0");
		return INIT_FAILED;
	}
	if(trailing_stop_pips <= 0.0)
	{
		log_msg(1, "Init failed: trailing_stop_pips must be > 0");
		return INIT_FAILED;
	}
	if(trailing_stop_contraction_rate <= 0.0 || trailing_stop_contraction_rate > 1.0)
	{
		log_msg(1, "Init failed: trailing_stop_contraction_rate must be > 0 and <= 1");
		return INIT_FAILED;
	}
	if(contraction_interval < 0)
	{
		log_msg(1, "Init failed: contraction_interval must be >= 0");
		return INIT_FAILED;
	}
	if(contraction_mode != NONE && trailing_stop_contraction_rate < 1.0 && contraction_interval <= 0)
	{
		log_msg(1, "Init failed: contraction_interval must be > 0 when contraction is enabled");
		return INIT_FAILED;
	}
	if(!detect_supported_symbol())
	{
		log_msg(1, "Init failed: unsupported symbol '" + Symbol() + "'");
		return INIT_FAILED;
	}

	double normalized_lots = 0.0;
	if(!normalize_lot_size(order_lot_size, normalized_lots))
	{
		log_msg(1, "Init failed: order_lot_size cannot be normalized for broker");
		return INIT_FAILED;
	}

	g_order_lot_size_active = normalized_lots;
	g_prev_slot_id_initialized = false;
	g_sequence_contraction_steps = 0;
	g_effective_trailing_stop_pips = trailing_stop_pips;
	g_last_contraction_steps = -1;
	g_last_effective_trailing_stop_pips = -1.0;
	g_contraction_reset_time = TimeCurrent();
	g_contraction_anchor_valid = false;
	g_contraction_anchor_price = 0.0;
	g_contraction_anchor_time = 0;
	g_contraction_anchor_net_profit = 0.0;
	initialize_history_marker();
	reset_scan_cache();
	ArrayResize(g_pending_slots, 0);
	parse_ignore_tickets();

	g_state_ready = true;
	scan_open_orders();
	log_contraction_update_if_needed();
	apply_trailing_stops();
	update_prev_slot_seed_if_needed();

	log_msg(1,
		"Init success: symbol=" + Symbol() +
		", direction=" + direction_to_string() +
		", order_lot_size=" + DoubleToString(g_order_lot_size_active, 2) +
		", grid_step_pips=" + IntegerToString(grid_step_pips) +
		", only_take_profit_mode=" + (only_take_profit_mode ? "true" : "false") +
		", only_manage_my_own_orders=" + (only_manage_my_own_orders ? "true" : "false") +
		", trailing_stop_pips=" + DoubleToString(trailing_stop_pips, 2) +
		", trailing_stop_contraction_rate=" + DoubleToString(trailing_stop_contraction_rate, 4) +
		", contraction_mode=" + (contraction_mode == PRICE ? "PRICE" : (contraction_mode == CANDLES ? "CANDLES" : "NONE")) +
		", contraction_interval=" + IntegerToString(contraction_interval) +
		", pip_size=" + DoubleToString(g_pip_size, 5));

	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	log_msg(1, "Deinit reason=" + IntegerToString(reason));
}

void OnTick(void)
{
	if(!g_state_ready)
		return;

	RefreshRates();
	detect_profit_taking_event();
	scan_open_orders();
	log_contraction_update_if_needed();
	apply_trailing_stops();
	run_entry_engine();
}
