#property strict

#define ORDER_COMMENT_PREFIX "GH|G="

input int TradeDirection = -1;			// -1 SELL only, +1 BUY only
input double LotSize = 0.01;
input int GridStepPips = 5;
input int MaxOrders = 50;
input int MagicNumber = 777001;
input int SlippagePips = 3;

input int W_ExtremeDeviation = 4;
input int W_ProfitGiveback = 3;
input int W_CandlePressure = 1;
input double HarvestThreshold = 4.0;
input double ScoreClampMax = 10.0;

input int ExtremeWindowBars = 200;
input double WickCapMultiplier = 3.0;
input double DeviationK = 1.0;
input double MinWindowRangePips = 10.0;

input bool UseProfitGiveback = true;
input double MinPeakProfitPips = 5.0;
input double MaxProfitGivebackPercent = 35.0;
input double MinTotalPositiveProfitPips = 1.0;

input int CandlePressureLookback = 3;
input double CandlePressureDecay = 0.70;
input int CP_W_Body = 3;
input int CP_W_Close = 2;
input int CP_W_Wick = 2;

input double MinOrderProfitPips = 1.0;
input double CloseSafetyPips = 2.0;

input int SpreadSampleTicks = 10;
input double MaxSpreadPips = 3.0;
input double MaxAverageSpreadPips = 2.5;
input double MaxSpreadJumpPips = 1.0;

input bool EnableDebugPrints = false;
input bool EnableChartComment = true;

double g_pip = 0.0;
int g_last_grid_index = 0;
bool g_has_last_grid_index = false;

double g_spread_samples[];
int g_spread_pos = 0;
int g_spread_count = 0;

bool g_pending_harvest = false;

double g_positive_profit_now_pips = 0.0;
double g_positive_profit_peak_pips = 0.0;
int g_harvestable_orders = 0;
datetime g_last_bar_time = 0;

double g_cached_window_high = 0.0;
double g_cached_window_low = 0.0;
double g_cached_window_range = 0.0;
datetime g_cached_window_bar_time = 0;
bool g_has_cached_window = false;

double g_last_extreme_score = 0.0;
double g_last_giveback_score = 0.0;
double g_last_pressure_score = 0.0;
double g_last_harvest_score = 0.0;

double g_last_spread_current = 0.0;
double g_last_spread_average = 0.0;
double g_last_spread_range = 0.0;
bool g_last_spread_ok = false;
int g_last_spread_block_reason = -1;

#define SPREAD_BLOCK_NONE 0
#define SPREAD_BLOCK_SAMPLES 1
#define SPREAD_BLOCK_CURRENT 2
#define SPREAD_BLOCK_AVERAGE 3
#define SPREAD_BLOCK_JUMP 4

double GetPipValue()
{
	if(Digits == 5 || Digits == 3)
		return Point * 10.0;
	return Point;
}

void DebugPrint(string message)
{
	if(EnableDebugPrints)
		Print("GBot: ", message);
}

