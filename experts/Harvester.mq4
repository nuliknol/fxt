#property strict

#define ORDER_COMMENT_PREFIX "Harvester|G="

enum HARVEST_MODE
{
	STALL_CANDLE = 0,
	SMALL_RANGE_COMPRESSION = 1,
	OPPOSITE_CANDLE = 2,
	WICK_REJECTION = 3,
	PROFIT_GIVEBACK = 4,
	HYBRID_SOFT_WEIGHTED = 5,
	CANDLE_TREND = 6,
	PROFIT_SPIKE_TRAIL = 7
};

enum CANDLE_TREND_CLASS
{
	CT_NEUTRAL = 0,
	CT_TREND = 1,
	CT_COUNTER = 2
};

input int TradeDirection = -1;			// -1 SELL only, +1 BUY only
input double LotSize = 0.01;
input int GridStepPips = 20;
input int MaxOrders = 200;
input int MagicNumber = 778001;
input int SlippagePips = 20;

input HARVEST_MODE HarvestMode = HYBRID_SOFT_WEIGHTED;

input double MinOrderProfitPips = 8.0;
input double CloseSafetyPips = 4.0;
input double MinTotalProfitPipsToHarvest = 10.0;
input int MinProfitableOrdersToHarvest = 1;

input int StallPrevMinBars = 3;
input int StallPrevMaxBars = 12;
input int StallNowMinBars = 2;
input int StallNowMaxBars = 5;
input double StallPrevMinEfficiency = 0.35;
input double StallMaxContinuationRatio = 0.25;
input double StallMinOppositeRatio = 0.05;
input double StallMaxNowRangeRatio = 0.60;
input double StallMinPrevRangePips = 50.0;
input double StallPatternThreshold = 0.70;

input int CompressionLookbackBars = 5;
input double CompressionRangeMaxPips = 100.0;

input int OppositeCandlesRequired = 1;
input double OppositeMinBodyPips = 5.0;
input double OppositeMinBodyRatio = 0.10;

input double WickRatioMin = 0.35;
input double WickMinPips = 10.0;
input bool RequireCloseAgainstDirection = false;

input int CandleTrendLookbackBars = 20;
input int CandleTrendMinTrendCandles = 3;
input int CandleTrendCounterCandlesToClose = 2;
input double CandleTrendMinBodyPips = 5.0;
input double CandleTrendMinBodyRatio = 0.15;
input bool CandleTrendIgnoreInsideCandles = true;
input bool CandleTrendIgnoreOutsideCandles = true;
input bool CandleTrendIgnoreNeutralCandles = true;
input int CandleTrendMaxNeutralGap = 3;

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

input double MinPeakProfitPips = 20.0;
input double MaxProfitGivebackPercent = 25.0;

input double W_Stall = 1.0;
input double W_Compression = 1.0;
input double W_OppositeCandle = 1.0;
input double W_Wick = 1.0;
input double W_Giveback = 1.0;
input bool UseNormalizedHybridScore = true;
input double HarvestThresholdRatio = 0.40;
input double HarvestScoreThreshold = 3.0;

input bool UseSpreadFilter = true;
input double MaxSpreadPips = 40.0;
input bool UseSpreadJumpFilter = false;
input int SpreadSampleTicks = 10;
input double MaxSpreadJumpPips = 20.0;

input bool TesterUseCustomScore = true;
input int TesterMinTrades = 3000;
input double TesterMinProfit = 1000.0;
input double TesterTradeBonus = 3.0;
input double TesterDrawdownPenalty = 0.50;
input bool TesterUseMaxDrawdownFilter = true;
input double TesterMaxDrawdownMoney = 25000.0;
input bool TesterUseMaxDrawdownPercentFilter = false;
input double TesterMaxDrawdownPercent = 25.0;

input bool EnableDebugPrints = false;
input bool EnableChartComment = true;

double g_pip = 0.0;
int g_last_grid_index = 0;
bool g_has_last_grid_index = false;

double g_spread_samples[];
int g_spread_pos = 0;
int g_spread_count = 0;

double g_last_spread_pips = 0.0;
double g_last_spread_range_pips = 0.0;
bool g_last_spread_ok = true;
int g_last_spread_block_reason = -1;

#define SPREAD_BLOCK_NONE 0
#define SPREAD_BLOCK_MAX_SPREAD 1
#define SPREAD_BLOCK_JUMP_WARMUP 2
#define SPREAD_BLOCK_JUMP_RANGE 3

double g_positive_profit_now_pips = 0.0;
double g_positive_profit_peak_pips = 0.0;
double g_harvestable_profit_pips = 0.0;
int g_harvestable_orders = 0;
double g_profit_spike_snapshots[];
int g_profit_spike_snapshot_count = 0;
bool g_profit_spike_armed = false;
double g_profit_spike_baseline_pips = 0.0;
double g_profit_spike_peak_pips = 0.0;
double g_profit_spike_now_pips = 0.0;
datetime g_profit_spike_peak_bar_time = 0;
int g_profit_spike_bars_since_peak = 0;
datetime g_profit_spike_last_bar_time = 0;

int g_signal_stall = 0;
int g_signal_compression = 0;
int g_signal_opposite = 0;
int g_signal_wick = 0;
int g_signal_giveback = 0;
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
int g_dbg_candle_trend_counter_count = 0;
int g_dbg_candle_trend_trend_count = 0;
int g_dbg_candle_trend_neutral_count = 0;
int g_dbg_candle_trend_signal = 0;
double g_dbg_stall_score = 0.0;
double g_dbg_stall_best_score = 0.0;
int g_dbg_stall_best_prev_bars = 0;
int g_dbg_stall_best_now_bars = 0;
double g_dbg_stall_prev_efficiency = 0.0;
double g_dbg_stall_continuation_ratio = 0.0;
double g_dbg_stall_opposite_ratio = 0.0;
double g_dbg_stall_now_range_ratio = 0.0;
double g_dbg_stall_prev_range_pips = 0.0;
double g_dbg_compression_score = 0.0;
double g_dbg_opposite_score = 0.0;
double g_dbg_wick_score = 0.0;
double g_dbg_giveback_score = 0.0;
double g_dbg_hybrid_score = 0.0;
double g_dbg_hybrid_max_score = 0.0;
double g_dbg_hybrid_normalized_score = 0.0;
double g_dbg_hybrid_threshold = 0.0;

double GetPipValue()
{
	if(Digits == 5 || Digits == 3)
		return Point * 10.0;
	return Point;
}

double Clamp01(double x)
{
	if(x < 0.0)
		return 0.0;
	if(x > 1.0)
		return 1.0;
	return x;
}

double SafeDiv(double a, double b)
{
	if(MathAbs(b) <= 0.0000000001)
		return 0.0;
	return a / b;
}

void DebugPrint(string message)
{
	if(EnableDebugPrints)
		Print("Harvester: ", message);
}

bool IsTradeDirectionValid()
{
	return (TradeDirection == -1 || TradeDirection == 1);
}

int MatchingOrderType()
{
	if(TradeDirection > 0)
		return OP_BUY;
	return OP_SELL;
}

