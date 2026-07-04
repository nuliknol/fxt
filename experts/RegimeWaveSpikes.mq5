#property copyright ""
#property link      ""
#property version   "1.00"
#property strict
#property indicator_chart_window
#property indicator_plots 0

enum wave_direction_filter_t
{
	WAVE_DIRECTION_UP = 0,
	WAVE_DIRECTION_DOWN = 1
};

input string InpRegimePrefix = "REGIME_";
input int	InpWaveHorizonBars = 64;
input int	InpTopN = 10;
input int	InpDisplayTopK = 3;
input bool	InpAvoidOverlaps = true;
input int	InpMinSeparationBars = 32;
input wave_direction_filter_t InpWaveDirection = WAVE_DIRECTION_UP;
input bool	InpTruncatePips = true;
input bool	InpShowCornerTable = true;
input int	InpMaxRegimes = 50;
input bool	InpDebugLogs = false;

struct wave_candidate_t
{
	int		anchor_idx;
	datetime	anchor_time;
	double		pips;
	double		max_h;
	double		min_l;
	datetime	max_h_time;
	datetime	min_l_time;
	int		max_h_idx;
	int		min_l_idx;
};

struct regime_t
{
	string		name;
	datetime	start_time;
	datetime	end_time;
	int		idx_lo;
	int		idx_hi;
	bool		valid;
	string		error_msg;
	wave_candidate_t selected[];
};

bool g_need_recalc = true;
datetime g_last_bar_time = 0;

void dbg_log(const string msg)
{
	if(InpDebugLogs)
		Print("RWS: " + msg);
}

string rws_prefix_for_regime(const string regime_name)
{
	return "RWS_" + regime_name + "_";
}

bool starts_with(const string value, const string prefix)
{
	int plen = (int)StringLen(prefix);
	if(plen <= 0)
		return true;
	if((int)StringLen(value) < plen)
		return false;
	return StringSubstr(value, 0, plen) == prefix;
}

double get_pip_size(void)
{
	int digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
	double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
	if(digits == 5 || digits == 3)
		return point * 10.0;
	return point;
}

double normalize_pips(const double raw_pips)
{
	if(InpTruncatePips)
	{
		// Integer pip truncation for consistency with spec.
		double floored = MathFloor(raw_pips + 1e-9);
		return floored;
	}
	// Consistent non-truncate mode: round to 0.1 pip.
	return MathRound(raw_pips * 10.0) / 10.0;
}

void delete_objects_by_prefix(const string obj_prefix)
{
	int total = ObjectsTotal(0, 0, -1);
	for(int i = total - 1; i >= 0; --i)
	{
		string obj_name = ObjectName(0, i, 0, -1);
		if(starts_with(obj_name, obj_prefix))
			ObjectDelete(0, obj_name);
	}
}

void clear_all_rws_objects(void)
{
	delete_objects_by_prefix("RWS_");
}

bool compare_candidate_desc(const wave_candidate_t &a, const wave_candidate_t &b)
{
	if(a.pips > b.pips)
		return true;
	if(a.pips < b.pips)
		return false;
	return a.anchor_idx < b.anchor_idx;
}

void copy_candidates(wave_candidate_t &dst[], const wave_candidate_t &src[])
{
	int n = ArraySize(src);
	ArrayResize(dst, n);
	for(int i = 0; i < n; ++i)
		dst[i] = src[i];
}

void sort_candidates_desc(wave_candidate_t &arr[])
{
	int n = ArraySize(arr);
	for(int i = 0; i < n - 1; ++i)
	{
		int best = i;
		for(int j = i + 1; j < n; ++j)
		{
			if(compare_candidate_desc(arr[j], arr[best]))
				best = j;
		}
		if(best != i)
		{
			wave_candidate_t tmp = arr[i];
			arr[i] = arr[best];
			arr[best] = tmp;
		}
	}
}

bool wave_intervals_compatible(const wave_candidate_t &a, const wave_candidate_t &b, const int L)
{
	int a_start = a.anchor_idx;
	int a_end = a.anchor_idx + (L - 1);
	int b_start = b.anchor_idx;
	int b_end = b.anchor_idx + (L - 1);

	int overlap_start = MathMax(a_start, b_start);
	int overlap_end = MathMin(a_end, b_end);
	if(overlap_start > overlap_end)
		return true;

	// Allow touching at exactly one boundary bar, reject wider overlap.
	int overlap_bars = overlap_end - overlap_start + 1;
	return (overlap_bars <= 1);
}

