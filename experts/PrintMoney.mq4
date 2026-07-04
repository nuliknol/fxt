#property strict

#define ORDER_COMMENT_PREFIX "PM|G="

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

input trade_direction_t trade_direction = BUY_ONLY;
input double lot_per_entry = 0.01;
input int grid_step_pips = 10;
input int lock_trigger_pips = 20;
input int lock_profit_pips = 10;
input bool ea_enabled = true;
input int magic_number = 20260221;
input bool protect_manually_placed_orders = false;
input string ignore_tickets_csv = "";
input int max_gap_orders_per_tick = 5;
input int timer_period_seconds = 60;
input int log_level = 1;

bool g_state_initialized = false;
int g_last_entry_grid_g = 0;
bool g_has_extremal_entry = false;
double g_min_entry_price_considered = 0.0;
double g_max_entry_price_considered = 0.0;
int g_ignored_tickets[];
instrument_type_t g_instrument_type = INSTRUMENT_UNKNOWN;
double g_pip_size = 0.0;
datetime g_last_maintenance_time = 0;

void log_msg(const int level, const string msg)
{
	if(log_level >= level)
		Print("PM: " + msg);
}

string direction_to_string(void)
{
	if(trade_direction == BUY_ONLY)
		return "BUY_ONLY";
	return "SELL_ONLY";
}

bool is_upper_alpha_symbol6(const string sym)
{
	if(StringLen(sym) != 6)
		return false;
	for(int i = 0; i < 6; ++i)
	{
		int ch = StringGetCharacter(sym, i);
		if(ch < 'A' || ch > 'Z')
			return false;
	}
	return true;
}