double EntryPrice()
{
	if(TradeDirection > 0)
		return Ask;
	return Bid;
}

double CurrentSpreadPips()
{
	return (Ask - Bid) / g_pip;
}

double GridStepPrice()
{
	return GridStepPips * g_pip;
}

double GridLevelPrice(int grid_index)
{
	return grid_index * GridStepPrice();
}

int SlippagePoints()
{
	double raw_points = (SlippagePips * g_pip) / Point;
	int points = (int)MathRound(raw_points);
	if(points < 0)
		points = 0;
	return points;
}

int GetGridIndexFromPrice(double price)
{
	double grid_step_price = GridStepPrice();
	if(grid_step_price <= 0.0)
		return 0;
	return (int)MathFloor(price / grid_step_price);
}

bool OrderMatchesScope()
{
	if(OrderSymbol() != Symbol())
		return false;
	if(OrderMagicNumber() != MagicNumber)
		return false;
	if(OrderType() != MatchingOrderType())
		return false;
	return true;
}

void SyncGridIndexToMarket()
{
	g_last_grid_index = GetGridIndexFromPrice(EntryPrice());
	g_has_last_grid_index = true;
}

int CountOpenEAOrders()
{
	int count = 0;
	int total = OrdersTotal();

	for(int i = 0; i < total; i++)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(OrderMatchesScope())
			count++;
	}

	return count;
}

bool IsGridLevelOccupied(int grid_index)
{
	int total = OrdersTotal();

	for(int i = 0; i < total; i++)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!OrderMatchesScope())
			continue;

		int order_grid_index = GetGridIndexFromPrice(OrderOpenPrice());
		if(order_grid_index == grid_index)
			return true;
	}

	return false;
}

bool OpenGridOrder(int grid_index)
{
	if(!IsTradeDirectionValid())
		return false;
	if(CountOpenEAOrders() >= MaxOrders)
		return false;
	if(IsGridLevelOccupied(grid_index))
		return false;

	int order_type = MatchingOrderType();
	double price = (order_type == OP_BUY ? Ask : Bid);
	double grid_price = GridLevelPrice(grid_index);
	string comment = ORDER_COMMENT_PREFIX + IntegerToString(grid_index);
	int ticket = OrderSend(Symbol(), order_type, LotSize, price, SlippagePoints(), 0.0, 0.0, comment, MagicNumber, 0, clrNONE);

	if(ticket < 0)
	{
		DebugPrint("OrderSend failed. grid=" + IntegerToString(grid_index) + " err=" + IntegerToString(GetLastError()));
		return false;
	}

	Print(
		"GRID_OPEN ticket=", ticket,
		" mode=", TradeDirectionName(),
		" grid_index=", grid_index,
		" grid_price=", DoubleToString(grid_price, Digits),
		" fill_price=", DoubleToString(price, Digits),
		" step_pips=", GridStepPips
	);
	DebugPrint("Opened grid order. ticket=" + IntegerToString(ticket) + " grid=" + IntegerToString(grid_index));
	return true;
}

void ProcessGridEntry()
{
	int grid_index = GetGridIndexFromPrice(EntryPrice());

	if(!g_has_last_grid_index)
	{
		g_last_grid_index = grid_index;
		g_has_last_grid_index = true;
		return;
	}

	bool crossed = false;

	if(TradeDirection > 0 && grid_index > g_last_grid_index)
		crossed = true;
	if(TradeDirection < 0 && grid_index < g_last_grid_index)
		crossed = true;

	if(crossed)
		OpenGridOrder(grid_index);

	g_last_grid_index = grid_index;
}

void UpdateSpreadSamples()
{
	g_last_spread_pips = CurrentSpreadPips();
	g_last_spread_range_pips = 0.0;

	if(!UseSpreadJumpFilter)
		return;
	if(SpreadSampleTicks <= 0)
		return;

	if(ArraySize(g_spread_samples) != SpreadSampleTicks)
		ArrayResize(g_spread_samples, SpreadSampleTicks);

	g_spread_samples[g_spread_pos] = g_last_spread_pips;
	g_spread_pos++;
	if(g_spread_pos >= SpreadSampleTicks)
		g_spread_pos = 0;

	if(g_spread_count < SpreadSampleTicks)
		g_spread_count++;
}

bool IsSpreadOK()
{
	if(UseSpreadFilter && g_last_spread_pips > MaxSpreadPips)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_MAX_SPREAD;
		return false;
	}

	if(!UseSpreadJumpFilter)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_NONE;
		return true;
	}

	if(SpreadSampleTicks <= 0 || g_spread_count < SpreadSampleTicks)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_JUMP_WARMUP;
		return false;
	}

	double min_spread = g_spread_samples[0];
	double max_spread = g_spread_samples[0];

	for(int i = 1; i < SpreadSampleTicks; i++)
	{
		double sample = g_spread_samples[i];
		if(sample < min_spread)
			min_spread = sample;
		if(sample > max_spread)
			max_spread = sample;
	}

	g_last_spread_range_pips = max_spread - min_spread;
	if(g_last_spread_range_pips > MaxSpreadJumpPips)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_JUMP_RANGE;
		return false;
	}

	g_last_spread_block_reason = SPREAD_BLOCK_NONE;
	return true;
}

void LogSpreadExecutionFailure()
{
	static int last_logged_reason = -1;

	if(g_last_spread_ok)
	{
		last_logged_reason = -1;
		return;
	}

	if(g_last_spread_block_reason == last_logged_reason)
		return;

	if(g_last_spread_block_reason == SPREAD_BLOCK_MAX_SPREAD)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked by spread filter. spread_pips=",
			DoubleToString(g_last_spread_pips, 2), " max_spread_pips=", DoubleToString(MaxSpreadPips, 2));
	}
	else if(g_last_spread_block_reason == SPREAD_BLOCK_JUMP_WARMUP)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked by spread jump filter warm-up. SpreadSampleTicks=", SpreadSampleTicks,
			" collected_samples=", g_spread_count);
	}
	else if(g_last_spread_block_reason == SPREAD_BLOCK_JUMP_RANGE)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked by spread jump filter. spread_range_pips=",
			DoubleToString(g_last_spread_range_pips, 2), " max_spread_jump_pips=", DoubleToString(MaxSpreadJumpPips, 2),
			" spread_pips=", DoubleToString(g_last_spread_pips, 2));
	}

	last_logged_reason = g_last_spread_block_reason;
}

double GetOrderProfitPips()
{
	if(OrderType() == OP_BUY)
		return (Bid - OrderOpenPrice()) / g_pip;
	if(OrderType() == OP_SELL)
		return (OrderOpenPrice() - Ask) / g_pip;
	return 0.0;
}

bool IsOrderHarvestable()
{
	if(!OrderMatchesScope())
		return false;

	double profit_pips = GetOrderProfitPips();
	if(profit_pips < (MinOrderProfitPips + CloseSafetyPips))
		return false;

	double money_profit = OrderProfit() + OrderSwap() + OrderCommission();
	if(money_profit <= 0.0)
		return false;

	return true;
}