bool candidate_allowed(const wave_candidate_t &cand, const wave_candidate_t &picked[], const int L)
{
	int m = ArraySize(picked);
	for(int i = 0; i < m; ++i)
	{
		if(!wave_intervals_compatible(cand, picked[i], L))
			return false;
	}

	if(!InpAvoidOverlaps)
		return true;
	int min_sep = InpMinSeparationBars;
	if(min_sep < 1)
		min_sep = 1;
	for(int i = 0; i < m; ++i)
	{
		if(MathAbs(cand.anchor_idx - picked[i].anchor_idx) < min_sep)
			return false;
	}
	return true;
}

bool wave_matches_direction(const datetime min_l_time, const datetime max_h_time)
{
	if(min_l_time <= 0 || max_h_time <= 0)
		return false;
	if(min_l_time == max_h_time)
		return false;

	bool is_up_wave = (min_l_time < max_h_time);
	if(InpWaveDirection == WAVE_DIRECTION_UP)
		return is_up_wave;
	return !is_up_wave;
}

int find_bar_index_for_time(const datetime t)
{
	int idx = iBarShift(_Symbol, _Period, t, false);
	if(idx >= 0)
		return idx;
	// Clamp out-of-range times to available history.
	int bars = Bars(_Symbol, _Period);
	if(bars <= 0)
		return -1;
	datetime first_time = iTime(_Symbol, _Period, bars - 1);
	datetime last_time = iTime(_Symbol, _Period, 0);
	if(t < first_time)
		return bars - 1;
	if(t > last_time)
		return 0;
	return -1;
}

bool resolve_regime_bounds(regime_t &reg)
{
	int idx_a = find_bar_index_for_time(reg.start_time);
	int idx_b = find_bar_index_for_time(reg.end_time);
	if(idx_a < 0 || idx_b < 0)
	{
		reg.valid = false;
		reg.error_msg = "Time range out of history";
		return false;
	}
	reg.idx_lo = MathMin(idx_a, idx_b);
	reg.idx_hi = MathMax(idx_a, idx_b);
	if(reg.idx_lo < 0 || reg.idx_hi < 0 || reg.idx_hi < reg.idx_lo)
	{
		reg.valid = false;
		reg.error_msg = "Invalid bar range";
		return false;
	}
	reg.valid = true;
	reg.error_msg = "";
	if(InpDebugLogs)
		PrintFormat("RWS: bounds %s idx_lo=%d idx_hi=%d", reg.name, reg.idx_lo, reg.idx_hi);
	return true;
}

void compute_regime_spikes(regime_t &reg,
	const int rates_total,
	const datetime &time[],
	const double &high[],
	const double &low[],
	const double pip_size)
{
	ArrayResize(reg.selected, 0);
	if(!reg.valid)
		return;

	int L = InpWaveHorizonBars;
	if(L < 1)
		L = 1;

	if((reg.idx_hi - reg.idx_lo + 1) < L)
	{
		reg.valid = false;
		reg.error_msg = "Regime too short for L=" + IntegerToString(L);
		return;
	}

	int anchor_first = reg.idx_lo;
	int anchor_last = reg.idx_hi - (L - 1);
	if(anchor_last < anchor_first)
	{
		reg.valid = false;
		reg.error_msg = "No valid anchors for L=" + IntegerToString(L);
		return;
	}

	wave_candidate_t candidates[];
	ArrayResize(candidates, 0);

	for(int i = anchor_first; i <= anchor_last; ++i)
	{
		double max_h = -DBL_MAX;
		double min_l = DBL_MAX;
		datetime max_h_time = 0;
		datetime min_l_time = 0;
		int max_h_idx = -1;
		int min_l_idx = -1;
		for(int j = i; j < i + L; ++j)
		{
			if(j < 0 || j >= rates_total)
				continue;
			if(high[j] > max_h)
			{
				max_h = high[j];
				max_h_time = time[j];
				max_h_idx = j;
			}
			if(low[j] < min_l)
			{
				min_l = low[j];
				min_l_time = time[j];
				min_l_idx = j;
			}
		}
		if(max_h == -DBL_MAX || min_l == DBL_MAX)
			continue;
		if(!wave_matches_direction(min_l_time, max_h_time))
			continue;

		double delta = max_h - min_l;
		double pips = normalize_pips(delta / pip_size);

		int n = ArraySize(candidates);
		ArrayResize(candidates, n + 1);
		candidates[n].anchor_idx = i;
		candidates[n].anchor_time = time[i];
		candidates[n].pips = pips;
		candidates[n].max_h = max_h;
		candidates[n].min_l = min_l;
		candidates[n].max_h_time = max_h_time;
		candidates[n].min_l_time = min_l_time;
		candidates[n].max_h_idx = max_h_idx;
		candidates[n].min_l_idx = min_l_idx;
	}

	if(ArraySize(candidates) == 0)
	{
		reg.valid = false;
		reg.error_msg = "No candidates";
		if(InpDebugLogs)
			PrintFormat("RWS: %s no candidates (L=%d)", reg.name, L);
		return;
	}

	sort_candidates_desc(candidates);

	int top_n = InpTopN;
	if(top_n < 1)
		top_n = 1;

	wave_candidate_t picked[];
	ArrayResize(picked, 0);
	for(int k = 0; k < ArraySize(candidates); ++k)
	{
		if(!candidate_allowed(candidates[k], picked, L))
			continue;
		int m = ArraySize(picked);
		ArrayResize(picked, m + 1);
		picked[m] = candidates[k];
		if(ArraySize(picked) >= top_n)
			break;
	}

	if(ArraySize(picked) == 0)
	{
		reg.valid = false;
		reg.error_msg = "No spikes after filtering";
		if(InpDebugLogs)
			PrintFormat("RWS: %s no spikes after filtering (min_sep=%d)", reg.name, InpMinSeparationBars);
		return;
	}

	copy_candidates(reg.selected, picked);
	reg.valid = true;
	reg.error_msg = "";
	if(InpDebugLogs)
		PrintFormat("RWS: %s candidates=%d selected=%d", reg.name, ArraySize(candidates), ArraySize(reg.selected));
}

