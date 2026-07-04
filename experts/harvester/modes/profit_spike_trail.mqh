#ifndef HARVESTER_MODE_PROFIT_SPIKE_TRAIL_MQH
#define HARVESTER_MODE_PROFIT_SPIKE_TRAIL_MQH

input int ProfitSpikeBaselineBars = 2;
input bool ProfitSpikeUseMinBaseline = true;
input double ProfitSpikeIgnoreBaselineBelowPips = 100.0;
input double ProfitSpikeIgnoreIncreaseBelowPips = 100.0;
input double ProfitSpikePercent = 15.0;
input double ProfitSpikeDropFromPeakPercent = 10.0;
input double ProfitSpikeDropFromPeakPips = 80.0;
input bool ProfitSpikeUseDropPercent = true;
input bool ProfitSpikeUseDropPips = true;
input bool ProfitSpikeUseNoNewPeakBars = true;
input int ProfitSpikeMaxBarsWithoutNewPeak = 2;
input double ProfitSpikeNoNewPeakMinDropPercent = 5.0;
input double ProfitSpikeMinCurrentProfitPips = 100.0;

double g_profit_spike_snapshots[];
int g_profit_spike_snapshot_count = 0;
bool g_profit_spike_armed = false;
double g_profit_spike_baseline_pips = 0.0;
double g_profit_spike_peak_pips = 0.0;
double g_profit_spike_now_pips = 0.0;
datetime g_profit_spike_peak_bar_time = 0;
int g_profit_spike_bars_since_peak = 0;
datetime g_profit_spike_last_bar_time = 0;

double g_dbg_profit_spike_baseline_pips = 0.0;
double g_dbg_profit_spike_now_pips = 0.0;
double g_dbg_profit_spike_peak_pips = 0.0;
double g_dbg_profit_spike_increase_pips = 0.0;
double g_dbg_profit_spike_increase_percent = 0.0;
double g_dbg_profit_spike_drop_pips = 0.0;
double g_dbg_profit_spike_drop_percent = 0.0;
int g_dbg_profit_spike_armed = 0;
int g_dbg_profit_spike_bars_since_peak = 0;
int g_dbg_profit_spike_signal = 0;
int g_dbg_profit_spike_reason_code = 0;

string StrategyModeName()
{
	return "PROFIT_SPIKE_TRAIL";
}

bool StrategyValidateInputs()
{
	if(ProfitSpikeBaselineBars < 1)
		return false;
	if(ProfitSpikeIgnoreBaselineBelowPips < 0.0)
		return false;
	if(ProfitSpikeIgnoreIncreaseBelowPips < 0.0)
		return false;
	if(ProfitSpikePercent < 0.0)
		return false;
	if(ProfitSpikeDropFromPeakPercent < 0.0)
		return false;
	if(ProfitSpikeDropFromPeakPips < 0.0)
		return false;
	if(ProfitSpikeMaxBarsWithoutNewPeak < 0)
		return false;
	if(ProfitSpikeNoNewPeakMinDropPercent < 0.0)
		return false;
	if(ProfitSpikeMinCurrentProfitPips < 0.0)
		return false;
	return true;
}

void StrategyResetState()
{
	ArrayResize(g_profit_spike_snapshots, ProfitSpikeBaselineBars + 2);
	ArrayInitialize(g_profit_spike_snapshots, 0.0);
	g_profit_spike_snapshot_count = 0;
	g_profit_spike_armed = false;
	g_profit_spike_baseline_pips = 0.0;
	g_profit_spike_peak_pips = 0.0;
	g_profit_spike_now_pips = 0.0;
	g_profit_spike_peak_bar_time = 0;
	g_profit_spike_bars_since_peak = 0;
	g_profit_spike_last_bar_time = 0;
	g_dbg_profit_spike_baseline_pips = 0.0;
	g_dbg_profit_spike_now_pips = 0.0;
	g_dbg_profit_spike_peak_pips = 0.0;
	g_dbg_profit_spike_increase_pips = 0.0;
	g_dbg_profit_spike_increase_percent = 0.0;
	g_dbg_profit_spike_drop_pips = 0.0;
	g_dbg_profit_spike_drop_percent = 0.0;
	g_dbg_profit_spike_armed = 0;
	g_dbg_profit_spike_bars_since_peak = 0;
	g_dbg_profit_spike_signal = 0;
	g_dbg_profit_spike_reason_code = 0;
}

