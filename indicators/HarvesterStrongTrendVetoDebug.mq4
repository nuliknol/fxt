#property strict
#property indicator_chart_window
#property indicator_buffers 3
#property indicator_color1 Lime
#property indicator_color2 Red
#property indicator_color3 Orange

input int TradeDirection = -1;
input int StrongTrendTimeframe = 0;
input int StrongTrendLookbackBars = 5;
input int StrongTrendMinAlignedCandles = 3;
input double StrongTrendMinBodyPercent = 55.0;
input double StrongTrendMaxOppositeWickPercent = 35.0;
input bool StrongTrendRequireDirectionalBreaks = true;
input int StrongTrendMinDirectionalBreaks = 2;
input double StrongTrendMaxRetraceOfImpulsePercent = 25.0;
input double StrongTrendMinImpulsePips = 3.0;
input bool UseExhaustionRelease = true;
input int ExhaustionOppositeCandles = 1;
input double ExhaustionLargeOppositeWickPercent = 50.0;
input int ExhaustionFailToExtendBars = 2;
input double ExhaustionRetraceOfImpulsePercent = 40.0;
input int StrongTrendMaxVetoBars = 5;
input int MarkerOffsetPips = 60;
input bool ShowReasonLabels = true;
input int MaxAnnotatedBars = 80;
input bool ShowCurrentStatusComment = true;

double g_pass_buffer[];
double g_reject_buffer[];
double g_release_buffer[];
double g_pip = 0.0;
string g_obj_prefix = "HarvesterStrongTrendVetoDebug|";

struct veto_eval_result
{
	bool strong_trend;
	bool exhaustion;
	int aligned;
	int breaks;
	double avg_body_percent;
	double max_opp_wick_percent;
	double retrace_percent;
	double impulse_pips;
	string reject_reason;
	string release_reason;
};

double get_pip_value()
{
	if(Digits == 5 || Digits == 3)
		return Point * 10.0;
	return Point;
}

int resolve_tf()
{
	if(StrongTrendTimeframe == 0)
		return Period();
	return StrongTrendTimeframe;
}

double marker_price(int chart_shift, bool above)
{
	double offset = MarkerOffsetPips * g_pip;

	if(above)
		return High[chart_shift] + offset;
	return Low[chart_shift] - offset;
}

void clear_buffers(int rates_total)
{
	for(int i = 0; i < rates_total; i++)
	{
		g_pass_buffer[i] = EMPTY_VALUE;
		g_reject_buffer[i] = EMPTY_VALUE;
		g_release_buffer[i] = EMPTY_VALUE;
	}
}

void clear_labels()
{
	for(int i = ObjectsTotal() - 1; i >= 0; i--)
	{
		string name = ObjectName(i);
		if(StringFind(name, g_obj_prefix) == 0)
			ObjectDelete(name);
	}
}

double candle_body_percent_at(int tf, int shift)
{
	double high = iHigh(Symbol(), tf, shift);
	double low = iLow(Symbol(), tf, shift);
	double open = iOpen(Symbol(), tf, shift);
	double close = iClose(Symbol(), tf, shift);
	double range = high - low;

	if(range <= 0.0)
		return 0.0;

	return 100.0 * MathAbs(close - open) / range;
}

bool is_directional_candle_at(int tf, int shift, int direction)
{
	double open = iOpen(Symbol(), tf, shift);
	double close = iClose(Symbol(), tf, shift);

	if(direction > 0)
		return close > open;
	if(direction < 0)
		return close < open;

	return false;
}

double opposite_wick_percent_at(int tf, int shift, int direction)
{
	double high = iHigh(Symbol(), tf, shift);
	double low = iLow(Symbol(), tf, shift);
	double open = iOpen(Symbol(), tf, shift);
	double close = iClose(Symbol(), tf, shift);
	double range = high - low;

	if(range <= 0.0)
		return 0.0;

	double upper_body = MathMax(open, close);
	double lower_body = MathMin(open, close);
	double upper_wick = high - upper_body;
	double lower_wick = lower_body - low;

	if(direction > 0)
		return 100.0 * lower_wick / range;
	if(direction < 0)
		return 100.0 * upper_wick / range;

	return 0.0;
}

bool has_directional_break_at(int tf, int newer_shift, int older_shift, int direction)
{
	if(direction > 0)
		return iHigh(Symbol(), tf, newer_shift) > iHigh(Symbol(), tf, older_shift);
	if(direction < 0)
		return iLow(Symbol(), tf, newer_shift) < iLow(Symbol(), tf, older_shift);

	return false;
}

