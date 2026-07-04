#ifndef HARVESTER_MODE_PROFIT_WAVE_TRAIL_MQH
#define HARVESTER_MODE_PROFIT_WAVE_TRAIL_MQH

#define PROFIT_WAVE_BASELINE_BARS 2

enum StrongTrendCandleClass
{
	ST_CANDLE_NEUTRAL = 0,
	ST_CANDLE_TREND = 1,
	ST_CANDLE_COUNTER = 2,
	ST_CANDLE_INSIDE = 3,
	ST_CANDLE_OUTSIDE = 4,
	ST_CANDLE_DOJI = 5
};

input double ProfitWaveArmGridUnits = 3.0;
input double ProfitWaveSoftDropPercent = 8.0;
input int ProfitWaveConfirmTicks = 5;

input bool UseStrongTrendCandleVeto = false;
input int StrongTrendMinCandles = 3;
input int StrongTrendMaxCounterCandles = 1;
input bool StrongTrendIgnoreInsideCandles = true;
input bool StrongTrendIgnoreOutsideCandles = true;
input double StrongTrendMinBodyPips = 0.2;
input bool StrongTrendShowChartDebug = false;
input bool StrongTrendPrintDebug = false;
input bool StrongTrendPrintCandleTrace = false;

bool g_profit_wave_armed = false;
bool g_profit_wave_warning_active = false;

double g_profit_wave_baseline_pips = 0.0;
double g_profit_wave_now_pips = 0.0;
double g_profit_wave_peak_pips = 0.0;
double g_profit_wave_drop_percent = 0.0;
double g_profit_wave_arm_threshold_pips = 0.0;

int g_profit_wave_confirm_ticks = 0;

datetime g_profit_wave_last_bar_time = 0;

double g_profit_wave_snapshots[];
int g_profit_wave_snapshot_count = 0;

bool g_strong_trend_veto_active = false;
int g_strong_trend_count = 0;
int g_strong_trend_counter_count = 0;
datetime g_strong_trend_last_bar_time = 0;
datetime g_strong_trend_veto_started_at = 0;
int g_strong_trend_doji_ignored = 0;
int g_strong_trend_inside_ignored = 0;
int g_strong_trend_outside_ignored = 0;
int g_strong_trend_veto_activations = 0;
int g_strong_trend_veto_releases = 0;
int g_strong_trend_veto_blocks = 0;
int g_strong_trend_last_candle_class = ST_CANDLE_NEUTRAL;
double g_strong_trend_last_body_pips = 0.0;

int g_dbg_profit_wave_signal = 0;
int g_dbg_profit_wave_reason_code = 0;
int g_soft_drop_harvests = 0;

string StrategyModeName()
{
	return "PROFIT_WAVE_TRAIL";
}

bool StrategyValidateInputs()
{
	if(ProfitWaveArmGridUnits <= 0.0)
		return false;
	if(ProfitWaveSoftDropPercent <= 0.0)
		return false;
	if(ProfitWaveConfirmTicks < 1)
		return false;
	if(UseStrongTrendCandleVeto)
	{
		if(StrongTrendMinCandles < 1)
			return false;
		if(StrongTrendMaxCounterCandles < 0)
			return false;
		if(StrongTrendMinBodyPips < 0.0)
			return false;
	}

	return true;
}

double PipPoint()
{
	if(g_pip > 0.0)
		return g_pip;
	if(Digits == 3 || Digits == 5)
		return Point * 10.0;
	return Point;
}

string DoubleToPipText(double value)
{
	return DoubleToString(value, 1) + "p";
}

string StrongTrendCandleClassName(StrongTrendCandleClass cls)
{
	if(cls == ST_CANDLE_TREND)
		return "TREND";
	if(cls == ST_CANDLE_COUNTER)
		return "COUNTER";
	if(cls == ST_CANDLE_INSIDE)
		return "INSIDE";
	if(cls == ST_CANDLE_OUTSIDE)
		return "OUTSIDE";
	if(cls == ST_CANDLE_DOJI)
		return "DOJI";
	return "NEUTRAL";
}