double ClampScore(double score)
{
	if(score < 0.0)
		return 0.0;
	if(score > ScoreClampMax)
		return ScoreClampMax;
	return score;
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
	double step = GridStepPrice();
	if(step <= 0.0)
		return 0;
	return (int)MathFloor(price / step);
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

void GetClippedCandle(int shift, double wick_cap, double &clipped_high, double &clipped_low, double &close_price)
{
	double open_price = Open[shift];
	double high_price = High[shift];
	double low_price = Low[shift];
	double close_value = Close[shift];
	double body_high = MathMax(open_price, close_value);
	double body_low = MathMin(open_price, close_value);
	double upper_wick = high_price - body_high;
	double lower_wick = body_low - low_price;
	double clipped_upper_wick = MathMin(upper_wick, wick_cap);
	double clipped_lower_wick = MathMin(lower_wick, wick_cap);

	clipped_high = body_high + clipped_upper_wick;
	clipped_low = body_low - clipped_lower_wick;
	close_price = close_value;
}

double GetMedianCandleRange(int bars)
{
	if(bars <= 0 || Bars <= bars)
		return 0.0;

	double ranges[];
	ArrayResize(ranges, bars);

	for(int i = 0; i < bars; i++)
		ranges[i] = High[i + 1] - Low[i + 1];

	ArraySort(ranges);

	if((bars % 2) == 1)
		return ranges[bars / 2];
	return (ranges[(bars / 2) - 1] + ranges[bars / 2]) * 0.5;
}

bool GetClippedWindowHighLow(int bars, double &window_high, double &window_low)
{
	if(bars <= 0 || Bars <= bars)
		return false;

	double median_range = GetMedianCandleRange(bars);
	if(median_range <= 0.0)
		return false;

	double wick_cap = median_range * WickCapMultiplier;
	bool initialized = false;

	for(int i = 1; i <= bars; i++)
	{
		double clipped_high = 0.0;
		double clipped_low = 0.0;
		double close_price = 0.0;
		GetClippedCandle(i, wick_cap, clipped_high, clipped_low, close_price);

		if(!initialized)
		{
			window_high = clipped_high;
			window_low = clipped_low;
			initialized = true;
		}
		else
		{
			if(clipped_high > window_high)
				window_high = clipped_high;
			if(clipped_low < window_low)
				window_low = clipped_low;
		}
	}

	if(!initialized || window_high <= window_low)
		return false;

	return true;
}

bool GetCachedWindow(double &window_high, double &window_low, double &window_range)
{
	datetime latest_closed_bar = 0;
	if(Bars > 1)
		latest_closed_bar = Time[1];

	if(g_has_cached_window && g_cached_window_bar_time == latest_closed_bar)
	{
		window_high = g_cached_window_high;
		window_low = g_cached_window_low;
		window_range = g_cached_window_range;
		return true;
	}

	if(!GetClippedWindowHighLow(ExtremeWindowBars, window_high, window_low))
		return false;

	window_range = window_high - window_low;
	if(window_range < (MinWindowRangePips * g_pip))
		return false;

	g_cached_window_high = window_high;
	g_cached_window_low = window_low;
	g_cached_window_range = window_range;
	g_cached_window_bar_time = latest_closed_bar;
	g_has_cached_window = true;

	return true;
}

bool IsOrderHarvestable()
{
	if(!OrderMatchesScope())
		return false;

	double profit_pips = 0.0;
	if(OrderType() == OP_BUY)
		profit_pips = (Bid - OrderOpenPrice()) / g_pip;
	else if(OrderType() == OP_SELL)
		profit_pips = (OrderOpenPrice() - Ask) / g_pip;
	else
		return false;

	if(profit_pips < (MinOrderProfitPips + CloseSafetyPips))
		return false;

	double net_profit = OrderProfit() + OrderSwap() + OrderCommission();
	if(net_profit <= 0.0)
		return false;

	return true;
}

void UpdatePositiveProfitState()
{
	g_positive_profit_now_pips = 0.0;
	g_harvestable_orders = 0;

	int total = OrdersTotal();
	for(int i = 0; i < total; i++)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!IsOrderHarvestable())
			continue;

		double profit_pips = 0.0;
		if(OrderType() == OP_BUY)
			profit_pips = (Bid - OrderOpenPrice()) / g_pip;
		else
			profit_pips = (OrderOpenPrice() - Ask) / g_pip;

		g_positive_profit_now_pips += profit_pips;
		g_harvestable_orders++;
	}

	if(g_positive_profit_now_pips > g_positive_profit_peak_pips)
		g_positive_profit_peak_pips = g_positive_profit_now_pips;
}

void UpdateSpreadSamples()
{
	double spread_pips = CurrentSpreadPips();
	g_last_spread_current = spread_pips;

	if(ArraySize(g_spread_samples) != SpreadSampleTicks)
		ArrayResize(g_spread_samples, SpreadSampleTicks);

	if(SpreadSampleTicks <= 0)
		return;

	g_spread_samples[g_spread_pos] = spread_pips;
	g_spread_pos++;
	if(g_spread_pos >= SpreadSampleTicks)
		g_spread_pos = 0;

	if(g_spread_count < SpreadSampleTicks)
		g_spread_count++;
}