double recent_impulse_pips_at(int tf, int base_shift)
{
	double highest = -DBL_MAX;
	double lowest = DBL_MAX;

	for(int offset = 0; offset < StrongTrendLookbackBars; offset++)
	{
		int shift = base_shift + offset;
		double high = iHigh(Symbol(), tf, shift);
		double low = iLow(Symbol(), tf, shift);

		if(high > highest)
			highest = high;
		if(low < lowest)
			lowest = low;
	}

	return (highest - lowest) / g_pip;
}

bool recent_retrace_small_enough_at(int tf, int base_shift, int direction, double &retrace_percent)
{
	double highest = -DBL_MAX;
	double lowest = DBL_MAX;

	for(int offset = 0; offset < StrongTrendLookbackBars; offset++)
	{
		int shift = base_shift + offset;
		double high = iHigh(Symbol(), tf, shift);
		double low = iLow(Symbol(), tf, shift);

		if(high > highest)
			highest = high;
		if(low < lowest)
			lowest = low;
	}

	double close_ref = iClose(Symbol(), tf, base_shift);
	double impulse = highest - lowest;

	if(impulse <= 0.0)
	{
		retrace_percent = 0.0;
		return false;
	}

	double retrace = 0.0;

	if(direction > 0)
		retrace = highest - close_ref;
	else
		retrace = close_ref - lowest;

	if(retrace < 0.0)
		retrace = 0.0;

	retrace_percent = 100.0 * retrace / impulse;
	return retrace_percent <= StrongTrendMaxRetraceOfImpulsePercent;
}

bool has_opposite_candles_at(int tf, int base_shift, int direction)
{
	int opposite = 0;

	for(int offset = 0; offset < ExhaustionOppositeCandles; offset++)
	{
		int shift = base_shift + offset;
		if(is_directional_candle_at(tf, shift, -direction))
			opposite++;
	}

	return opposite >= ExhaustionOppositeCandles;
}

bool has_large_opposite_wick_at(int tf, int base_shift, int direction)
{
	double wick_pct = opposite_wick_percent_at(tf, base_shift, direction);
	return wick_pct >= ExhaustionLargeOppositeWickPercent;
}

bool has_failed_to_extend_at(int tf, int base_shift, int direction)
{
	if(ExhaustionFailToExtendBars <= 0)
		return false;

	for(int offset = 0; offset < ExhaustionFailToExtendBars; offset++)
	{
		int shift = base_shift + offset;
		if(has_directional_break_at(tf, shift, shift + 1, direction))
			return false;
	}

	return true;
}

bool has_large_impulse_retrace_at(int tf, int base_shift, int direction)
{
	double highest = -DBL_MAX;
	double lowest = DBL_MAX;

	for(int offset = 0; offset < StrongTrendLookbackBars; offset++)
	{
		int shift = base_shift + offset;
		double high = iHigh(Symbol(), tf, shift);
		double low = iLow(Symbol(), tf, shift);

		if(high > highest)
			highest = high;
		if(low < lowest)
			lowest = low;
	}

	double close_ref = iClose(Symbol(), tf, base_shift);
	double impulse = highest - lowest;

	if(impulse <= 0.0)
		return false;

	double retrace = 0.0;

	if(direction > 0)
		retrace = highest - close_ref;
	else
		retrace = close_ref - lowest;

	if(retrace < 0.0)
		retrace = 0.0;

	double retrace_pct = 100.0 * retrace / impulse;
	return retrace_pct >= ExhaustionRetraceOfImpulsePercent;
}