void PrintStrongTrendCandleTrace(
	int shift,
	StrongTrendCandleClass cls,
	int trend_count_before,
	int counter_count_before,
	bool veto_active_before)
{
	if(!StrongTrendPrintCandleTrace)
		return;

	Print(
		"StrongTrendCandleTrace time=", TimeToString(Time[shift], TIME_DATE | TIME_SECONDS),
		" class=", StrongTrendCandleClassName(cls),
		" dir=", TradeDirection,
		" open=", DoubleToString(Open[shift], Digits),
		" high=", DoubleToString(High[shift], Digits),
		" low=", DoubleToString(Low[shift], Digits),
		" close=", DoubleToString(Close[shift], Digits),
		" body_pips=", DoubleToString(StrongTrendCandleBodyPips(shift), 2),
		" veto_before=", (veto_active_before ? 1 : 0),
		" trend_before=", trend_count_before,
		" counter_before=", counter_count_before,
		" veto_after=", (g_strong_trend_veto_active ? 1 : 0),
		" trend_after=", g_strong_trend_count,
		" counter_after=", g_strong_trend_counter_count
	);
}

string StrongTrendDebugObjectPrefix()
{
	return "HarvesterStrongTrend|";
}

void ClearStrongTrendDebugObjects()
{
	for(int i = ObjectsTotal() - 1; i >= 0; i--)
	{
		string name = ObjectName(i);
		if(StringFind(name, StrongTrendDebugObjectPrefix()) == 0)
			ObjectDelete(name);
	}
}

void DrawStrongTrendTrendMarker(datetime t, double anchor_price)
{
	if(!StrongTrendShowChartDebug)
		return;

	double offset = 30.0 * PipPoint();
	double price = anchor_price + offset;
	string name = StrongTrendDebugObjectPrefix() + IntegerToString((int)t);

	if(ObjectFind(name) >= 0)
		ObjectDelete(name);

	ObjectCreate(name, OBJ_ARROW, 0, t, price);
	ObjectSet(name, OBJPROP_ARROWCODE, 108);
	ObjectSet(name, OBJPROP_COLOR, clrLimeGreen);
}

void ResetStrongTrendCandleVeto()
{
	g_strong_trend_veto_active = false;
	g_strong_trend_count = 0;
	g_strong_trend_counter_count = 0;
	g_strong_trend_last_bar_time = 0;
	g_strong_trend_veto_started_at = 0;
	g_strong_trend_doji_ignored = 0;
	g_strong_trend_inside_ignored = 0;
	g_strong_trend_outside_ignored = 0;
	g_strong_trend_veto_activations = 0;
	g_strong_trend_veto_releases = 0;
	g_strong_trend_veto_blocks = 0;
	g_strong_trend_last_candle_class = ST_CANDLE_NEUTRAL;
	g_strong_trend_last_body_pips = 0.0;
	ClearStrongTrendDebugObjects();
}

void StrategyResetState()
{
	g_profit_wave_armed = false;
	g_profit_wave_warning_active = false;
	g_profit_wave_baseline_pips = 0.0;
	g_profit_wave_now_pips = 0.0;
	g_profit_wave_peak_pips = 0.0;
	g_profit_wave_drop_percent = 0.0;
	g_profit_wave_arm_threshold_pips = 0.0;
	g_profit_wave_confirm_ticks = 0;
	g_profit_wave_last_bar_time = 0;
	g_profit_wave_snapshot_count = 0;

	ArrayResize(g_profit_wave_snapshots, PROFIT_WAVE_BASELINE_BARS + 2);
	for(int i = 0; i < ArraySize(g_profit_wave_snapshots); i++)
		g_profit_wave_snapshots[i] = 0.0;

	g_dbg_profit_wave_signal = 0;
	g_dbg_profit_wave_reason_code = 0;
	g_soft_drop_harvests = 0;

	ResetStrongTrendCandleVeto();
}

bool ProfitWaveIsNewBar()
{
	datetime t = iTime(Symbol(), Period(), 0);

	if(t != g_profit_wave_last_bar_time)
	{
		g_profit_wave_last_bar_time = t;
		return true;
	}

	return false;
}

void ProfitWaveUpdateSnapshots()
{
	if(!ProfitWaveIsNewBar())
		return;

	double now = GetTotalPositiveProfitPips();
	int size = PROFIT_WAVE_BASELINE_BARS + 2;

	if(ArraySize(g_profit_wave_snapshots) != size)
		ArrayResize(g_profit_wave_snapshots, size);

	for(int i = size - 1; i > 0; i--)
		g_profit_wave_snapshots[i] = g_profit_wave_snapshots[i - 1];

	g_profit_wave_snapshots[0] = now;

	if(g_profit_wave_snapshot_count < size)
		g_profit_wave_snapshot_count++;
}