bool is_explicit_gold_symbol(const string sym)
{
	if(sym == "XAUUSD")
		return true;
	if(sym == "XAUUSDm")
		return true;
	if(sym == "GOLD")
		return true;
	return false;
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

bool detect_instrument(void)
{
	string sym = Symbol();
	if(is_explicit_gold_symbol(sym))
	{
		g_instrument_type = INSTRUMENT_GOLD;
		g_pip_size = 1.0;
		return true;
	}
	if(symbol_is_one_of(sym, "US100", "NDXUSD", ".USTECHCash"))
	{
		g_instrument_type = INSTRUMENT_INDEX;
		g_pip_size = 1.0;
		return true;
	}
	if(is_upper_alpha_symbol6(sym))
	{
		string quote = StringSubstr(sym, 3, 3);
		g_instrument_type = INSTRUMENT_FX;
		if(quote == "JPY")
			g_pip_size = 0.01;
		else
			g_pip_size = 0.0001;
		return true;
	}
	g_instrument_type = INSTRUMENT_UNKNOWN;
	g_pip_size = 0.0;
	return false;
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

bool is_ticket_ignored(const int ticket)
{
	int n = ArraySize(g_ignored_tickets);
	for(int i = 0; i < n; ++i)
	{
		if(g_ignored_tickets[i] == ticket)
			return true;
	}
	return false;
}

void parse_ignore_list(void)
{
	ArrayResize(g_ignored_tickets, 0);
	string src = ignore_tickets_csv;
	int src_len = StringLen(src);
	if(src_len <= 0)
	{
		log_msg(1, "Ignore list active: 0 tickets");
		return;
	}

	string parts[];
	int cnt = StringSplit(src, ',', parts);
	if(cnt <= 0)
	{
		log_msg(1, "Ignore list active: 0 tickets");
		return;
	}

	for(int i = 0; i < cnt; ++i)
	{
		int t = 0;
		if(!try_parse_positive_int(parts[i], t))
		{
			if(log_level >= 2)
				log_msg(2, "Ignore list token skipped: '" + parts[i] + "'");
			continue;
		}
		if(is_ticket_ignored(t))
			continue;
		int n = ArraySize(g_ignored_tickets);
		ArrayResize(g_ignored_tickets, n + 1);
		g_ignored_tickets[n] = t;
	}

	log_msg(1, "Ignore list active: " + IntegerToString(ArraySize(g_ignored_tickets)) + " tickets");
}

int map_price_to_p(const double price)
{
	if(g_instrument_type == INSTRUMENT_GOLD || g_instrument_type == INSTRUMENT_INDEX)
		return (int)MathFloor(price + 1e-9);
	return (int)MathRound(price / g_pip_size);
}

double price_from_p(const int p)
{
	if(g_instrument_type == INSTRUMENT_GOLD || g_instrument_type == INSTRUMENT_INDEX)
		return (double)p;
	return ((double)p) * g_pip_size;
}

int grid_floor(const int p)
{
	if(grid_step_pips <= 0)
		return p;
	return (p / grid_step_pips) * grid_step_pips;
}

int snap_to_nearest_grid(const int p)
{
	// Restart reconstruction snaps to nearest absolute grid line.
	if(grid_step_pips <= 0)
		return p;
	int lower = grid_floor(p);
	int upper = lower + grid_step_pips;
	if((p - lower) <= (upper - p))
		return lower;
	return upper;
}

bool try_parse_grid_from_comment(const string comment, int &grid_out)
{
	if(StringFind(comment, ORDER_COMMENT_PREFIX, 0) != 0)
		return false;
	string payload = StringSubstr(comment, StringLen(ORDER_COMMENT_PREFIX));
	string token = payload;
	int sep = StringFind(payload, "|", 0);
	if(sep >= 0)
		token = StringSubstr(payload, 0, sep);
	int parsed = 0;
	if(!try_parse_int(token, parsed))
		return false;
	grid_out = parsed;
	return true;
}

int get_order_grid_from_selected_order(void)
{
	int g = 0;
	if(try_parse_grid_from_comment(OrderComment(), g))
		return g;
	int p = map_price_to_p(OrderOpenPrice());
	return snap_to_nearest_grid(p);
}

double entry_reference_price(void)
{
	if(trade_direction == SELL_ONLY)
		return Bid;
	return Ask;
}

int matching_order_type(void)
{
	if(trade_direction == SELL_ONLY)
		return OP_SELL;
	return OP_BUY;
}

bool order_matches_scope(void)
{
	if(OrderSymbol() != Symbol())
		return false;
	if(OrderMagicNumber() != magic_number)
		return false;
	if(OrderType() != matching_order_type())
		return false;
	return true;
}

bool order_matches_profit_lock_scope(void)
{
	if(OrderSymbol() != Symbol())
		return false;
	if(OrderType() != matching_order_type())
		return false;
	if(OrderMagicNumber() == magic_number)
		return true;
	if(protect_manually_placed_orders && OrderMagicNumber() == 0)
		return true;
	return false;
}

void set_default_last_grid_from_market(void)
{
	double ref_price = entry_reference_price();
	int p_now = map_price_to_p(ref_price);
	g_last_entry_grid_g = grid_floor(p_now);
}

void scan_orders_and_rebuild_state(void)
{
	bool has_considered = false;
	g_has_extremal_entry = false;
	g_min_entry_price_considered = 0.0;
	g_max_entry_price_considered = 0.0;

	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!order_matches_scope())
			continue;

		int ticket = OrderTicket();
		if(is_ticket_ignored(ticket))
			continue;

		int g_order = get_order_grid_from_selected_order();
		double open_price = OrderOpenPrice();

		if(!has_considered)
		{
			has_considered = true;
			g_last_entry_grid_g = g_order;
			g_min_entry_price_considered = open_price;
			g_max_entry_price_considered = open_price;
		}
		else
		{
			if(trade_direction == SELL_ONLY)
			{
				if(g_order < g_last_entry_grid_g)
					g_last_entry_grid_g = g_order;
				if(open_price < g_min_entry_price_considered)
					g_min_entry_price_considered = open_price;
			}
			else
			{
				if(g_order > g_last_entry_grid_g)
					g_last_entry_grid_g = g_order;
				if(open_price > g_max_entry_price_considered)
					g_max_entry_price_considered = open_price;
			}
		}
	}

	if(!has_considered)
	{
		set_default_last_grid_from_market();
		g_has_extremal_entry = false;
	}
	else
	{
		g_has_extremal_entry = true;
	}

	if(log_level >= 2)
	{
		string ext_msg;
		if(!g_has_extremal_entry)
			ext_msg = "none";
		else if(trade_direction == SELL_ONLY)
			ext_msg = "min_entry=" + DoubleToString(g_min_entry_price_considered, Digits);
		else
			ext_msg = "max_entry=" + DoubleToString(g_max_entry_price_considered, Digits);
		log_msg(2,
			"state rebuilt: last_G=" + IntegerToString(g_last_entry_grid_g) +
			", extremal=" + ext_msg +
			", symbol=" + Symbol() +
			", direction=" + direction_to_string() +
			", magic=" + IntegerToString(magic_number));
	}
}