color rank_color(const int rank)
{
	if(rank == 1)
		return clrLimeGreen;
	if(rank == 2)
		return clrDeepSkyBlue;
	if(rank == 3)
		return clrGold;
	return clrSilver;
}

void draw_regime_error(const regime_t &reg)
{
	string obj_prefix = rws_prefix_for_regime(reg.name);
	string name = obj_prefix + "ERR";
	ObjectDelete(0, name);

	double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
	double y = SymbolInfoDouble(_Symbol, SYMBOL_BID) + (20.0 * point);
	datetime t = reg.end_time;
	if(reg.idx_lo >= 0)
	{
		double h = iHigh(_Symbol, _Period, reg.idx_lo);
		datetime th = iTime(_Symbol, _Period, reg.idx_lo);
		if(h > 0.0)
			y = h + (10.0 * point);
		if(th > 0)
			t = th;
	}

	if(ObjectCreate(0, name, OBJ_TEXT, 0, t, y))
	{
		ObjectSetString(0, name, OBJPROP_TEXT, reg.error_msg);
		ObjectSetInteger(0, name, OBJPROP_COLOR, clrTomato);
		ObjectSetInteger(0, name, OBJPROP_FONTSIZE, 9);
	}
}

void draw_regime_spikes(const regime_t &reg)
{
	string obj_prefix = rws_prefix_for_regime(reg.name);
	delete_objects_by_prefix(obj_prefix);

	if(!reg.valid)
	{
		draw_regime_error(reg);
		return;
	}

	int display_k = InpDisplayTopK;
	if(display_k < 1)
		display_k = 1;
	int top_n = ArraySize(reg.selected);
	if(display_k > top_n)
		display_k = top_n;

	double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

	for(int i = 0; i < display_k; ++i)
	{
		wave_candidate_t c = reg.selected[i];
		int rank = i + 1;

		string arr_name = obj_prefix + "ARR_" + IntegerToString(rank);
		string txt_name = obj_prefix + "SPIKE_" + IntegerToString(rank);
		string line_name = obj_prefix + "WAVE_" + IntegerToString(rank);
		ObjectDelete(0, arr_name);
		ObjectDelete(0, txt_name);
		ObjectDelete(0, line_name);

		long t_high = (long)c.max_h_time;
		long t_low = (long)c.min_l_time;
		datetime center_time = c.anchor_time;
		if(t_high > 0 && t_low > 0)
			center_time = (datetime)((t_high + t_low) / 2);

		double wave_height = c.max_h - c.min_l;
		double label_gap = MathMax(25.0 * point, wave_height * 0.12);
		double y = c.max_h + label_gap;
		string pips_text = DoubleToString(c.pips, InpTruncatePips ? 0 : 1);
		string txt = "#" + IntegerToString(rank) + " (" + pips_text + ")";

		double arrow_y = c.max_h + (3.0 * point);

		if(c.max_h_time > 0 && c.min_l_time > 0)
		{
			ResetLastError();
			if(ObjectCreate(0, line_name, OBJ_TREND, 0, c.max_h_time, c.max_h, c.min_l_time, c.min_l))
			{
				ObjectSetInteger(0, line_name, OBJPROP_RAY_RIGHT, false);
				ObjectSetInteger(0, line_name, OBJPROP_COLOR, rank_color(rank));
				ObjectSetInteger(0, line_name, OBJPROP_STYLE, STYLE_DOT);
				ObjectSetInteger(0, line_name, OBJPROP_WIDTH, 1);
			}
			else if(InpDebugLogs)
			{
				PrintFormat("RWS: ObjectCreate wave line failed %s err=%d", line_name, GetLastError());
			}
		}

		ResetLastError();
		if(ObjectCreate(0, arr_name, OBJ_ARROW, 0, c.max_h_time, arrow_y))
		{
			ObjectSetInteger(0, arr_name, OBJPROP_ARROWCODE, 234);
			ObjectSetInteger(0, arr_name, OBJPROP_COLOR, rank_color(rank));
			ObjectSetInteger(0, arr_name, OBJPROP_WIDTH, 2);
		}
		else if(InpDebugLogs)
		{
			PrintFormat("RWS: ObjectCreate arrow failed %s err=%d", arr_name, GetLastError());
		}

		ResetLastError();
		if(ObjectCreate(0, txt_name, OBJ_TEXT, 0, center_time, y))
		{
			ObjectSetString(0, txt_name, OBJPROP_TEXT, txt);
			ObjectSetInteger(0, txt_name, OBJPROP_COLOR, rank_color(rank));
			ObjectSetInteger(0, txt_name, OBJPROP_FONTSIZE, 9);
			ObjectSetInteger(0, txt_name, OBJPROP_ANCHOR, ANCHOR_LOWER);
		}
		else if(InpDebugLogs)
		{
			PrintFormat("RWS: ObjectCreate text failed %s err=%d", txt_name, GetLastError());
		}
	}
}