bool ProfitWaveMovingBaseline(double &baseline)
{
	if(g_profit_wave_snapshot_count < PROFIT_WAVE_BASELINE_BARS + 1)
		return false;

	baseline = DBL_MAX;

	for(int i = 1; i <= PROFIT_WAVE_BASELINE_BARS; i++)
	{
		double v = g_profit_wave_snapshots[i];
		if(v < baseline)
			baseline = v;
	}

	if(baseline == DBL_MAX)
		return false;

	return true;
}

bool ProfitWaveTryArm()
{
	double baseline = 0.0;

	if(!ProfitWaveMovingBaseline(baseline))
	{
		g_dbg_profit_wave_reason_code = 6;
		return false;
	}

	g_profit_wave_baseline_pips = baseline;

	double increase = g_profit_wave_now_pips - baseline;
	if(increase < 0.0)
		increase = 0.0;

	if(increase < g_profit_wave_arm_threshold_pips)
		return false;

	g_profit_wave_armed = true;
	g_profit_wave_warning_active = false;
	g_profit_wave_confirm_ticks = 0;
	g_profit_wave_peak_pips = g_profit_wave_now_pips;
	g_profit_wave_drop_percent = 0.0;

	g_dbg_profit_wave_reason_code = 1;
	return true;
}

void ProfitWaveUpdatePeak()
{
	if(!g_profit_wave_armed)
		return;

	if(g_profit_wave_now_pips > g_profit_wave_peak_pips)
	{
		g_profit_wave_peak_pips = g_profit_wave_now_pips;
		g_profit_wave_warning_active = false;
		g_profit_wave_confirm_ticks = 0;
		g_profit_wave_drop_percent = 0.0;
		g_dbg_profit_wave_reason_code = 2;
	}
}

double StrongTrendCandleBodyPips(int shift)
{
	double body_price = MathAbs(Close[shift] - Open[shift]);
	double pip = PipPoint();

	if(pip <= 0.0)
		return 0.0;

	return body_price / pip;
}

StrongTrendCandleClass ClassifyStrongTrendCandle(int shift, int direction)
{
	double h1 = High[shift];
	double l1 = Low[shift];
	double o1 = Open[shift];
	double c1 = Close[shift];
	double h2 = High[shift + 1];
	double l2 = Low[shift + 1];
	double body_pips = StrongTrendCandleBodyPips(shift);

	if(body_pips < StrongTrendMinBodyPips)
		return ST_CANDLE_DOJI;

	bool is_inside = (h1 <= h2 && l1 >= l2);
	bool is_outside = (h1 >= h2 && l1 <= l2 && (h1 > h2 || l1 < l2));

	if(is_inside && StrongTrendIgnoreInsideCandles)
		return ST_CANDLE_INSIDE;

	if(is_outside && StrongTrendIgnoreOutsideCandles)
		return ST_CANDLE_OUTSIDE;

	if(direction > 0)
	{
		if(c1 > o1)
			return ST_CANDLE_TREND;
		if(c1 < o1)
			return ST_CANDLE_COUNTER;
	}

	if(direction < 0)
	{
		if(c1 < o1)
			return ST_CANDLE_TREND;
		if(c1 > o1)
			return ST_CANDLE_COUNTER;
	}

	return ST_CANDLE_NEUTRAL;
}