double GetTotalPositiveProfitPips()
{
	double total_pips = 0.0;
	int total = OrdersTotal();

	for(int i = 0; i < total; i++)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!OrderMatchesScope())
			continue;

		double profit_pips = GetOrderProfitPips();
		if(profit_pips > 0.0)
			total_pips += profit_pips;
	}

	return total_pips;
}

int GetHarvestableOrderCount()
{
	int count = 0;
	int total = OrdersTotal();

	for(int i = 0; i < total; i++)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(IsOrderHarvestable())
			count++;
	}

	return count;
}

double GetHarvestableProfitPips()
{
	double total_pips = 0.0;
	int total = OrdersTotal();

	for(int i = 0; i < total; i++)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!IsOrderHarvestable())
			continue;
		total_pips += GetOrderProfitPips();
	}

	return total_pips;
}

void UpdatePositiveProfitState()
{
	g_positive_profit_now_pips = GetTotalPositiveProfitPips();
	g_harvestable_orders = GetHarvestableOrderCount();
	g_harvestable_profit_pips = GetHarvestableProfitPips();

	if(g_positive_profit_now_pips > g_positive_profit_peak_pips)
		g_positive_profit_peak_pips = g_positive_profit_now_pips;
}

bool HarvestPreconditionsMet()
{
	if(g_harvestable_orders < MinProfitableOrdersToHarvest)
		return false;
	if(g_harvestable_profit_pips < MinTotalProfitPipsToHarvest)
		return false;
	return true;
}

double CandleRangePips(int shift)
{
	return (High[shift] - Low[shift]) / g_pip;
}

double CandleBodyPips(int shift)
{
	return MathAbs(Close[shift] - Open[shift]) / g_pip;
}

double CandleBodyRatio(int shift)
{
	double range_pips = CandleRangePips(shift);
	if(range_pips <= 0.0)
		return 0.0;
	return CandleBodyPips(shift) / range_pips;
}

bool MeasureSegment(
	int start_shift,
	int bars,
	double &oldest_close,
	double &latest_close,
	double &seg_high,
	double &seg_low,
	double &seg_range,
	double &net_move
)
{
	if(bars <= 0)
		return false;
	if(Bars <= start_shift + bars)
		return false;

	seg_high = -DBL_MAX;
	seg_low = DBL_MAX;

	for(int i = 0; i < bars; i++)
	{
		int shift = start_shift + i;
		double h = iHigh(Symbol(), Period(), shift);
		double l = iLow(Symbol(), Period(), shift);

		if(h > seg_high)
			seg_high = h;
		if(l < seg_low)
			seg_low = l;
	}

	latest_close = iClose(Symbol(), Period(), start_shift);
	oldest_close = iClose(Symbol(), Period(), start_shift + bars - 1);
	seg_range = seg_high - seg_low;
	net_move = latest_close - oldest_close;

	return true;
}

double ScoreImpulseStallCandidate(int prev_bars, int now_bars)
{
	double prev_oldest_close = 0.0;
	double prev_latest_close = 0.0;
	double prev_high = 0.0;
	double prev_low = 0.0;
	double prev_range = 0.0;
	double prev_net = 0.0;
	double now_oldest_close = 0.0;
	double now_latest_close = 0.0;
	double now_high = 0.0;
	double now_low = 0.0;
	double now_range = 0.0;
	double now_net = 0.0;

	bool ok_now = MeasureSegment(1, now_bars, now_oldest_close, now_latest_close, now_high, now_low, now_range, now_net);
	bool ok_prev = MeasureSegment(now_bars + 1, prev_bars, prev_oldest_close, prev_latest_close, prev_high, prev_low, prev_range, prev_net);

	if(!ok_now || !ok_prev)
		return 0.0;

	double d = (TradeDirection > 0 ? 1.0 : -1.0);
	double prev_favorable_net = d * prev_net;

	if(prev_favorable_net <= 0.0)
		return 0.0;

	double prev_range_pips = prev_range / g_pip;
	if(prev_range_pips < StallMinPrevRangePips)
		return 0.0;

	double prev_efficiency = SafeDiv(prev_favorable_net, prev_range);
	if(prev_efficiency < StallPrevMinEfficiency)
		return 0.0;

	double now_favorable_net = d * now_net;
	if(now_favorable_net < 0.0)
		now_favorable_net = 0.0;

	double continuation_ratio = SafeDiv(now_favorable_net, prev_favorable_net);
	if(continuation_ratio > StallMaxContinuationRatio)
		return 0.0;

	double now_opposite_net = -d * now_net;
	if(now_opposite_net < 0.0)
		now_opposite_net = 0.0;

	double opposite_ratio = SafeDiv(now_opposite_net, prev_favorable_net);
	double now_range_ratio = SafeDiv(now_range, prev_range);

	bool opposite_ok = (opposite_ratio >= StallMinOppositeRatio);
	bool compression_ok = (now_range_ratio <= StallMaxNowRangeRatio);
	if(!opposite_ok && !compression_ok)
		return 0.0;

	double prev_score = Clamp01(SafeDiv(prev_efficiency, StallPrevMinEfficiency));

	double continuation_score = 0.0;
	if(StallMaxContinuationRatio <= 0.0)
		continuation_score = (continuation_ratio <= 0.0 ? 1.0 : 0.0);
	else
		continuation_score = Clamp01(1.0 - SafeDiv(continuation_ratio, StallMaxContinuationRatio));

	double opposite_score = 0.0;
	if(StallMinOppositeRatio <= 0.0)
		opposite_score = (opposite_ratio > 0.0 ? 1.0 : 0.0);
	else
		opposite_score = Clamp01(SafeDiv(opposite_ratio, StallMinOppositeRatio));

	double compression_score = 0.0;
	if(StallMaxNowRangeRatio > 0.0)
		compression_score = Clamp01(1.0 - SafeDiv(now_range_ratio, StallMaxNowRangeRatio));

	double now_score = MathMax(opposite_score, compression_score);
	double candidate_score = MathMin(prev_score, MathMin(continuation_score, now_score));

	return Clamp01(candidate_score);
}