bool evaluate_strong_trend_at(int tf, int base_shift, int direction, veto_eval_result &result)
{
	result.strong_trend = false;
	result.aligned = 0;
	result.breaks = 0;
	result.avg_body_percent = 0.0;
	result.max_opp_wick_percent = 0.0;
	result.retrace_percent = 0.0;
	result.impulse_pips = 0.0;
	result.reject_reason = "";

	double body_sum = 0.0;

	for(int offset = 0; offset < StrongTrendLookbackBars; offset++)
	{
		int shift = base_shift + offset;

		if(is_directional_candle_at(tf, shift, direction))
		{
			result.aligned++;

			double body_pct = candle_body_percent_at(tf, shift);
			body_sum += body_pct;

			double wick_pct = opposite_wick_percent_at(tf, shift, direction);
			if(wick_pct > result.max_opp_wick_percent)
				result.max_opp_wick_percent = wick_pct;
		}

		if(offset < StrongTrendLookbackBars - 1)
		{
			int newer_shift = base_shift + offset;
			int older_shift = base_shift + offset + 1;
			if(has_directional_break_at(tf, newer_shift, older_shift, direction))
				result.breaks++;
		}
	}

	if(result.aligned < StrongTrendMinAlignedCandles)
	{
		result.reject_reason = "aligned";
		return false;
	}

	result.avg_body_percent = body_sum / MathMax(1, result.aligned);

	if(result.avg_body_percent < StrongTrendMinBodyPercent)
	{
		result.reject_reason = "body";
		return false;
	}

	if(result.max_opp_wick_percent > StrongTrendMaxOppositeWickPercent)
	{
		result.reject_reason = "wick";
		return false;
	}

	if(StrongTrendRequireDirectionalBreaks && result.breaks < StrongTrendMinDirectionalBreaks)
	{
		result.reject_reason = "breaks";
		return false;
	}

	result.impulse_pips = recent_impulse_pips_at(tf, base_shift);
	if(result.impulse_pips < StrongTrendMinImpulsePips)
	{
		result.reject_reason = "impulse";
		return false;
	}

	if(!recent_retrace_small_enough_at(tf, base_shift, direction, result.retrace_percent))
	{
		result.reject_reason = "retrace";
		return false;
	}

	result.strong_trend = true;
	return true;
}

bool evaluate_exhaustion_at(int tf, int base_shift, int direction, veto_eval_result &result)
{
	result.exhaustion = false;
	result.release_reason = "";

	if(!UseExhaustionRelease)
		return false;

	if(has_opposite_candles_at(tf, base_shift, direction))
	{
		result.release_reason = "opp_candle";
		result.exhaustion = true;
		return true;
	}

	if(has_large_opposite_wick_at(tf, base_shift, direction))
	{
		result.release_reason = "opp_wick";
		result.exhaustion = true;
		return true;
	}

	if(has_failed_to_extend_at(tf, base_shift, direction))
	{
		result.release_reason = "failed_ext";
		result.exhaustion = true;
		return true;
	}

	if(has_large_impulse_retrace_at(tf, base_shift, direction))
	{
		result.release_reason = "large_ret";
		result.exhaustion = true;
		return true;
	}

	return false;
}

string build_label_text(veto_eval_result &result)
{
	if(result.strong_trend && !result.exhaustion)
	{
		return "PASS "
			+ "A" + IntegerToString(result.aligned)
			+ " B" + DoubleToString(result.avg_body_percent, 0)
			+ " W" + DoubleToString(result.max_opp_wick_percent, 0)
			+ " Br" + IntegerToString(result.breaks)
			+ " R" + DoubleToString(result.retrace_percent, 0);
	}

	if(result.strong_trend && result.exhaustion)
		return "REL " + result.release_reason;

	return "REJ " + result.reject_reason
		+ " A" + IntegerToString(result.aligned)
		+ " B" + DoubleToString(result.avg_body_percent, 0)
		+ " W" + DoubleToString(result.max_opp_wick_percent, 0)
		+ " Br" + IntegerToString(result.breaks);
}

string veto_gap_text(veto_eval_result &result)
{
	string text = "";
	int aligned_gap = StrongTrendMinAlignedCandles - result.aligned;
	double body_gap = StrongTrendMinBodyPercent - result.avg_body_percent;
	double wick_gap = result.max_opp_wick_percent - StrongTrendMaxOppositeWickPercent;
	int break_gap = StrongTrendMinDirectionalBreaks - result.breaks;
	double retrace_gap = result.retrace_percent - StrongTrendMaxRetraceOfImpulsePercent;
	double impulse_gap = StrongTrendMinImpulsePips - result.impulse_pips;

	if(aligned_gap > 0)
		text += " aligned+" + IntegerToString(aligned_gap);
	if(body_gap > 0.0)
		text += " body+" + DoubleToString(body_gap, 1);
	if(wick_gap > 0.0)
		text += " wick-" + DoubleToString(wick_gap, 1);
	if(StrongTrendRequireDirectionalBreaks && break_gap > 0)
		text += " breaks+" + IntegerToString(break_gap);
	if(impulse_gap > 0.0)
		text += " impulse+" + DoubleToString(impulse_gap, 1);
	if(retrace_gap > 0.0)
		text += " retrace-" + DoubleToString(retrace_gap, 1);

	if(text == "")
		text = " PASS";

	return text;
}