void UpdateStrongTrendCandleVeto()
{
	if(!UseStrongTrendCandleVeto)
	{
		ResetStrongTrendCandleVeto();
		return;
	}

	if(Bars < 10)
		return;

	datetime current_bar_time = Time[0];
	if(current_bar_time == g_strong_trend_last_bar_time)
		return;

	g_strong_trend_last_bar_time = current_bar_time;

	int direction = TradeDirection;
	if(direction != 1 && direction != -1)
		return;

	bool veto_active_before = g_strong_trend_veto_active;
	int trend_count_before = g_strong_trend_count;
	int counter_count_before = g_strong_trend_counter_count;
	StrongTrendCandleClass cls = ClassifyStrongTrendCandle(1, direction);
	g_strong_trend_last_candle_class = cls;
	g_strong_trend_last_body_pips = StrongTrendCandleBodyPips(1);

	if(cls == ST_CANDLE_DOJI)
	{
		g_strong_trend_doji_ignored++;

			if(StrongTrendPrintDebug)
				Print("StrongTrendCandleVeto: DOJI ignored. body_pips=", g_strong_trend_last_body_pips);

			PrintStrongTrendCandleTrace(1, cls, trend_count_before, counter_count_before, veto_active_before);
			return;
	}

	if(cls == ST_CANDLE_INSIDE)
	{
		g_strong_trend_inside_ignored++;

		PrintStrongTrendCandleTrace(1, cls, trend_count_before, counter_count_before, veto_active_before);
		return;
	}

	if(cls == ST_CANDLE_OUTSIDE)
	{
		g_strong_trend_outside_ignored++;

		PrintStrongTrendCandleTrace(1, cls, trend_count_before, counter_count_before, veto_active_before);
		return;
	}

	if(cls == ST_CANDLE_TREND)
	{
		g_strong_trend_count++;
		g_strong_trend_counter_count = 0;

		if(!g_strong_trend_veto_active && g_strong_trend_count >= StrongTrendMinCandles)
		{
			g_strong_trend_veto_active = true;
			g_strong_trend_veto_started_at = Time[1];
			g_strong_trend_veto_activations++;

			if(StrongTrendPrintDebug)
				Print("StrongTrendCandleVeto ON: trend_count=", g_strong_trend_count);
		}

		if(StrongTrendShowChartDebug && g_strong_trend_count >= StrongTrendMinCandles)
			DrawStrongTrendTrendMarker(Time[1], High[1]);

		PrintStrongTrendCandleTrace(1, cls, trend_count_before, counter_count_before, veto_active_before);
		return;
	}

	if(cls == ST_CANDLE_COUNTER)
	{
		if(g_strong_trend_veto_active)
		{
			g_strong_trend_counter_count++;

				if(g_strong_trend_counter_count > StrongTrendMaxCounterCandles)
				{
					if(StrongTrendPrintDebug)
						Print("StrongTrendCandleVeto OFF: counter_count=", g_strong_trend_counter_count);

				g_strong_trend_veto_active = false;
				g_strong_trend_count = 0;
				g_strong_trend_counter_count = 0;
				g_strong_trend_veto_started_at = 0;
				g_strong_trend_veto_releases++;
			}
		}
			else
			{
				g_strong_trend_count = 0;
				g_strong_trend_counter_count = 0;
			}

			PrintStrongTrendCandleTrace(1, cls, trend_count_before, counter_count_before, veto_active_before);
			return;
	}
}

void StrategyOnTickPrelude()
{
	ProfitWaveUpdateSnapshots();
	UpdateStrongTrendCandleVeto();
}

void StrategyUpdateSignalState()
{
	g_profit_wave_now_pips = GetTotalPositiveProfitPips();
	g_profit_wave_arm_threshold_pips = ProfitWaveArmGridUnits * GridStepPips;

	if(g_profit_wave_peak_pips > 0.0)
	{
		double drop = g_profit_wave_peak_pips - g_profit_wave_now_pips;
		if(drop < 0.0)
			drop = 0.0;

		g_profit_wave_drop_percent = 100.0 * drop / g_profit_wave_peak_pips;
	}
	else
	{
		g_profit_wave_drop_percent = 0.0;
	}
}

void LogProfitWaveDecision(string reason)
{
	Print(
		"HARVEST_DECISION mode=", StrategyModeName(),
		" reason=", reason,
		" now_pips=", DoubleToString(g_profit_wave_now_pips, 2),
		" baseline_pips=", DoubleToString(g_profit_wave_baseline_pips, 2),
		" peak_pips=", DoubleToString(g_profit_wave_peak_pips, 2),
		" drop_percent=", DoubleToString(g_profit_wave_drop_percent, 2),
		" soft_drop_percent=", DoubleToString(ProfitWaveSoftDropPercent, 2),
		" confirm_ticks=", g_profit_wave_confirm_ticks,
		" veto_active=", (g_strong_trend_veto_active ? 1 : 0),
		" trend_count=", g_strong_trend_count,
		" counter_count=", g_strong_trend_counter_count,
		" candle_class=", g_strong_trend_last_candle_class,
		" body_pips=", DoubleToString(g_strong_trend_last_body_pips, 2)
	);
}