double ScoreImpulseStallPattern()
{
	double best_score = 0.0;

	g_dbg_stall_best_score = 0.0;
	g_dbg_stall_best_prev_bars = 0;
	g_dbg_stall_best_now_bars = 0;
	g_dbg_stall_prev_efficiency = 0.0;
	g_dbg_stall_continuation_ratio = 0.0;
	g_dbg_stall_opposite_ratio = 0.0;
	g_dbg_stall_now_range_ratio = 0.0;
	g_dbg_stall_prev_range_pips = 0.0;

	for(int now_bars = StallNowMinBars; now_bars <= StallNowMaxBars; now_bars++)
	{
		for(int prev_bars = StallPrevMinBars; prev_bars <= StallPrevMaxBars; prev_bars++)
		{
			double s = ScoreImpulseStallCandidate(prev_bars, now_bars);

			if(s > best_score)
			{
				double prev_oldest_close = 0.0;
				double prev_latest_close = 0.0;
				double prev_high = 0.0;
				double prev_low = 0.0;
				double prev_range = 0.0;
				double prev_net = 0.0;
				double now_oldest_close = 0.0;
				double now_latest_close = 0.0;
				double now_high = 0.0;
				double now_low = 0.0;
				double now_range = 0.0;
				double now_net = 0.0;

				MeasureSegment(1, now_bars, now_oldest_close, now_latest_close, now_high, now_low, now_range, now_net);
				MeasureSegment(now_bars + 1, prev_bars, prev_oldest_close, prev_latest_close, prev_high, prev_low, prev_range, prev_net);

				double d = (TradeDirection > 0 ? 1.0 : -1.0);
				double prev_favorable_net = d * prev_net;
				double now_favorable_net = d * now_net;
				double now_opposite_net = -d * now_net;

				if(now_favorable_net < 0.0)
					now_favorable_net = 0.0;
				if(now_opposite_net < 0.0)
					now_opposite_net = 0.0;

				best_score = s;
				g_dbg_stall_best_score = s;
				g_dbg_stall_best_prev_bars = prev_bars;
				g_dbg_stall_best_now_bars = now_bars;
				g_dbg_stall_prev_efficiency = SafeDiv(prev_favorable_net, prev_range);
				g_dbg_stall_continuation_ratio = SafeDiv(now_favorable_net, prev_favorable_net);
				g_dbg_stall_opposite_ratio = SafeDiv(now_opposite_net, prev_favorable_net);
				g_dbg_stall_now_range_ratio = SafeDiv(now_range, prev_range);
				g_dbg_stall_prev_range_pips = prev_range / g_pip;
			}
		}
	}

	g_dbg_stall_score = Clamp01(best_score);
	return g_dbg_stall_score;
}

bool RawSignalSmallRangeCompression()
{
	if(CompressionLookbackBars <= 0)
		return false;
	if(Bars <= CompressionLookbackBars)
		return false;

	double window_high = High[1];
	double window_low = Low[1];

	for(int i = 2; i <= CompressionLookbackBars; i++)
	{
		if(High[i] > window_high)
			window_high = High[i];
		if(Low[i] < window_low)
			window_low = Low[i];
	}

	double window_range_pips = (window_high - window_low) / g_pip;
	return (window_range_pips <= CompressionRangeMaxPips);
}

double ScoreSmallRangeCompression()
{
	if(CompressionLookbackBars <= 0)
		return 0.0;
	if(Bars <= CompressionLookbackBars)
		return 0.0;
	if(CompressionRangeMaxPips <= 0.0)
		return 0.0;

	double window_high = High[1];
	double window_low = Low[1];

	for(int i = 2; i <= CompressionLookbackBars; i++)
	{
		if(High[i] > window_high)
			window_high = High[i];
		if(Low[i] < window_low)
			window_low = Low[i];
	}

	double window_range_pips = (window_high - window_low) / g_pip;
	double score = 1.0 - (window_range_pips / CompressionRangeMaxPips);
	return Clamp01(score);
}

bool IsOppositeCandle(int shift)
{
	double range_pips = CandleRangePips(shift);
	double body_pips = CandleBodyPips(shift);

	if(range_pips <= 0.0)
		return false;
	if(body_pips < OppositeMinBodyPips)
		return false;
	if((body_pips / range_pips) < OppositeMinBodyRatio)
		return false;

	if(TradeDirection < 0)
		return (Close[shift] > Open[shift]);
	return (Close[shift] < Open[shift]);
}

bool RawSignalOppositeCandle()
{
	if(OppositeCandlesRequired <= 0)
		return false;
	if(Bars <= OppositeCandlesRequired)
		return false;

	for(int i = 1; i <= OppositeCandlesRequired; i++)
	{
		if(!IsOppositeCandle(i))
			return false;
	}

	return true;
}

double ScoreOppositeCandle()
{
	if(OppositeCandlesRequired <= 0)
		return 0.0;
	if(Bars <= OppositeCandlesRequired)
		return 0.0;

	double score = 1.0;

	for(int i = 1; i <= OppositeCandlesRequired; i++)
	{
		double range_pips = CandleRangePips(i);
		double body_pips = CandleBodyPips(i);
		if(range_pips <= 0.0)
			return 0.0;

		bool is_opposite = false;
		if(TradeDirection < 0)
			is_opposite = (Close[i] > Open[i]);
		else
			is_opposite = (Close[i] < Open[i]);

		if(!is_opposite)
			return 0.0;

		double body_ratio = body_pips / range_pips;
		double body_pip_score = 1.0;
		double body_ratio_score = 1.0;

		if(OppositeMinBodyPips > 0.0)
			body_pip_score = body_pips / OppositeMinBodyPips;
		if(OppositeMinBodyRatio > 0.0)
			body_ratio_score = body_ratio / OppositeMinBodyRatio;

		double candle_score = MathMin(Clamp01(body_pip_score), Clamp01(body_ratio_score));
		if(candle_score < score)
			score = candle_score;
	}

	return Clamp01(score);
}

bool RawSignalWickRejection()
{
	if(Bars <= 1)
		return false;

	double range = High[1] - Low[1];
	if(range <= 0.0)
		return false;

	double upper_wick = High[1] - MathMax(Open[1], Close[1]);
	double lower_wick = MathMin(Open[1], Close[1]) - Low[1];
	double wick_size = 0.0;

	if(TradeDirection < 0)
		wick_size = lower_wick;
	else
		wick_size = upper_wick;

	double wick_pips = wick_size / g_pip;
	double wick_ratio = wick_size / range;

	if(wick_pips < WickMinPips)
		return false;
	if(wick_ratio < WickRatioMin)
		return false;

	if(RequireCloseAgainstDirection)
	{
		if(TradeDirection < 0 && Close[1] <= Open[1])
			return false;
		if(TradeDirection > 0 && Close[1] >= Open[1])
			return false;
	}

	return true;
}

double ScoreWickRejection()
{
	if(Bars <= 1)
		return 0.0;

	double range = High[1] - Low[1];
	if(range <= 0.0)
		return 0.0;

	double upper_wick = High[1] - MathMax(Open[1], Close[1]);
	double lower_wick = MathMin(Open[1], Close[1]) - Low[1];
	double wick = 0.0;

	if(TradeDirection < 0)
		wick = lower_wick;
	else
		wick = upper_wick;

	if(RequireCloseAgainstDirection)
	{
		if(TradeDirection < 0 && Close[1] <= Open[1])
			return 0.0;
		if(TradeDirection > 0 && Close[1] >= Open[1])
			return 0.0;
	}

	double wick_pips = wick / g_pip;
	double wick_ratio = wick / range;
	double pip_score = 1.0;
	double ratio_score = 1.0;

	if(WickMinPips > 0.0)
		pip_score = wick_pips / WickMinPips;
	if(WickRatioMin > 0.0)
		ratio_score = wick_ratio / WickRatioMin;

	double score = MathMin(Clamp01(pip_score), Clamp01(ratio_score));
	return Clamp01(score);
}