void update_current_status_comment(int tf)
{
	if(!ShowCurrentStatusComment)
	{
		Comment("");
		return;
	}

	veto_eval_result result;
	evaluate_strong_trend_at(tf, 1, TradeDirection, result);
	evaluate_exhaustion_at(tf, 1, TradeDirection, result);

	string status = "REJECT";
	if(result.strong_trend && !result.exhaustion)
		status = "PASS";
	else if(result.strong_trend && result.exhaustion)
		status = "RELEASE";

	string text =
		"Harvester Strong Trend Veto Debug" +
		"\nStatus: " + status +
		"\nDirection: " + IntegerToString(TradeDirection) +
		"\nAligned: " + IntegerToString(result.aligned) + " / " + IntegerToString(StrongTrendMinAlignedCandles) +
		"\nAvg body %: " + DoubleToString(result.avg_body_percent, 2) + " / " + DoubleToString(StrongTrendMinBodyPercent, 2) +
		"\nMax opp wick %: " + DoubleToString(result.max_opp_wick_percent, 2) + " / " + DoubleToString(StrongTrendMaxOppositeWickPercent, 2) +
		"\nBreaks: " + IntegerToString(result.breaks) + " / " + IntegerToString(StrongTrendMinDirectionalBreaks) +
		"\nImpulse pips: " + DoubleToString(result.impulse_pips, 2) + " / " + DoubleToString(StrongTrendMinImpulsePips, 2) +
		"\nRetrace %: " + DoubleToString(result.retrace_percent, 2) + " / " + DoubleToString(StrongTrendMaxRetraceOfImpulsePercent, 2) +
		"\nExhaustion: " + (result.exhaustion ? "1" : "0") +
		"\nRelease reason: " + result.release_reason +
		"\nGap to pass:" + veto_gap_text(result);

	Comment(text);
}

void add_label(string name, datetime t, double price, color clr, string text)
{
	ObjectCreate(name, OBJ_TEXT, 0, t, price);
	ObjectSetText(name, text, 8, "Arial", clr);
}

int OnInit()
{
	g_pip = get_pip_value();

	SetIndexBuffer(0, g_pass_buffer);
	SetIndexStyle(0, DRAW_ARROW, STYLE_SOLID, 1, Lime);
	SetIndexArrow(0, 233);
	SetIndexLabel(0, "VetoPass");

	SetIndexBuffer(1, g_reject_buffer);
	SetIndexStyle(1, DRAW_ARROW, STYLE_SOLID, 1, Red);
	SetIndexArrow(1, 234);
	SetIndexLabel(1, "VetoReject");

	SetIndexBuffer(2, g_release_buffer);
	SetIndexStyle(2, DRAW_ARROW, STYLE_SOLID, 1, Orange);
	SetIndexArrow(2, 251);
	SetIndexLabel(2, "VetoRelease");

	IndicatorShortName("Harvester Strong Trend Veto Debug");
	return INIT_SUCCEEDED;
}

int OnCalculate(
	const int rates_total,
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
	int tf = resolve_tf();
	int tf_bars = iBars(Symbol(), tf);
	int min_required = StrongTrendLookbackBars + MathMax(ExhaustionOppositeCandles, ExhaustionFailToExtendBars) + 3;

	clear_buffers(rates_total);

	if(ShowReasonLabels)
		clear_labels();

	if(tf_bars <= min_required)
		return rates_total;

	update_current_status_comment(tf);

	int annotated = 0;

	for(int shift = 1; shift < tf_bars - min_required; shift++)
	{
		datetime decision_time = iTime(Symbol(), tf, shift);
		int chart_shift = iBarShift(Symbol(), Period(), decision_time, true);
		if(chart_shift < 1 || chart_shift >= rates_total)
			continue;

		veto_eval_result result;
		evaluate_strong_trend_at(tf, shift, TradeDirection, result);
		evaluate_exhaustion_at(tf, shift, TradeDirection, result);

		bool draw_above = (TradeDirection < 0);
		double price = marker_price(chart_shift, draw_above);

		if(result.strong_trend && !result.exhaustion)
			g_pass_buffer[chart_shift] = price;
		else if(result.strong_trend && result.exhaustion)
			g_release_buffer[chart_shift] = price;
		else
			g_reject_buffer[chart_shift] = price;

		if(ShowReasonLabels && annotated < MaxAnnotatedBars)
		{
			string obj_name = g_obj_prefix + IntegerToString((int)decision_time);
			color label_color = Red;

			if(result.strong_trend && !result.exhaustion)
				label_color = Lime;
			else if(result.strong_trend && result.exhaustion)
				label_color = Orange;

			add_label(obj_name, time[chart_shift], price, label_color, build_label_text(result));
			annotated++;
		}
	}

	return rates_total;
}

void OnDeinit(const int reason)
{
	clear_labels();
	Comment("");
}
