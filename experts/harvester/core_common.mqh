#ifndef HARVESTER_CORE_COMMON_MQH
#define HARVESTER_CORE_COMMON_MQH

input int TradeDirection = -1;			// -1 SELL only, +1 BUY only
input double LotSize = 0.01;
input int GridStepPips = 20;
input int MaxOrders = 200;
input int MagicNumber = 778001;
input int SlippagePips = 20;

input double MinOrderProfitPips = 8.0;
input double CloseSafetyPips = 4.0;
input double MinTotalProfitPipsToHarvest = 10.0;
input int MinProfitableOrdersToHarvest = 1;

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

double g_positive_profit_now_pips = 0.0;
double g_positive_profit_peak_pips = 0.0;
double g_harvestable_profit_pips = 0.0;
int g_harvestable_orders = 0;

double GetPipValue()
{
	if(Digits == 5 || Digits == 3)
		return Point * 10.0;
	return Point;
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
	double requested_price = (order_type == OP_BUY ? Ask : Bid);
	double grid_price = GridLevelPrice(grid_index);
	string comment = ORDER_COMMENT_PREFIX + IntegerToString(grid_index);
	int slippage_points = SlippagePoints();
	double slippage_pips_limit = (g_pip > 0.0 ? (slippage_points * Point) / g_pip : 0.0);
	int ticket = OrderSend(Symbol(), order_type, LotSize, requested_price, slippage_points, 0.0, 0.0, comment, MagicNumber, 0, clrNONE);

	if(ticket < 0)
	{
		int err = GetLastError();
		Print(
			"_EXECUTION_FAILURE_ GRID_OPEN_FAILED mode=", StrategyModeName(),
			" grid_index=", grid_index,
			" grid_price=", DoubleToString(grid_price, Digits),
			" requested_price=", DoubleToString(requested_price, Digits),
			" slippage_points=", slippage_points,
			" slippage_pips_limit=", DoubleToString(slippage_pips_limit, 2),
			" err=", err
		);
		DebugPrint("OrderSend failed. grid=" + IntegerToString(grid_index) + " err=" + IntegerToString(err));
		return false;
	}

	double actual_open_price = requested_price;
	double deviation_pips = 0.0;
	bool order_selected = OrderSelect(ticket, SELECT_BY_TICKET);
	if(order_selected)
	{
		actual_open_price = OrderOpenPrice();
		if(g_pip > 0.0)
			deviation_pips = MathAbs(actual_open_price - requested_price) / g_pip;
	}

	Print(
		"GRID_OPEN ticket=", ticket,
		" mode=", StrategyModeName(),
		" grid_index=", grid_index,
		" grid_price=", DoubleToString(grid_price, Digits),
		" requested_price=", DoubleToString(requested_price, Digits),
		" actual_open_price=", DoubleToString(actual_open_price, Digits),
		" deviation_pips=", DoubleToString(deviation_pips, 2),
		" slippage_pips_limit=", DoubleToString(slippage_pips_limit, 2),
		" step_pips=", GridStepPips
	);

	Print(
		"GRID_OPEN_EXECUTION ticket=", ticket,
		" mode=", StrategyModeName(),
		" requested_price=", DoubleToString(requested_price, Digits),
		" actual_open_price=", DoubleToString(actual_open_price, Digits),
		" deviation_pips=", DoubleToString(deviation_pips, 2),
		" slippage_points=", slippage_points,
		" slippage_pips_limit=", DoubleToString(slippage_pips_limit, 2),
		" order_selected=", (order_selected ? 1 : 0)
	);
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
		return false;

	if(!UseSpreadJumpFilter)
		return true;

	if(SpreadSampleTicks <= 0 || g_spread_count < SpreadSampleTicks)
		return false;

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
		return false;

	return true;
}