bool IsNewBarForProfitSpike()
{
	datetime t = iTime(Symbol(), Period(), 0);

	if(t != g_profit_spike_last_bar_time)
	{
		g_profit_spike_last_bar_time = t;
		return true;
	}

	return false;
}

void UpdateProfitSpikeSnapshots()
{
	if(!IsNewBarForProfitSpike())
		return;

	double now = GetTotalPositiveProfitPips();
	int size = ProfitSpikeBaselineBars + 2;

	if(ArraySize(g_profit_spike_snapshots) != size)
		ArrayResize(g_profit_spike_snapshots, size);

	for(int i = size - 1; i > 0; i--)
		g_profit_spike_snapshots[i] = g_profit_spike_snapshots[i - 1];

	g_profit_spike_snapshots[0] = now;

	if(g_profit_spike_snapshot_count < size)
		g_profit_spike_snapshot_count++;

	if(g_profit_spike_armed)
		g_profit_spike_bars_since_peak++;
}

bool GetProfitSpikeMovingBaseline(double &baseline)
{
	if(ProfitSpikeBaselineBars < 1)
		return false;
	if(g_profit_spike_snapshot_count < ProfitSpikeBaselineBars + 1)
		return false;

	if(ProfitSpikeUseMinBaseline)
	{
		baseline = DBL_MAX;

		for(int i = 1; i <= ProfitSpikeBaselineBars; i++)
		{
			double v = g_profit_spike_snapshots[i];
			if(v < baseline)
				baseline = v;
		}

		if(baseline == DBL_MAX)
			return false;

		return true;
	}

	double sum = 0.0;

	for(int i = 1; i <= ProfitSpikeBaselineBars; i++)
		sum += g_profit_spike_snapshots[i];

	baseline = sum / ProfitSpikeBaselineBars;
	return true;
}

bool TryArmProfitSpikeTrail(double now)
{
	double baseline = 0.0;

	if(!GetProfitSpikeMovingBaseline(baseline))
	{
		g_dbg_profit_spike_reason_code = 7;
		return false;
	}

	double denominator = MathMax(baseline, ProfitSpikeIgnoreBaselineBelowPips);
	double increase = now - baseline;
	if(increase < 0.0)
		increase = 0.0;

	double increase_percent = 0.0;
	if(denominator > 0.0)
		increase_percent = 100.0 * increase / denominator;

	g_dbg_profit_spike_baseline_pips = baseline;
	g_dbg_profit_spike_increase_pips = increase;
	g_dbg_profit_spike_increase_percent = increase_percent;

	if(now < ProfitSpikeMinCurrentProfitPips)
	{
		g_dbg_profit_spike_reason_code = 5;
		return false;
	}

	if(increase < ProfitSpikeIgnoreIncreaseBelowPips)
	{
		g_dbg_profit_spike_reason_code = 6;
		return false;
	}

	if(increase_percent < ProfitSpikePercent)
		return false;

	g_profit_spike_armed = true;
	g_profit_spike_baseline_pips = baseline;
	g_profit_spike_peak_pips = now;
	g_profit_spike_peak_bar_time = iTime(Symbol(), Period(), 0);
	g_profit_spike_bars_since_peak = 0;
	g_dbg_profit_spike_armed = 1;
	g_dbg_profit_spike_reason_code = 1;

	return true;
}

void UpdateProfitSpikePeak(double now)
{
	if(!g_profit_spike_armed)
		return;

	if(now > g_profit_spike_peak_pips)
	{
		g_profit_spike_peak_pips = now;
		g_profit_spike_peak_bar_time = iTime(Symbol(), Period(), 0);
		g_profit_spike_bars_since_peak = 0;
	}
}