bool RawSignalProfitGiveback()
{
	if(g_positive_profit_peak_pips < MinPeakProfitPips)
		return false;
	if(g_positive_profit_peak_pips <= 0.0)
		return false;

	double giveback = g_positive_profit_peak_pips - g_positive_profit_now_pips;
	double giveback_ratio = giveback / g_positive_profit_peak_pips;

	return (giveback_ratio >= (MaxProfitGivebackPercent / 100.0));
}

double ScoreProfitGiveback()
{
	if(g_positive_profit_peak_pips < MinPeakProfitPips)
		return 0.0;
	if(g_positive_profit_peak_pips <= 0.0)
		return 0.0;

	double required_ratio = MaxProfitGivebackPercent / 100.0;
	if(required_ratio <= 0.0)
		return 0.0;

	double giveback = g_positive_profit_peak_pips - g_positive_profit_now_pips;
	double giveback_ratio = giveback / g_positive_profit_peak_pips;
	double score = giveback_ratio / required_ratio;
	return Clamp01(score);
}

double GetPositiveProfitPips()
{
	double total = 0.0;

	for(int i = OrdersTotal() - 1; i >= 0; i--)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(OrderSymbol() != Symbol())
			continue;
		if(OrderMagicNumber() != MagicNumber)
			continue;
		if(TradeDirection < 0 && OrderType() != OP_SELL)
			continue;
		if(TradeDirection > 0 && OrderType() != OP_BUY)
			continue;

		double profit_pips = 0.0;
		if(OrderType() == OP_BUY)
			profit_pips = (Bid - OrderOpenPrice()) / g_pip;
		else if(OrderType() == OP_SELL)
			profit_pips = (OrderOpenPrice() - Ask) / g_pip;
		else
			continue;

		if(profit_pips > 0.0)
			total += profit_pips;
	}

	return total;
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

	double now = GetPositiveProfitPips();
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