bool IsSpreadStableForExecution()
{
	g_last_spread_average = 0.0;
	g_last_spread_range = 0.0;

	if(SpreadSampleTicks < 2 || g_spread_count < SpreadSampleTicks)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_SAMPLES;
		return false;
	}

	double min_spread = g_spread_samples[0];
	double max_spread = g_spread_samples[0];
	double sum_spread = 0.0;

	for(int i = 0; i < SpreadSampleTicks; i++)
	{
		double sample = g_spread_samples[i];
		sum_spread += sample;
		if(sample < min_spread)
			min_spread = sample;
		if(sample > max_spread)
			max_spread = sample;
	}

	g_last_spread_average = sum_spread / SpreadSampleTicks;
	g_last_spread_range = max_spread - min_spread;

	if(g_last_spread_current > MaxSpreadPips)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_CURRENT;
		return false;
	}
	if(g_last_spread_average > MaxAverageSpreadPips)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_AVERAGE;
		return false;
	}
	if(g_last_spread_range > MaxSpreadJumpPips)
	{
		g_last_spread_block_reason = SPREAD_BLOCK_JUMP;
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

	if(g_last_spread_block_reason == SPREAD_BLOCK_SAMPLES)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked by spread filter warm-up. SpreadSampleTicks=", SpreadSampleTicks,
			" collected_samples=", g_spread_count);
	}
	else if(g_last_spread_block_reason == SPREAD_BLOCK_CURRENT)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked because current spread exceeded MaxSpreadPips. current_spread_pips=",
			DoubleToString(g_last_spread_current, 2), " max_spread_pips=", DoubleToString(MaxSpreadPips, 2));
	}
	else if(g_last_spread_block_reason == SPREAD_BLOCK_AVERAGE)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked because average spread exceeded MaxAverageSpreadPips. average_spread_pips=",
			DoubleToString(g_last_spread_average, 2), " max_average_spread_pips=", DoubleToString(MaxAverageSpreadPips, 2),
			" current_spread_pips=", DoubleToString(g_last_spread_current, 2));
	}
	else if(g_last_spread_block_reason == SPREAD_BLOCK_JUMP)
	{
		Print("_EXECUTION_FAILURE_ Execution blocked because spread jump exceeded MaxSpreadJumpPips. spread_range_pips=",
			DoubleToString(g_last_spread_range, 2), " max_spread_jump_pips=", DoubleToString(MaxSpreadJumpPips, 2),
			" current_spread_pips=", DoubleToString(g_last_spread_current, 2),
			" average_spread_pips=", DoubleToString(g_last_spread_average, 2));
	}

	last_logged_reason = g_last_spread_block_reason;
}

double GetAdaptiveExtremeDeviationScore()
{
	double window_high = 0.0;
	double window_low = 0.0;
	double window_range = 0.0;
	if(!GetCachedWindow(window_high, window_low, window_range))
		return 0.0;

	double deviations[];
	ArrayResize(deviations, ExtremeWindowBars);

	double deviation_sum = 0.0;
	for(int i = 0; i < ExtremeWindowBars; i++)
	{
		double deviation = 0.0;
		double close_price = Close[i + 1];

		if(TradeDirection > 0)
			deviation = (window_high - close_price) / window_range;
		else
			deviation = (close_price - window_low) / window_range;

		if(deviation < 0.0)
			deviation = 0.0;

		deviations[i] = deviation;
		deviation_sum += deviation;
	}

	double avg_deviation = deviation_sum / ExtremeWindowBars;
	double variance = 0.0;
	for(int j = 0; j < ExtremeWindowBars; j++)
	{
		double delta = deviations[j] - avg_deviation;
		variance += delta * delta;
	}

	double std_deviation = MathSqrt(variance / ExtremeWindowBars);
	double normal_deviation = avg_deviation + (DeviationK * std_deviation);
	if(normal_deviation <= 0.0)
		return 0.0;

	double current_deviation = 0.0;
	if(TradeDirection > 0)
		current_deviation = (window_high - Bid) / window_range;
	else
		current_deviation = (Ask - window_low) / window_range;

	if(current_deviation <= 0.0)
		return 0.0;

	return ClampScore(current_deviation / normal_deviation);
}

double GetProfitGivebackScore()
{
	if(!UseProfitGiveback)
		return 0.0;
	if(g_positive_profit_peak_pips < MinPeakProfitPips)
		return 0.0;
	if(MaxProfitGivebackPercent <= 0.0)
		return 0.0;

	double giveback = g_positive_profit_peak_pips - g_positive_profit_now_pips;
	if(giveback <= 0.0)
		return 0.0;

	double giveback_ratio = giveback / g_positive_profit_peak_pips;
	double max_giveback_ratio = MaxProfitGivebackPercent / 100.0;
	if(max_giveback_ratio <= 0.0)
		return 0.0;

	return ClampScore(giveback_ratio / max_giveback_ratio);
}