void draw_corner_table_single_regime(const regime_t &reg)
{
	string table_name = "RWS_TABLE_SINGLE";
	ObjectDelete(0, table_name);
	if(!InpShowCornerTable)
		return;
	if(!reg.valid)
		return;

	string text = "Regime: " + reg.name + "\n";
	int n = ArraySize(reg.selected);
	for(int i = 0; i < n; ++i)
	{
		string row = "#" + IntegerToString(i + 1) + " " + DoubleToString(reg.selected[i].pips, InpTruncatePips ? 0 : 1) +
			" pips  " + TimeToString(reg.selected[i].anchor_time, TIME_DATE | TIME_MINUTES);
		text += row;
		if(i < n - 1)
			text += "\n";
	}

	if(ObjectCreate(0, table_name, OBJ_LABEL, 0, 0, 0))
	{
		ObjectSetInteger(0, table_name, OBJPROP_CORNER, CORNER_LEFT_UPPER);
		ObjectSetInteger(0, table_name, OBJPROP_XDISTANCE, 10);
		ObjectSetInteger(0, table_name, OBJPROP_YDISTANCE, 18);
		ObjectSetInteger(0, table_name, OBJPROP_COLOR, clrWhite);
		ObjectSetInteger(0, table_name, OBJPROP_FONTSIZE, 10);
		ObjectSetString(0, table_name, OBJPROP_FONT, "Consolas");
		ObjectSetString(0, table_name, OBJPROP_TEXT, text);
	}
}

int enumerate_regimes(regime_t &regs[])
{
	ArrayResize(regs, 0);
	int total = ObjectsTotal(0, 0, -1);
	int count = 0;
	for(int i = 0; i < total; ++i)
	{
		string name = ObjectName(0, i, 0, -1);
		if(!starts_with(name, InpRegimePrefix))
			continue;
		ENUM_OBJECT obj_type = (ENUM_OBJECT)ObjectGetInteger(0, name, OBJPROP_TYPE);
		if(obj_type != OBJ_RECTANGLE)
			continue;

		datetime t1 = (datetime)ObjectGetInteger(0, name, OBJPROP_TIME, 0);
		datetime t2 = (datetime)ObjectGetInteger(0, name, OBJPROP_TIME, 1);
		if(t1 <= 0 || t2 <= 0)
			continue;

		int n = ArraySize(regs);
		ArrayResize(regs, n + 1);
		regs[n].name = name;
		regs[n].start_time = (t1 < t2 ? t1 : t2);
		regs[n].end_time = (t1 < t2 ? t2 : t1);
		regs[n].idx_lo = -1;
		regs[n].idx_hi = -1;
		regs[n].valid = true;
		regs[n].error_msg = "";
		ArrayResize(regs[n].selected, 0);

		count++;
		if(InpDebugLogs)
			PrintFormat("RWS: found regime rectangle %s t1=%s t2=%s",
				name,
				TimeToString(t1, TIME_DATE | TIME_MINUTES),
				TimeToString(t2, TIME_DATE | TIME_MINUTES));
		if(count >= InpMaxRegimes)
			break;
	}
	return count;
}