bool SignalProfitSpikeTrail()
{
	double now = GetPositiveProfitPips();

	g_profit_spike_now_pips = now;
	g_dbg_profit_spike_now_pips = now;
	g_dbg_profit_spike_signal = 0;
	g_dbg_profit_spike_armed = (g_profit_spike_armed ? 1 : 0);

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

void ResetProfitSpikeAfterHarvest()
{
	g_profit_spike_armed = false;
	g_profit_spike_baseline_pips = 0.0;
	g_profit_spike_peak_pips = GetPositiveProfitPips();
	g_profit_spike_now_pips = g_profit_spike_peak_pips;
	g_profit_spike_bars_since_peak = 0;
	g_profit_spike_peak_bar_time = iTime(Symbol(), Period(), 0);
	g_dbg_profit_spike_armed = 0;
}

CANDLE_TREND_CLASS ClassifyCandleTrend(int shift)
{
	if(shift <= 0)
		return CT_NEUTRAL;
	if(Bars <= shift + 1)
		return CT_NEUTRAL;

	double o = iOpen(Symbol(), Period(), shift);
	double h = iHigh(Symbol(), Period(), shift);
	double l = iLow(Symbol(), Period(), shift);
	double c = iClose(Symbol(), Period(), shift);

	double range = h - l;
	if(range <= 0.0)
		return CT_NEUTRAL;

	double body = MathAbs(c - o);
	double body_pips = body / g_pip;
	double body_ratio = body / range;

	if(body_pips < CandleTrendMinBodyPips)
		return CT_NEUTRAL;
	if(body_ratio < CandleTrendMinBodyRatio)
		return CT_NEUTRAL;

	double prev_h = iHigh(Symbol(), Period(), shift + 1);
	double prev_l = iLow(Symbol(), Period(), shift + 1);
	bool inside = (h <= prev_h && l >= prev_l);
	bool outside = (h >= prev_h && l <= prev_l);

	if(CandleTrendIgnoreInsideCandles && inside)
		return CT_NEUTRAL;
	if(CandleTrendIgnoreOutsideCandles && outside)
		return CT_NEUTRAL;

	if(TradeDirection < 0)
	{
		if(c < o)
			return CT_TREND;
		if(c > o)
			return CT_COUNTER;
	}
	else
	{
		if(c > o)
			return CT_TREND;
		if(c < o)
			return CT_COUNTER;
	}

	return CT_NEUTRAL;
}

bool SignalCandleTrend()
{
	g_dbg_candle_trend_counter_count = 0;
	g_dbg_candle_trend_trend_count = 0;
	g_dbg_candle_trend_neutral_count = 0;
	g_dbg_candle_trend_signal = 0;

	if(!HarvestPreconditionsMet())
		return false;
	if(CandleTrendLookbackBars < 1)
		return false;
	if(CandleTrendMinTrendCandles < 1)
		return false;
	if(CandleTrendCounterCandlesToClose < 1)
		return false;
	if(Bars <= CandleTrendLookbackBars + 2)
		return false;

	int counter_count = 0;
	int trend_count = 0;
	int neutral_gap = 0;
	int neutral_count = 0;
	bool counter_phase = true;
	bool saw_counter = false;

	for(int shift = 1; shift <= CandleTrendLookbackBars; shift++)
	{
		CANDLE_TREND_CLASS cls = ClassifyCandleTrend(shift);

		if(cls == CT_NEUTRAL)
		{
			if(CandleTrendIgnoreNeutralCandles)
			{
				neutral_gap++;
				neutral_count++;
				if(neutral_gap > CandleTrendMaxNeutralGap)
					break;
				continue;
			}
			else
			{
				break;
			}
		}

		neutral_gap = 0;

		if(counter_phase)
		{
			if(cls == CT_COUNTER)
			{
				counter_count++;
				saw_counter = true;
				continue;
			}

			if(cls == CT_TREND)
			{
				counter_phase = false;
				trend_count++;
				continue;
			}
		}
		else
		{
			if(cls == CT_TREND)
			{
				trend_count++;
				continue;
			}

			if(cls == CT_COUNTER)
				break;
		}
	}

	g_dbg_candle_trend_counter_count = counter_count;
	g_dbg_candle_trend_trend_count = trend_count;
	g_dbg_candle_trend_neutral_count = neutral_count;

	if(!saw_counter)
		return false;
	if(counter_count < CandleTrendCounterCandlesToClose)
		return false;
	if(trend_count < CandleTrendMinTrendCandles)
		return false;

	g_dbg_candle_trend_signal = 1;
	return true;
}

void UpdateSignalStates()
{
	g_signal_stall = 0;
	g_signal_compression = (RawSignalSmallRangeCompression() ? 1 : 0);
	g_signal_opposite = (RawSignalOppositeCandle() ? 1 : 0);
	g_signal_wick = (RawSignalWickRejection() ? 1 : 0);
	g_signal_giveback = (RawSignalProfitGiveback() ? 1 : 0);
	g_dbg_profit_spike_baseline_pips = 0.0;
	g_dbg_profit_spike_now_pips = 0.0;
	g_dbg_profit_spike_peak_pips = 0.0;
	g_dbg_profit_spike_increase_pips = 0.0;
	g_dbg_profit_spike_increase_percent = 0.0;
	g_dbg_profit_spike_drop_pips = 0.0;
	g_dbg_profit_spike_drop_percent = 0.0;
	g_dbg_profit_spike_armed = (g_profit_spike_armed ? 1 : 0);
	g_dbg_profit_spike_bars_since_peak = 0;
	g_dbg_profit_spike_signal = 0;
	g_dbg_profit_spike_reason_code = 0;
	g_dbg_candle_trend_counter_count = 0;
	g_dbg_candle_trend_trend_count = 0;
	g_dbg_candle_trend_neutral_count = 0;
	g_dbg_candle_trend_signal = 0;
	g_dbg_stall_score = 0.0;
	g_dbg_stall_best_score = 0.0;
	g_dbg_stall_best_prev_bars = 0;
	g_dbg_stall_best_now_bars = 0;
	g_dbg_stall_prev_efficiency = 0.0;
	g_dbg_stall_continuation_ratio = 0.0;
	g_dbg_stall_opposite_ratio = 0.0;
	g_dbg_stall_now_range_ratio = 0.0;
	g_dbg_stall_prev_range_pips = 0.0;
	g_dbg_compression_score = 0.0;
	g_dbg_opposite_score = 0.0;
	g_dbg_wick_score = 0.0;
	g_dbg_giveback_score = 0.0;
	g_dbg_hybrid_score = 0.0;
	g_dbg_hybrid_max_score = 0.0;
	g_dbg_hybrid_normalized_score = 0.0;
	g_dbg_hybrid_threshold = 0.0;
}

bool SignalStallCandle()
{
	if(!HarvestPreconditionsMet())
		return false;
	double score = ScoreImpulseStallPattern();
	g_signal_stall = (score >= StallPatternThreshold ? 1 : 0);
	return (score >= StallPatternThreshold);
}

bool SignalSmallRangeCompression()
{
	if(!HarvestPreconditionsMet())
		return false;
	return (g_signal_compression > 0);
}

bool SignalOppositeCandle()
{
	if(!HarvestPreconditionsMet())
		return false;
	return (g_signal_opposite > 0);
}

bool SignalWickRejection()
{
	if(!HarvestPreconditionsMet())
		return false;
	return (g_signal_wick > 0);
}

bool SignalProfitGiveback()
{
	if(!HarvestPreconditionsMet())
		return false;
	return (g_signal_giveback > 0);
}

bool SignalHybridSoftWeighted()
{
	if(!HarvestPreconditionsMet())
		return false;

	double stall_score = ScoreImpulseStallPattern();
	double compression_score = ScoreSmallRangeCompression();
	double opposite_score = ScoreOppositeCandle();
	double wick_score = ScoreWickRejection();
	double giveback_score = ScoreProfitGiveback();

	double score =
		(W_Stall * stall_score) +
		(W_Compression * compression_score) +
		(W_OppositeCandle * opposite_score) +
		(W_Wick * wick_score) +
		(W_Giveback * giveback_score);

	double max_score =
		MathMax(0.0, W_Stall) +
		MathMax(0.0, W_Compression) +
		MathMax(0.0, W_OppositeCandle) +
		MathMax(0.0, W_Wick) +
		MathMax(0.0, W_Giveback);

	if(max_score <= 0.0)
		return false;

	double normalized_score = score / max_score;
	bool should_harvest = false;

	g_dbg_stall_score = stall_score;
	g_dbg_compression_score = compression_score;
	g_dbg_opposite_score = opposite_score;
	g_dbg_wick_score = wick_score;
	g_dbg_giveback_score = giveback_score;
	g_dbg_hybrid_score = score;
	g_dbg_hybrid_max_score = max_score;
	g_dbg_hybrid_normalized_score = normalized_score;

	if(UseNormalizedHybridScore)
	{
		g_dbg_hybrid_threshold = HarvestThresholdRatio;
		should_harvest = (normalized_score >= HarvestThresholdRatio);
	}
	else
	{
		g_dbg_hybrid_threshold = HarvestScoreThreshold;
		should_harvest = (score >= HarvestScoreThreshold);
	}

	if(should_harvest && EnableDebugPrints)
	{
		Print("HYBRID_SOFT harvest: ",
			"score=", DoubleToString(score, 4),
			" max=", DoubleToString(max_score, 4),
			" norm=", DoubleToString(normalized_score, 4),
			" threshold_ratio=", DoubleToString(HarvestThresholdRatio, 4),
			" abs_threshold=", DoubleToString(HarvestScoreThreshold, 4),
			" stall=", DoubleToString(stall_score, 4),
			" compression=", DoubleToString(compression_score, 4),
			" opposite=", DoubleToString(opposite_score, 4),
			" wick=", DoubleToString(wick_score, 4),
			" giveback=", DoubleToString(giveback_score, 4));
	}

	return should_harvest;
}

bool ShouldHarvest()
{
	switch(HarvestMode)
	{
		case STALL_CANDLE:
			return SignalStallCandle();
		case SMALL_RANGE_COMPRESSION:
			return SignalSmallRangeCompression();
		case OPPOSITE_CANDLE:
			return SignalOppositeCandle();
		case WICK_REJECTION:
			return SignalWickRejection();
		case PROFIT_GIVEBACK:
			return SignalProfitGiveback();
		case HYBRID_SOFT_WEIGHTED:
			return SignalHybridSoftWeighted();
		case CANDLE_TREND:
			return SignalCandleTrend();
		case PROFIT_SPIKE_TRAIL:
			return SignalProfitSpikeTrail();
	}

	return false;
}

void LogStallHarvestDecision()
{
	if(HarvestMode != STALL_CANDLE)
		return;
	if(g_dbg_stall_best_prev_bars <= 0 || g_dbg_stall_best_now_bars <= 0)
		return;

	Print(
		"STALL_HARVEST_DECISION result=", BoolText(g_dbg_stall_score >= StallPatternThreshold),
		" stall_score=", DoubleToString(g_dbg_stall_score, 4),
		" threshold=", DoubleToString(StallPatternThreshold, 4),
		" best_prev_bars=", g_dbg_stall_best_prev_bars,
		" best_now_bars=", g_dbg_stall_best_now_bars,
		" prev_efficiency=", DoubleToString(g_dbg_stall_prev_efficiency, 4),
		" continuation_ratio=", DoubleToString(g_dbg_stall_continuation_ratio, 4),
		" opposite_ratio=", DoubleToString(g_dbg_stall_opposite_ratio, 4),
		" now_range_ratio=", DoubleToString(g_dbg_stall_now_range_ratio, 4),
		" prev_range_pips=", DoubleToString(g_dbg_stall_prev_range_pips, 2)
	);
}

void LogHarvestExecution()
{
	Print(
		"HARVEST_EXECUTION mode=", HarvestModeName(),
		" stall=", g_signal_stall,
		" compression=", g_signal_compression,
		" opposite=", g_signal_opposite,
		" wick=", g_signal_wick,
		" giveback=", g_signal_giveback,
		" soft_stall=", DoubleToString(g_dbg_stall_score, 4),
		" soft_compression=", DoubleToString(g_dbg_compression_score, 4),
		" soft_opposite=", DoubleToString(g_dbg_opposite_score, 4),
		" soft_wick=", DoubleToString(g_dbg_wick_score, 4),
		" soft_giveback=", DoubleToString(g_dbg_giveback_score, 4),
		" hybrid_score=", DoubleToString(g_dbg_hybrid_score, 4),
		" hybrid_max=", DoubleToString(g_dbg_hybrid_max_score, 4),
		" hybrid_norm=", DoubleToString(g_dbg_hybrid_normalized_score, 4),
		" threshold=", DoubleToString(g_dbg_hybrid_threshold, 4),
		" harvestable_orders=", g_harvestable_orders,
		" harvestable_profit_pips=", DoubleToString(g_harvestable_profit_pips, 2),
		" positive_now_pips=", DoubleToString(g_positive_profit_now_pips, 2),
		" positive_peak_pips=", DoubleToString(g_positive_profit_peak_pips, 2),
		" spread_pips=", DoubleToString(g_last_spread_pips, 2)
	);

	LogStallHarvestDecision();
}

int HarvestProfitableOrders()
{
	int closed_orders = 0;
	int total = OrdersTotal();

	LogHarvestExecution();

	for(int i = total - 1; i >= 0; i--)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!IsOrderHarvestable())
			continue;

		int ticket = OrderTicket();
		double lots = OrderLots();
		double profit_pips = GetOrderProfitPips();
		double money_profit = OrderProfit() + OrderSwap() + OrderCommission();
		double close_price = (OrderType() == OP_BUY ? Bid : Ask);

		RefreshRates();
		if(OrderType() == OP_BUY)
			close_price = Bid;
		else
			close_price = Ask;

		if(OrderClose(ticket, lots, close_price, SlippagePoints(), clrNONE))
		{
			closed_orders++;
			Print(
				"HARVEST_CLOSE mode=", HarvestModeName(),
				" ticket=", ticket,
				" order_type=", OrderType(),
				" lots=", DoubleToString(lots, 2),
				" profit_pips=", DoubleToString(profit_pips, 2),
				" money_profit=", DoubleToString(money_profit, 2),
				" close_price=", DoubleToString(close_price, Digits)
			);
			DebugPrint("Closed harvestable order. ticket=" + IntegerToString(ticket));
		}
		else
		{
			DebugPrint("OrderClose failed. ticket=" + IntegerToString(ticket) + " err=" + IntegerToString(GetLastError()));
		}
	}

	if(closed_orders > 0)
	{
		UpdatePositiveProfitState();
		g_positive_profit_peak_pips = GetTotalPositiveProfitPips();
		ResetProfitSpikeAfterHarvest();
	}

	return closed_orders;
}