double GetCandlePressureScore()
{
	if(CandlePressureLookback <= 0 || Bars <= CandlePressureLookback)
		return 0.0;

	double score = 0.0;
	double weight = 1.0;

	for(int i = 1; i <= CandlePressureLookback; i++)
	{
		double range = High[i] - Low[i];
		if(range <= 0.0)
		{
			weight *= CandlePressureDecay;
			continue;
		}

		double open_price = Open[i];
		double close_price = Close[i];
		double upper_wick = High[i] - MathMax(open_price, close_price);
		double lower_wick = MathMin(open_price, close_price) - Low[i];
		double close_position = (close_price - Low[i]) / range;

		double adverse_body = 0.0;
		double adverse_close = 0.0;
		double adverse_wick = 0.0;

		if(TradeDirection > 0)
		{
			adverse_body = MathMax(0.0, open_price - close_price) / range;
			adverse_close = 1.0 - close_position;
			adverse_wick = upper_wick / range;
		}
		else
		{
			adverse_body = MathMax(0.0, close_price - open_price) / range;
			adverse_close = close_position;
			adverse_wick = lower_wick / range;
		}

		double candle_pressure =
			(CP_W_Body * adverse_body) +
			(CP_W_Close * adverse_close) +
			(CP_W_Wick * adverse_wick);

		score += candle_pressure * weight;
		weight *= CandlePressureDecay;
	}

	return ClampScore(score);
}

bool CloseSelectedOrder()
{
	if(!IsOrderHarvestable())
		return false;

	double close_price = 0.0;
	if(OrderType() == OP_BUY)
		close_price = Bid;
	else if(OrderType() == OP_SELL)
		close_price = Ask;
	else
		return false;

	bool closed = OrderClose(OrderTicket(), OrderLots(), close_price, SlippagePoints(), clrNONE);
	if(!closed)
		DebugPrint("OrderClose failed. ticket=" + IntegerToString(OrderTicket()) + " err=" + IntegerToString(GetLastError()));

	return closed;
}

int HarvestProfitableOrders()
{
	int closed_count = 0;

	for(int i = OrdersTotal() - 1; i >= 0; i--)
	{
		if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES))
			continue;
		if(!OrderMatchesScope())
			continue;
		if(!IsOrderHarvestable())
			continue;

		RefreshRates();
		if(CloseSelectedOrder())
			closed_count++;
	}

	if(closed_count > 0)
	{
		UpdatePositiveProfitState();
		g_positive_profit_peak_pips = g_positive_profit_now_pips;
		g_pending_harvest = false;
	}

	return closed_count;
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

bool OpenGridOrder(int grid_index)
{
	if(CountOpenEAOrders() >= MaxOrders)
		return false;
	if(IsGridLevelOccupied(grid_index))
		return false;

	int order_type = MatchingOrderType();
	double price = (order_type == OP_BUY ? Ask : Bid);
	string comment = ORDER_COMMENT_PREFIX + IntegerToString(grid_index);
	int ticket = OrderSend(Symbol(), order_type, LotSize, price, SlippagePoints(), 0.0, 0.0, comment, MagicNumber, 0, clrNONE);

	if(ticket < 0)
	{
		int err = GetLastError();
		if(err == ERR_PRICE_CHANGED || err == ERR_REQUOTE)
		{
			Print("_EXECUTION_FAILURE_ OrderSend rejected because price moved beyond allowed slippage. SlippagePips may be too small. grid=", grid_index,
				" slippage_pips=", SlippagePips, " slippage_points=", SlippagePoints(), " err=", err);
		}
		DebugPrint("OrderSend failed. grid=" + IntegerToString(grid_index) + " err=" + IntegerToString(err));
		return false;
	}

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

	if(TradeDirection > 0)
	{
		if(grid_index > g_last_grid_index)
		{
			if(OpenGridOrder(grid_index))
				g_last_grid_index = grid_index;
			else
				g_last_grid_index = grid_index;
		}
	}
	else
	{
		if(grid_index < g_last_grid_index)
		{
			if(OpenGridOrder(grid_index))
				g_last_grid_index = grid_index;
			else
				g_last_grid_index = grid_index;
		}
	}
}