bool StrategyShouldHarvest()
{
	double now = GetTotalPositiveProfitPips();

	g_profit_spike_now_pips = now;
	g_dbg_profit_spike_now_pips = now;
	g_dbg_profit_spike_signal = 0;
	g_dbg_profit_spike_armed = (g_profit_spike_armed ? 1 : 0);
	g_dbg_profit_spike_bars_since_peak = g_profit_spike_bars_since_peak;

	if(!HarvestPreconditionsMet())
		return false;

	if(!g_profit_spike_armed)
	{
		TryArmProfitSpikeTrail(now);
		g_dbg_profit_spike_armed = (g_profit_spike_armed ? 1 : 0);
		return false;
	}

	UpdateProfitSpikePeak(now);

	double peak = g_profit_spike_peak_pips;
	g_dbg_profit_spike_peak_pips = peak;
	g_dbg_profit_spike_bars_since_peak = g_profit_spike_bars_since_peak;
	g_dbg_profit_spike_armed = 1;
	g_dbg_profit_spike_baseline_pips = g_profit_spike_baseline_pips;

	if(now < ProfitSpikeMinCurrentProfitPips)
	{
		g_dbg_profit_spike_reason_code = 5;
		return false;
	}

	if(peak <= 0.0)
		return false;

	double drop = peak - now;
	if(drop < 0.0)
		drop = 0.0;

	double drop_percent = 100.0 * drop / peak;
	g_dbg_profit_spike_drop_pips = drop;
	g_dbg_profit_spike_drop_percent = drop_percent;

	bool drop_percent_hit =
		ProfitSpikeUseDropPercent &&
		drop_percent >= ProfitSpikeDropFromPeakPercent;

	bool drop_pips_hit =
		ProfitSpikeUseDropPips &&
		drop >= ProfitSpikeDropFromPeakPips;

	bool no_new_peak_hit =
		ProfitSpikeUseNoNewPeakBars &&
		g_profit_spike_bars_since_peak >= ProfitSpikeMaxBarsWithoutNewPeak &&
		drop_percent >= ProfitSpikeNoNewPeakMinDropPercent;

	if(drop_percent_hit)
	{
		g_dbg_profit_spike_reason_code = 2;
		g_dbg_profit_spike_signal = 1;
		return true;
	}

	if(drop_pips_hit)
	{
		g_dbg_profit_spike_reason_code = 3;
		g_dbg_profit_spike_signal = 1;
		return true;
	}

	if(no_new_peak_hit)
	{
		g_dbg_profit_spike_reason_code = 4;
		g_dbg_profit_spike_signal = 1;
		return true;
	}

	return false;
}

void StrategyOnHarvestSuccess()
{
	g_profit_spike_armed = false;
	g_profit_spike_baseline_pips = 0.0;
	g_profit_spike_peak_pips = GetTotalPositiveProfitPips();
	g_profit_spike_now_pips = g_profit_spike_peak_pips;
	g_profit_spike_bars_since_peak = 0;
	g_profit_spike_peak_bar_time = iTime(Symbol(), Period(), 0);
	g_dbg_profit_spike_armed = 0;
}

void StrategyOnTickPrelude()
{
	UpdateProfitSpikeSnapshots();
}

void StrategyUpdateSignalState()
{
	g_dbg_profit_spike_armed = (g_profit_spike_armed ? 1 : 0);
	g_dbg_profit_spike_bars_since_peak = g_profit_spike_bars_since_peak;
}

void StrategyAppendComment(string &text)
{
	text +=
		"\nPROFIT_SPIKE now/base/peak: " +
			DoubleToString(g_dbg_profit_spike_now_pips, 2) + " / " +
			DoubleToString(g_dbg_profit_spike_baseline_pips, 2) + " / " +
			DoubleToString(g_dbg_profit_spike_peak_pips, 2) +
		"\nPROFIT_SPIKE inc/drop: " +
			DoubleToString(g_dbg_profit_spike_increase_pips, 2) + " / " +
			DoubleToString(g_dbg_profit_spike_increase_percent, 2) + "% / " +
			DoubleToString(g_dbg_profit_spike_drop_pips, 2) + " / " +
			DoubleToString(g_dbg_profit_spike_drop_percent, 2) + "%" +
		"\nPROFIT_SPIKE armed/bars/signal/reason: " +
			IntegerToString(g_dbg_profit_spike_armed) + " / " +
			IntegerToString(g_dbg_profit_spike_bars_since_peak) + " / " +
			IntegerToString(g_dbg_profit_spike_signal) + " / " +
			IntegerToString(g_dbg_profit_spike_reason_code);
}

void StrategyOnDeinit(const int reason)
{
}

#endif