string HarvestModeName()
{
	switch(HarvestMode)
	{
		case STALL_CANDLE:
			return "STALL_CANDLE";
		case SMALL_RANGE_COMPRESSION:
			return "SMALL_RANGE_COMPRESSION";
		case OPPOSITE_CANDLE:
			return "OPPOSITE_CANDLE";
		case WICK_REJECTION:
			return "WICK_REJECTION";
		case PROFIT_GIVEBACK:
			return "PROFIT_GIVEBACK";
		case HYBRID_SOFT_WEIGHTED:
			return "HYBRID_SOFT_WEIGHTED";
		case CANDLE_TREND:
			return "CANDLE_TREND";
		case PROFIT_SPIKE_TRAIL:
			return "PROFIT_SPIKE_TRAIL";
	}

	return "UNKNOWN";
}

string TradeDirectionName()
{
	if(TradeDirection > 0)
		return "BUY_ONLY";
	return "SELL_ONLY";
}

string BoolText(bool value)
{
	if(value)
		return "1";
	return "0";
}

void UpdateChartComment()
{
	if(!EnableChartComment)
		return;

	string text =
		"Harvester.mq4" +
		"\nSymbol: " + Symbol() +
		"\nPeriod: " + IntegerToString(Period()) +
		"\nTradeDirection: " + TradeDirectionName() +
		"\nGridStepPips: " + IntegerToString(GridStepPips) +
		"\nLotSize: " + DoubleToString(LotSize, 2) +
		"\nOpen EA orders: " + IntegerToString(CountOpenEAOrders()) +
		"\nHarvestable orders: " + IntegerToString(g_harvestable_orders) +
		"\nHarvestable profit pips: " + DoubleToString(g_harvestable_profit_pips, 2) +
		"\nTotal positive profit pips: " + DoubleToString(g_positive_profit_now_pips, 2) +
		"\nPositive profit peak pips: " + DoubleToString(g_positive_profit_peak_pips, 2) +
		"\nHarvestMode: " + HarvestModeName() +
		"\nSignals stall/compression/opposite/wick/giveback: " +
			BoolText(g_signal_stall > 0) + " / " +
			BoolText(g_signal_compression > 0) + " / " +
			BoolText(g_signal_opposite > 0) + " / " +
			BoolText(g_signal_wick > 0) + " / " +
			BoolText(g_signal_giveback > 0) +
		"\nCANDLE_TREND c/t/n/s: " +
			IntegerToString(g_dbg_candle_trend_counter_count) + " / " +
			IntegerToString(g_dbg_candle_trend_trend_count) + " / " +
			IntegerToString(g_dbg_candle_trend_neutral_count) + " / " +
			IntegerToString(g_dbg_candle_trend_signal) +
		"\nCANDLE_TREND cfg trend/counter/body/ratio: " +
			IntegerToString(CandleTrendMinTrendCandles) + " / " +
			IntegerToString(CandleTrendCounterCandlesToClose) + " / " +
			DoubleToString(CandleTrendMinBodyPips, 2) + " / " +
			DoubleToString(CandleTrendMinBodyRatio, 2) +
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
			IntegerToString(g_dbg_profit_spike_reason_code) +
		"\nStall score / threshold: " +
			DoubleToString(g_dbg_stall_score, 2) + " / " +
			DoubleToString(StallPatternThreshold, 2) +
		"\nStall best prev/now bars: " +
			IntegerToString(g_dbg_stall_best_prev_bars) + " / " +
			IntegerToString(g_dbg_stall_best_now_bars) +
		"\nStall eff/cont/opp/range: " +
			DoubleToString(g_dbg_stall_prev_efficiency, 2) + " / " +
			DoubleToString(g_dbg_stall_continuation_ratio, 2) + " / " +
			DoubleToString(g_dbg_stall_opposite_ratio, 2) + " / " +
			DoubleToString(g_dbg_stall_now_range_ratio, 2) +
		"\nStall prev range pips: " +
			DoubleToString(g_dbg_stall_prev_range_pips, 2) +
		"\nHybrid soft scores s/c/o/w/g: " +
			DoubleToString(g_dbg_stall_score, 2) + " / " +
			DoubleToString(g_dbg_compression_score, 2) + " / " +
			DoubleToString(g_dbg_opposite_score, 2) + " / " +
			DoubleToString(g_dbg_wick_score, 2) + " / " +
			DoubleToString(g_dbg_giveback_score, 2) +
		"\nHybrid score/max/norm: " +
			DoubleToString(g_dbg_hybrid_score, 2) + " / " +
			DoubleToString(g_dbg_hybrid_max_score, 2) + " / " +
			DoubleToString(g_dbg_hybrid_normalized_score, 2) +
		"\nHybrid threshold ratio/abs/use_norm: " +
			DoubleToString(HarvestThresholdRatio, 2) + " / " +
			DoubleToString(HarvestScoreThreshold, 2) + " / " +
			BoolText(UseNormalizedHybridScore) +
		"\nSpread pips: " + DoubleToString(g_last_spread_pips, 2) +
		"\nSpread range pips: " + DoubleToString(g_last_spread_range_pips, 2) +
		"\nSpread OK: " + BoolText(g_last_spread_ok);

	Comment(text);
}