bool no_considered_losers_gate_passes(void)
{
	if(!g_has_extremal_entry)
		return true;

	if(trade_direction == SELL_ONLY)
	{
		if(Ask <= g_min_entry_price_considered)
			return true;
		log_msg(1, "Entry blocked: Ask > min_entry (ignored excluded)");
		return false;
	}

	if(Bid >= g_max_entry_price_considered)
		return true;
	log_msg(1, "Entry blocked: Bid < max_entry (ignored excluded)");
	return false;
}

int send_market_order_for_grid(const int grid_g)
{
	int type = matching_order_type();
	double price = (type == OP_BUY ? Ask : Bid);
	string comment = ORDER_COMMENT_PREFIX + IntegerToString(grid_g);
	RefreshRates();
	price = (type == OP_BUY ? Ask : Bid);
	int ticket = OrderSend(Symbol(), type, lot_per_entry, price, 20, 0.0, 0.0, comment, magic_number, 0, clrNONE);
	return ticket;
}

void run_entry_logic(void)
{
	if(!g_state_initialized)
		return;
	if(grid_step_pips <= 0)
		return;
	if(max_gap_orders_per_tick <= 0)
		return;

	double ref_price = entry_reference_price();
	int p_now = map_price_to_p(ref_price);
	int g_now = grid_floor(p_now);
	int g_next = (trade_direction == SELL_ONLY ? g_last_entry_grid_g - grid_step_pips : g_last_entry_grid_g + grid_step_pips);

	bool crossed = (trade_direction == SELL_ONLY ? (g_now <= g_next) : (g_now >= g_next));
	if(!crossed)
	{
		if(log_level >= 2)
			log_msg(2, "No crossing: P_now=" + IntegerToString(p_now) + ", G_now=" + IntegerToString(g_now) + ", G_next=" + IntegerToString(g_next));
		return;
	}

	if(!no_considered_losers_gate_passes())
		return;

	int placed = 0;
	while((trade_direction == SELL_ONLY ? (g_now <= g_next) : (g_now >= g_next)) && placed < max_gap_orders_per_tick)
	{
		int ticket = send_market_order_for_grid(g_next);
		if(ticket > 0)
		{
			if(!OrderSelect(ticket, SELECT_BY_TICKET, MODE_TRADES))
			{
				log_msg(1,
					"Opened " + direction_to_string() +
					" for grid G=" + IntegerToString(g_next) +
					" (ticket=" + IntegerToString(ticket) + ")");
			}
			else
			{
				log_msg(1,
					"Opened " + direction_to_string() +
					" for grid G=" + IntegerToString(g_next) +
					" at fill price " + DoubleToString(OrderOpenPrice(), Digits) +
					", ticket=" + IntegerToString(ticket));
			}

			g_last_entry_grid_g = g_next;
			g_next = (trade_direction == SELL_ONLY ? g_next - grid_step_pips : g_next + grid_step_pips);
			placed++;
			continue;
		}

		int err = GetLastError();
		log_msg(1,
			"OrderSend failed: symbol=" + Symbol() +
			", direction=" + direction_to_string() +
			", magic=" + IntegerToString(magic_number) +
			", G=" + IntegerToString(g_next) +
			", err=" + IntegerToString(err));
		break;
	}

	if(placed >= max_gap_orders_per_tick && (trade_direction == SELL_ONLY ? (g_now <= g_next) : (g_now >= g_next)))
		log_msg(1, "Gap exceeded max_gap_orders_per_tick; remaining levels deferred");
}

double lock_sl_epsilon(void)
{
	if(g_instrument_type == INSTRUMENT_GOLD || g_instrument_type == INSTRUMENT_INDEX)
		return 0.05;
	return MathMax(Point * 3.0, g_pip_size * 0.2);
}

bool is_already_locked(const double current_sl, const double target_sl)
{
	if(current_sl <= 0.0)
		return false;
	return (MathAbs(current_sl - target_sl) <= lock_sl_epsilon());
}