void process_all_regimes(const int rates_total,
	const datetime &time[],
	const double &high[],
	const double &low[])
{
	clear_all_rws_objects();

	regime_t regs[];
	int regime_count = enumerate_regimes(regs);
	if(InpDebugLogs)
		PrintFormat("RWS: process_all_regimes rates_total=%d regimes=%d", rates_total, regime_count);
	if(regime_count <= 0)
	{
		ObjectDelete(0, "RWS_TABLE_SINGLE");
		dbg_log("no regimes found: draw at least one OBJ_RECTANGLE with name prefix " + InpRegimePrefix);
		return;
	}

	double pip_size = get_pip_size();
	if(pip_size <= 0.0)
		pip_size = SymbolInfoDouble(_Symbol, SYMBOL_POINT);

	for(int r = 0; r < regime_count; ++r)
	{
		resolve_regime_bounds(regs[r]);
		compute_regime_spikes(regs[r], rates_total, time, high, low, pip_size);
		draw_regime_spikes(regs[r]);
	}

	if(InpShowCornerTable && regime_count == 1)
		draw_corner_table_single_regime(regs[0]);
	else
		ObjectDelete(0, "RWS_TABLE_SINGLE");
}

bool recalc_from_history(void)
{
	int bars = Bars(_Symbol, _Period);
	if(bars <= 0)
		return false;

	datetime time_arr[];
	double high_arr[];
	double low_arr[];
	ArraySetAsSeries(time_arr, true);
	ArraySetAsSeries(high_arr, true);
	ArraySetAsSeries(low_arr, true);

	int copied_t = CopyTime(_Symbol, _Period, 0, bars, time_arr);
	int copied_h = CopyHigh(_Symbol, _Period, 0, bars, high_arr);
	int copied_l = CopyLow(_Symbol, _Period, 0, bars, low_arr);
	if(copied_t <= 0 || copied_h <= 0 || copied_l <= 0)
		return false;

	int rates_total = copied_t;
	if(copied_h < rates_total)
		rates_total = copied_h;
	if(copied_l < rates_total)
		rates_total = copied_l;
	if(rates_total <= 0)
		return false;

	process_all_regimes(rates_total, time_arr, high_arr, low_arr);
	g_need_recalc = false;
	ChartRedraw(0);
	return true;
}

int OnInit(void)
{
	IndicatorSetString(INDICATOR_SHORTNAME, "RegimeWaveSpikes");
	g_need_recalc = true;
	g_last_bar_time = 0;
	return(INIT_SUCCEEDED);
}

void OnDeinit(const int reason)
{
	clear_all_rws_objects();
	ObjectDelete(0, "RWS_TABLE_SINGLE");
}

void OnChartEvent(const int id,
	const long &lparam,
	const double &dparam,
	const string &sparam)
{
	if(id == CHARTEVENT_OBJECT_CREATE ||
		id == CHARTEVENT_OBJECT_CHANGE ||
		id == CHARTEVENT_OBJECT_DELETE ||
		id == CHARTEVENT_OBJECT_DRAG)
	{
		if(starts_with(sparam, InpRegimePrefix))
		{
			g_need_recalc = true;
			if(InpDebugLogs)
				PrintFormat("RWS: chart event id=%d object=%s -> recalc", id, sparam);
			recalc_from_history();
		}
	}
}

int OnCalculate(const int rates_total,
	const int prev_calculated,
	const datetime &time[],
	const double &open[],
	const double &high[],
	const double &low[],
	const double &close[],
	const long &tick_volume[],
	const long &volume[],
	const int &spread[])
{
	if(rates_total <= 0)
		return 0;

	bool new_bar = (time[0] != g_last_bar_time);
	if(new_bar)
	{
		g_last_bar_time = time[0];
		g_need_recalc = true;
	}

	if(!g_need_recalc && rates_total == prev_calculated)
		return rates_total;

	if(InpWaveHorizonBars < 1)
		return rates_total;

	process_all_regimes(rates_total, time, high, low);
	g_need_recalc = false;
	ChartRedraw(0);

	return rates_total;
}