bool ShouldHarvestProfitableOrders()
{
	if(g_harvestable_orders <= 0)
	{
		g_last_extreme_score = 0.0;
		g_last_giveback_score = 0.0;
		g_last_pressure_score = 0.0;
		g_last_harvest_score = 0.0;
		return false;
	}
	if(g_positive_profit_now_pips < MinTotalPositiveProfitPips)
	{
		g_last_extreme_score = 0.0;
		g_last_giveback_score = 0.0;
		g_last_pressure_score = 0.0;
		g_last_harvest_score = 0.0;
		return false;
	}

	g_last_extreme_score = GetAdaptiveExtremeDeviationScore();
	g_last_giveback_score = GetProfitGivebackScore();
	g_last_pressure_score = GetCandlePressureScore();

	g_last_harvest_score =
		(W_ExtremeDeviation * g_last_extreme_score) +
		(W_ProfitGiveback * g_last_giveback_score) +
		(W_CandlePressure * g_last_pressure_score);

	if(g_last_harvest_score >= HarvestThreshold)
		return true;
	return false;
}

void UpdateChartComment()
{
	if(!EnableChartComment)
	{
		Comment("");
		return;
	}

	string direction_text = "SELL";
	if(TradeDirection > 0)
		direction_text = "BUY";

	string spread_state = "frozen";
	if(g_last_spread_ok)
		spread_state = "ok";

	Comment(
		"GBot\n",
		"Direction: ", direction_text,
		"\nGrid step: ", IntegerToString(GridStepPips),
		"\nOpen EA orders: ", IntegerToString(CountOpenEAOrders()),
		"\nHarvestable orders: ", IntegerToString(g_harvestable_orders),
		"\nPositive profit now: ", DoubleToString(g_positive_profit_now_pips, 2),
		"\nPositive profit peak: ", DoubleToString(g_positive_profit_peak_pips, 2),
		"\nExtremeDeviationScore: ", DoubleToString(g_last_extreme_score, 3),
		"\nProfitGivebackScore: ", DoubleToString(g_last_giveback_score, 3),
		"\nCandlePressureScore: ", DoubleToString(g_last_pressure_score, 3),
		"\nHarvestScore: ", DoubleToString(g_last_harvest_score, 3),
		"\nSpread current/avg/range: ",
		DoubleToString(g_last_spread_current, 2), " / ",
		DoubleToString(g_last_spread_average, 2), " / ",
		DoubleToString(g_last_spread_range, 2),
		"\nSpread state: ", spread_state,
		"\nPendingHarvest: ", (g_pending_harvest ? "true" : "false")
	);
}

bool ValidateInputs()
{
	if(!IsTradeDirectionValid())
		return false;
	if(GridStepPips <= 0)
		return false;
	if(LotSize <= 0.0)
		return false;
	if(MaxOrders <= 0)
		return false;
	if(ExtremeWindowBars < 10)
		return false;
	if(SpreadSampleTicks < 2)
		return false;
	if(g_pip <= 0.0)
		return false;
	if(UseProfitGiveback && MaxProfitGivebackPercent <= 0.0)
		return false;
	return true;
}

int OnInit()
{
	g_pip = GetPipValue();
	ArrayResize(g_spread_samples, SpreadSampleTicks);
	ArrayInitialize(g_spread_samples, 0.0);
	g_spread_pos = 0;
	g_spread_count = 0;
	g_positive_profit_peak_pips = 0.0;
	g_pending_harvest = false;
	g_has_cached_window = false;
	g_last_bar_time = 0;
	SyncGridIndexToMarket();

	if(!ValidateInputs())
	{
		Print("GBot: invalid inputs");
		return INIT_PARAMETERS_INCORRECT;
	}

	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	if(EnableChartComment)
		Comment("");
}

void OnTick()
{
	RefreshRates();
	UpdateSpreadSamples();
	UpdatePositiveProfitState();

	g_last_spread_ok = IsSpreadStableForExecution();
	LogSpreadExecutionFailure();

	bool harvest_signal = ShouldHarvestProfitableOrders();

	if(harvest_signal && g_last_spread_ok)
		HarvestProfitableOrders();

	if(harvest_signal && !g_last_spread_ok)
		g_pending_harvest = true;

	if(g_pending_harvest && g_last_spread_ok)
	{
		UpdatePositiveProfitState();
		if(ShouldHarvestProfitableOrders())
			HarvestProfitableOrders();
		else if(g_harvestable_orders <= 0 || g_positive_profit_now_pips < MinTotalPositiveProfitPips)
			g_pending_harvest = false;
	}

	if(g_last_spread_ok)
		ProcessGridEntry();
	else
		SyncGridIndexToMarket();

	UpdatePositiveProfitState();
	UpdateChartComment();
}

double OnTester()
{
	return TesterStatistics(STAT_PROFIT);
}