bool ValidateInputs()
{
	if(!IsTradeDirectionValid())
		return false;
	if(LotSize <= 0.0)
		return false;
	if(GridStepPips <= 0)
		return false;
	if(MaxOrders <= 0)
		return false;
	if(SlippagePips < 0)
		return false;
	if(MinProfitableOrdersToHarvest <= 0)
		return false;
	if(MinOrderProfitPips < 0.0 || CloseSafetyPips < 0.0)
		return false;
	if(MinTotalProfitPipsToHarvest < 0.0)
		return false;
	if(StallPrevMinBars < 1)
		return false;
	if(StallPrevMaxBars < StallPrevMinBars)
		return false;
	if(StallNowMinBars < 2)
		return false;
	if(StallNowMaxBars < StallNowMinBars)
		return false;
	if(StallPrevMinEfficiency < 0.0 || StallPrevMinEfficiency > 1.0)
		return false;
	if(StallMaxContinuationRatio < 0.0)
		return false;
	if(StallMinOppositeRatio < 0.0)
		return false;
	if(StallMaxNowRangeRatio <= 0.0)
		return false;
	if(StallMinPrevRangePips < 0.0)
		return false;
	if(StallPatternThreshold < 0.0 || StallPatternThreshold > 1.0)
		return false;
	if(CandleTrendLookbackBars < 2)
		return false;
	if(CandleTrendMinTrendCandles < 1)
		return false;
	if(CandleTrendCounterCandlesToClose < 1)
		return false;
	if(CandleTrendMinBodyPips < 0.0)
		return false;
	if(CandleTrendMinBodyRatio < 0.0 || CandleTrendMinBodyRatio > 1.0)
		return false;
	if(CandleTrendMaxNeutralGap < 0)
		return false;
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
	if(W_Stall < 0.0 || W_Compression < 0.0 || W_OppositeCandle < 0.0 || W_Wick < 0.0 || W_Giveback < 0.0)
		return false;
	if(HarvestThresholdRatio < 0.0 || HarvestThresholdRatio > 1.0)
		return false;
	if(HarvestScoreThreshold < 0.0)
		return false;
	if(UseSpreadFilter && MaxSpreadPips < 0.0)
		return false;
	if(UseSpreadJumpFilter && (SpreadSampleTicks <= 0 || MaxSpreadJumpPips < 0.0))
		return false;
	return true;
}

int OnInit()
{
	g_pip = GetPipValue();

	if(UseSpreadJumpFilter && SpreadSampleTicks > 0)
	{
		ArrayResize(g_spread_samples, SpreadSampleTicks);
		ArrayInitialize(g_spread_samples, 0.0);
	}

	g_spread_pos = 0;
	g_spread_count = 0;
	g_positive_profit_now_pips = 0.0;
	g_positive_profit_peak_pips = 0.0;
	g_harvestable_profit_pips = 0.0;
	g_harvestable_orders = 0;
	ArrayResize(g_profit_spike_snapshots, ProfitSpikeBaselineBars + 2);
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
	g_dbg_candle_trend_counter_count = 0;
	g_dbg_candle_trend_trend_count = 0;
	g_dbg_candle_trend_neutral_count = 0;
	g_dbg_candle_trend_signal = 0;
	g_dbg_stall_score = 0.0;
	g_dbg_stall_best_score = 0.0;
	g_dbg_stall_best_prev_bars = 0;
	g_dbg_stall_best_now_bars = 0;
	g_dbg_stall_prev_efficiency = 0.0;
	g_dbg_stall_continuation_ratio = 0.0;
	g_dbg_stall_opposite_ratio = 0.0;
	g_dbg_stall_now_range_ratio = 0.0;
	g_dbg_stall_prev_range_pips = 0.0;
	g_dbg_compression_score = 0.0;
	g_dbg_opposite_score = 0.0;
	g_dbg_wick_score = 0.0;
	g_dbg_giveback_score = 0.0;
	g_dbg_hybrid_score = 0.0;
	g_dbg_hybrid_max_score = 0.0;
	g_dbg_hybrid_normalized_score = 0.0;
	g_dbg_hybrid_threshold = 0.0;
	g_last_spread_pips = 0.0;
	g_last_spread_range_pips = 0.0;
	g_last_spread_ok = true;
	g_has_last_grid_index = false;
	SyncGridIndexToMarket();

	if(!ValidateInputs())
	{
		Print("Invalid hybrid soft weighted input");
		return INIT_PARAMETERS_INCORRECT;
	}

	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	if(EnableChartComment)
		Comment("");
}

double OnTester()
{
	double profit = TesterStatistics(STAT_PROFIT);
	double trades = TesterStatistics(STAT_TRADES);
	double equity_dd = TesterStatistics(STAT_EQUITY_DD);

	if(!TesterUseCustomScore)
		return profit;

	if(profit < TesterMinProfit)
		return -1000000000.0;

	if(trades < TesterMinTrades)
		return -1000000000.0;

	if(TesterUseMaxDrawdownFilter && equity_dd > TesterMaxDrawdownMoney)
		return -1000000000.0;

	if(TesterUseMaxDrawdownPercentFilter)
	{
		double equity_dd_percent = TesterStatistics(STAT_EQUITY_DDREL_PERCENT);

		if(equity_dd_percent > TesterMaxDrawdownPercent)
			return -1000000000.0;
	}

	double score =
		profit +
		TesterTradeBonus * trades -
		TesterDrawdownPenalty * equity_dd;

	if(EnableDebugPrints)
	{
		Print("OnTester custom score: profit=", profit,
			" trades=", trades,
			" equity_dd=", equity_dd,
			" score=", score);
	}

	return score;
}

void OnTick()
{
	RefreshRates();
	UpdateSpreadSamples();
	UpdateProfitSpikeSnapshots();
	UpdatePositiveProfitState();
	g_last_spread_ok = IsSpreadOK();
	LogSpreadExecutionFailure();

	if(g_last_spread_ok)
		ProcessGridEntry();
	else
		SyncGridIndexToMarket();

	UpdateSignalStates();

	bool harvest_signal = ShouldHarvest();
	if(harvest_signal && g_last_spread_ok)
		HarvestProfitableOrders();

	UpdatePositiveProfitState();
	UpdateSignalStates();
	UpdateChartComment();
}