bool StrategyShouldHarvest()
{
	g_dbg_profit_wave_signal = 0;
	g_dbg_profit_wave_reason_code = 0;

	if(!HarvestPreconditionsMet())
		return false;

	g_profit_wave_now_pips = GetTotalPositiveProfitPips();
	g_profit_wave_arm_threshold_pips = ProfitWaveArmGridUnits * GridStepPips;

	if(!g_profit_wave_armed)
	{
		ProfitWaveTryArm();
		return false;
	}

	ProfitWaveUpdatePeak();

	if(g_profit_wave_peak_pips <= 0.0)
		return false;

	double drop = g_profit_wave_peak_pips - g_profit_wave_now_pips;
	if(drop < 0.0)
		drop = 0.0;

	g_profit_wave_drop_percent = 100.0 * drop / g_profit_wave_peak_pips;

	bool soft_drop = false;

	if(g_profit_wave_drop_percent >= ProfitWaveSoftDropPercent)
	{
		if(!g_profit_wave_warning_active)
		{
			g_profit_wave_warning_active = true;
			g_profit_wave_confirm_ticks = 1;
			g_dbg_profit_wave_reason_code = 3;
			return false;
		}

		g_profit_wave_confirm_ticks++;

		if(g_profit_wave_confirm_ticks >= ProfitWaveConfirmTicks)
			soft_drop = true;
	}
	else
	{
		g_profit_wave_warning_active = false;
		g_profit_wave_confirm_ticks = 0;
	}

	if(!soft_drop)
		return false;

	if(UseStrongTrendCandleVeto && g_strong_trend_veto_active)
	{
		g_strong_trend_veto_blocks++;

		if(StrongTrendPrintDebug)
		{
			Print(
				"StrongTrendCandleVeto BLOCKED soft drop. trend_count=",
				g_strong_trend_count,
				" counter_count=",
				g_strong_trend_counter_count
			);
		}

		LogProfitWaveDecision("soft_drop_blocked");
		return false;
	}

	g_soft_drop_harvests++;
	g_dbg_profit_wave_signal = 1;
	g_dbg_profit_wave_reason_code = 4;
	LogProfitWaveDecision("soft_drop_harvest");

	return true;
}

void StrategyOnHarvestSuccess()
{
	g_profit_wave_armed = false;
	g_profit_wave_warning_active = false;
	g_profit_wave_baseline_pips = 0.0;
	g_profit_wave_now_pips = GetTotalPositiveProfitPips();
	g_profit_wave_peak_pips = g_profit_wave_now_pips;
	g_profit_wave_drop_percent = 0.0;
	g_profit_wave_confirm_ticks = 0;
	g_dbg_profit_wave_signal = 0;
	g_dbg_profit_wave_reason_code = 0;
}

void StrategyAppendComment(string &text)
{
	text +=
		"\nPROFIT_WAVE_TRAIL" +
		"\nPW now pips: " + DoubleToString(g_profit_wave_now_pips, 1) +
		"\nPW baseline pips: " + DoubleToString(g_profit_wave_baseline_pips, 1) +
		"\nPW peak pips: " + DoubleToString(g_profit_wave_peak_pips, 1) +
		"\nPW drop %: " + DoubleToString(g_profit_wave_drop_percent, 2) +
		"\nPW arm grid units: " + DoubleToString(ProfitWaveArmGridUnits, 2) +
		"\nPW arm threshold pips: " + DoubleToString(g_profit_wave_arm_threshold_pips, 1) +
		"\nPW armed: " + IntegerToString(g_profit_wave_armed ? 1 : 0) +
		"\nPW warning: " + IntegerToString(g_profit_wave_warning_active ? 1 : 0) +
		"\nPW confirm ticks: " + IntegerToString(g_profit_wave_confirm_ticks) +
		"\nPW signal: " + IntegerToString(g_dbg_profit_wave_signal) +
		"\nPW reason: " + IntegerToString(g_dbg_profit_wave_reason_code) +
		"\nST veto/trend/counter: " +
			IntegerToString(g_strong_trend_veto_active ? 1 : 0) + " / " +
			IntegerToString(g_strong_trend_count) + " / " +
			IntegerToString(g_strong_trend_counter_count) +
		"\nST doji/in/out/blocks: " +
			IntegerToString(g_strong_trend_doji_ignored) + " / " +
			IntegerToString(g_strong_trend_inside_ignored) + " / " +
			IntegerToString(g_strong_trend_outside_ignored) + " / " +
			IntegerToString(g_strong_trend_veto_blocks);
}

void StrategyOnDeinit(const int reason)
{
	Print(
		"StrongTrendCandleVeto stats: doji_ignored=", g_strong_trend_doji_ignored,
		" inside_ignored=", g_strong_trend_inside_ignored,
		" outside_ignored=", g_strong_trend_outside_ignored,
		" veto_activations=", g_strong_trend_veto_activations,
		" veto_releases=", g_strong_trend_veto_releases,
		" soft_drop_blocks=", g_strong_trend_veto_blocks,
		" soft_drop_harvests=", g_soft_drop_harvests
	);
	ClearStrongTrendDebugObjects();
}

#endif