void RefreshSpreadState()
{
	g_last_spread_pips = CurrentSpreadPips();
	g_last_spread_ok = IsSpreadOK();
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

int HarvestProfitableOrders()
{
	int closed_orders = 0;
	int total = OrdersTotal();

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
		int order_type = OrderType();
		int slippage_points = SlippagePoints();
		double slippage_pips_limit = (g_pip > 0.0 ? (slippage_points * Point) / g_pip : 0.0);

		RefreshRates();
		RefreshSpreadState();
		if(!g_last_spread_ok)
		{
			Print(
				"_EXECUTION_FAILURE_ CLOSE_BLOCKED_SPREAD mode=", StrategyModeName(),
				" ticket=", ticket,
				" order_type=", order_type,
				" spread_pips=", DoubleToString(g_last_spread_pips, 2),
				" spread_range_pips=", DoubleToString(g_last_spread_range_pips, 2),
				" max_spread_pips=", DoubleToString(MaxSpreadPips, 2),
				" max_spread_jump_pips=", DoubleToString(MaxSpreadJumpPips, 2)
			);
			continue;
		}

		if(!OrderSelect(ticket, SELECT_BY_TICKET))
			continue;

		double live_profit_pips = GetOrderProfitPips();
		double live_money_profit = OrderProfit() + OrderSwap() + OrderCommission();
		if(live_profit_pips < (MinOrderProfitPips + CloseSafetyPips) || live_money_profit <= 0.0)
		{
			Print(
				"_EXECUTION_FAILURE_ CLOSE_BLOCKED_PROFIT mode=", StrategyModeName(),
				" ticket=", ticket,
				" order_type=", OrderType(),
				" live_profit_pips=", DoubleToString(live_profit_pips, 2),
				" live_money_profit=", DoubleToString(live_money_profit, 2),
				" min_profit_pips=", DoubleToString(MinOrderProfitPips, 2),
				" close_safety_pips=", DoubleToString(CloseSafetyPips, 2),
				" spread_pips=", DoubleToString(g_last_spread_pips, 2)
			);
			continue;
		}

		profit_pips = live_profit_pips;
		money_profit = live_money_profit;

		if(OrderType() == OP_BUY)
			close_price = Bid;
		else
			close_price = Ask;

		if(OrderClose(ticket, lots, close_price, slippage_points, clrNONE))
		{
			double actual_close_price = close_price;
			double deviation_pips = 0.0;
			bool history_selected = OrderSelect(ticket, SELECT_BY_TICKET, MODE_HISTORY);
			if(history_selected)
			{
				actual_close_price = OrderClosePrice();
				if(g_pip > 0.0)
					deviation_pips = MathAbs(actual_close_price - close_price) / g_pip;
			}

			closed_orders++;
			Print(
				"HARVEST_CLOSE mode=", StrategyModeName(),
				" ticket=", ticket,
				" order_type=", order_type,
				" lots=", DoubleToString(lots, 2),
				" profit_pips=", DoubleToString(profit_pips, 2),
				" money_profit=", DoubleToString(money_profit, 2),
				" requested_close_price=", DoubleToString(close_price, Digits),
				" actual_close_price=", DoubleToString(actual_close_price, Digits),
				" deviation_pips=", DoubleToString(deviation_pips, 2),
				" slippage_pips_limit=", DoubleToString(slippage_pips_limit, 2)
			);
			Print(
				"HARVEST_CLOSE_EXECUTION mode=", StrategyModeName(),
				" ticket=", ticket,
				" requested_close_price=", DoubleToString(close_price, Digits),
				" actual_close_price=", DoubleToString(actual_close_price, Digits),
				" deviation_pips=", DoubleToString(deviation_pips, 2),
				" slippage_points=", slippage_points,
				" slippage_pips_limit=", DoubleToString(slippage_pips_limit, 2),
				" history_selected=", (history_selected ? 1 : 0)
			);
		}
		else
		{
			int err = GetLastError();
			Print(
				"_EXECUTION_FAILURE_ CLOSE_FAILED mode=", StrategyModeName(),
				" ticket=", ticket,
				" order_type=", order_type,
				" requested_close_price=", DoubleToString(close_price, Digits),
				" profit_pips=", DoubleToString(profit_pips, 2),
				" money_profit=", DoubleToString(money_profit, 2),
				" spread_pips=", DoubleToString(g_last_spread_pips, 2),
				" slippage_points=", slippage_points,
				" slippage_pips_limit=", DoubleToString(slippage_pips_limit, 2),
				" err=", err
			);
			DebugPrint("OrderClose failed. ticket=" + IntegerToString(ticket) + " err=" + IntegerToString(err));
		}
	}

	if(closed_orders > 0)
	{
		UpdatePositiveProfitState();
		g_positive_profit_peak_pips = GetTotalPositiveProfitPips();
		StrategyOnHarvestSuccess();
	}

	return closed_orders;
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
		"Harvester" +
		"\nOpen EA orders: " + IntegerToString(CountOpenEAOrders()) +
		"\nHarvestable orders: " + IntegerToString(g_harvestable_orders) +
		"\nHarvestable profit pips: " + DoubleToString(g_harvestable_profit_pips, 2) +
		"\nTotal positive profit pips: " + DoubleToString(g_positive_profit_now_pips, 2) +
		"\nPositive profit peak pips: " + DoubleToString(g_positive_profit_peak_pips, 2) +
		"\nSpread pips: " + DoubleToString(g_last_spread_pips, 2) +
		"\nSpread range pips: " + DoubleToString(g_last_spread_range_pips, 2) +
		"\nSpread OK: " + BoolText(g_last_spread_ok);

	StrategyAppendComment(text);
	Comment(text);
}

bool ValidateCommonInputs()
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
	if(UseSpreadFilter && MaxSpreadPips < 0.0)
		return false;
	if(UseSpreadJumpFilter && (SpreadSampleTicks <= 0 || MaxSpreadJumpPips < 0.0))
		return false;
	if(TesterMinTrades < 0)
		return false;
	if(TesterTradeBonus < 0.0)
		return false;
	if(TesterDrawdownPenalty < 0.0)
		return false;
	if(TesterMaxDrawdownMoney < 0.0)
		return false;
	if(TesterMaxDrawdownPercent < 0.0)
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
	g_last_spread_pips = 0.0;
	g_last_spread_range_pips = 0.0;
	g_last_spread_ok = true;
	g_has_last_grid_index = false;
	SyncGridIndexToMarket();
	StrategyResetState();

	if(!ValidateCommonInputs() || !StrategyValidateInputs())
	{
		Print("Harvester: invalid inputs");
		return INIT_PARAMETERS_INCORRECT;
	}

	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	StrategyOnDeinit(reason);

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
	StrategyOnTickPrelude();
	UpdatePositiveProfitState();
	g_last_spread_ok = IsSpreadOK();

	if(g_last_spread_ok)
		ProcessGridEntry();
	else
		SyncGridIndexToMarket();

	StrategyUpdateSignalState();

	bool harvest_signal = StrategyShouldHarvest();
	if(harvest_signal && g_last_spread_ok)
		HarvestProfitableOrders();

	UpdatePositiveProfitState();
	StrategyUpdateSignalState();
	UpdateChartComment();
}

#endif