void run_profit_lock_management(void)
{
	if(!g_state_initialized)
		return;
	if(lock_trigger_pips <= 0)
		return;

	RefreshRates();
	int p_ask_now = map_price_to_p(Ask);
	int p_bid_now = map_price_to_p(Bid);

	int total = OrdersTotal();
	for(int i = 0; i < total; ++i)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!order_matches_profit_lock_scope())
			continue;
		if(is_ticket_ignored(OrderTicket()))
			continue;

		int g_order = get_order_grid_from_selected_order();
		int favorable_pips = 0;
		if(trade_direction == SELL_ONLY)
			favorable_pips = g_order - p_ask_now;
		else
			favorable_pips = p_bid_now - g_order;
		if(favorable_pips < lock_trigger_pips)
			continue;

		int sl_target_p = (trade_direction == SELL_ONLY ? (g_order - lock_profit_pips) : (g_order + lock_profit_pips));
		double sl_target_price = price_from_p(sl_target_p);
		sl_target_price = NormalizeDouble(sl_target_price, Digits);
		if(is_already_locked(OrderStopLoss(), sl_target_price))
			continue;

		bool ok = OrderModify(OrderTicket(), OrderOpenPrice(), sl_target_price, OrderTakeProfit(), 0, clrNONE);
		if(ok)
		{
			log_msg(1,
				"Locked ticket " + IntegerToString(OrderTicket()) +
				" at SL=" + DoubleToString(sl_target_price, Digits) +
				" (G=" + IntegerToString(g_order) + ")");
		}
		else
		{
			int err = GetLastError();
			log_msg(1,
				"Failed to set SL: ticket=" + IntegerToString(OrderTicket()) +
				", symbol=" + Symbol() +
				", direction=" + direction_to_string() +
				", magic=" + IntegerToString(magic_number) +
				", G=" + IntegerToString(g_order) +
				", err=" + IntegerToString(err));
		}
	}
}

void run_scheduled_maintenance(void)
{
	scan_orders_and_rebuild_state();
	run_profit_lock_management();
	g_last_maintenance_time = TimeCurrent();
}

void run_tester_timer_fallback_if_due(void)
{
	if(!IsTesting())
		return;
	if(!g_state_initialized)
		return;
	if(timer_period_seconds <= 0)
		return;

	datetime now_ts = TimeCurrent();
	if(g_last_maintenance_time == 0 || (now_ts - g_last_maintenance_time) >= timer_period_seconds)
		run_scheduled_maintenance();
}

bool validate_inputs(void)
{
	if(lot_per_entry <= 0.0)
	{
		log_msg(1, "Invalid input: lot_per_entry must be > 0");
		return false;
	}
	if(grid_step_pips <= 0)
	{
		log_msg(1, "Invalid input: grid_step_pips must be > 0");
		return false;
	}
	if(lock_trigger_pips <= 0)
	{
		log_msg(1, "Invalid input: lock_trigger_pips must be > 0");
		return false;
	}
	if(lock_profit_pips < 0)
	{
		log_msg(1, "Invalid input: lock_profit_pips must be >= 0");
		return false;
	}
	if(max_gap_orders_per_tick <= 0)
	{
		log_msg(1, "Invalid input: max_gap_orders_per_tick must be > 0");
		return false;
	}
	if(timer_period_seconds <= 0)
	{
		log_msg(1, "Invalid input: timer_period_seconds must be > 0");
		return false;
	}
	return true;
}

int OnInit(void)
{
	if(!validate_inputs())
		return INIT_FAILED;

	if(!detect_instrument())
	{
		log_msg(1, "Unsupported symbol: " + Symbol());
		return INIT_FAILED;
	}

	parse_ignore_list();
	EventSetTimer(timer_period_seconds);
	scan_orders_and_rebuild_state();
	g_last_maintenance_time = TimeCurrent();
	g_state_initialized = true;

	string instrument_name = "FX";
	if(g_instrument_type == INSTRUMENT_GOLD)
		instrument_name = "GOLD";
	else if(g_instrument_type == INSTRUMENT_INDEX)
		instrument_name = "INDEX";
	log_msg(1,
		"Initialized: symbol=" + Symbol() +
		", instrument=" + instrument_name +
		", direction=" + direction_to_string() +
		", magic=" + IntegerToString(magic_number) +
		", last_G=" + IntegerToString(g_last_entry_grid_g));
	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	EventKillTimer();
}

void OnTick(void)
{
	if(!ea_enabled)
		return;

	run_tester_timer_fallback_if_due();
	run_entry_logic();
}

void OnTimer(void)
{
	if(!ea_enabled)
		return;

	run_scheduled_maintenance();
}
