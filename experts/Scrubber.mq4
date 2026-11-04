#property strict
#property version "2.00"
#property description "Pending stop/limit grid; independent tick-based net-profit trailing per position."

#include "scrubber/profit_math.mqh"

enum SCRUBBER_DIRECTION { SHORT_ONLY = -1, LONG_ONLY = 1 };
input SCRUBBER_DIRECTION TradeDirection = LONG_ONLY;
input double LotSizeFX = 0.01;     // LotSizeFX: currency pairs only
input double LotSizeNAS100 = 0.01; // LotSizeNAS100: Nasdaq 100: US100 / NAS / USTEC aliases
input double LotSizeGold = 0.01;   // LotSizeGold: XAU / GOLD aliases
input double GridStepPips = 20.0;
input double PipSizeOverride = 0.0; // PipSizeOverride: price units; 0 = FX/metals/known Nasdaq auto detection
input int PendingGridLevels = 3; // PendingGridLevels: placement levels per side; retained orders can add more
input int PendingRetentionExtraLevels = 3; // PendingRetentionExtraLevels: extra grid intervals to retain existing pending orders
input int MaxOrders = 200; // MaxOrders: own market + pending orders, never a lot multiplier
input int MagicNumber = 779001;
input double MaximumProfitDrawdownPercent = 15.0; // MaximumProfitDrawdownPercent: retracement from each activated position's own peak
input double position_minimum_profit_floor = 0.0; // position_minimum_profit_floor: net pips to activate permanent per-position trailing; 0 = any positive net
input double ExitCommissionPerLot = -1.0; // ExitCommissionPerLot: -1 reserves abs(booked negative commission); 0 = prepaid

// Harvester spread and slippage mechanics. Spread gates entries, NEVER profit exits.
input int SlippagePips = 20;
input bool UseSpreadFilter = true;
input double MaxSpreadPips = 40.0;
input bool UseSpreadJumpFilter = false;
input int SpreadSampleTicks = 10;
input double MaxSpreadJumpPips = 20.0;

input int FridayCloseMinutesServer = -1; // FridayCloseMinutesServer: -1 = broker Friday sessions; 1435 = 23:55; 1440 = midnight
input int MinutesBeforeFridayClose = 20;
input datetime SpecialCloseServerTime = 0; // SpecialCloseServerTime: optional holiday/early-close override for this symbol

// Same optimization fitness and filters as Harvester.
input bool TesterUseCustomScore = true;
input int TesterMinTrades = 3000;
input double TesterMinProfit = 1000.0;
input double TesterTradeBonus = 3.0;
input double TesterDrawdownPenalty = 0.50;
input bool TesterUseMaxDrawdownFilter = true;
input double TesterMaxDrawdownMoney = 25000.0;
input bool TesterUseMaxDrawdownPercentFilter = false;
input double TesterMaxDrawdownPercent = 25.0;

input bool EnableDebugPrints = true; // EnableDebugPrints: add market/profit context to action logs; no per-tick logging
input bool EnableChartComment = true;

double g_pip = 0.0, g_tick = 0.0, g_grid = 0.0;
double g_lot_size = 0.0; // selected once at initialization, fixed for this run
string g_lot_input = "";
double g_spread = 0.0, g_spread_range = 0.0;
double g_spreads[1000];
int g_sample_count = 0, g_sample_pos = 0;
bool g_spread_ok = false, g_ready = false;
bool g_timer_started = false;
bool g_expiration_denied = false, g_uncertain_send = false;
datetime g_retry_entry_after = 0, g_last_quote_server = 0;
uint g_last_quote_ms = 0;
string g_key = "", g_last_block = "";
string g_lock_key = "";
bool g_owns_lock = false;
string g_block_code = "";
struct ScrubberPositionState
{
   int ticket;
   double lots;
   double peak; // account currency, costs included; >0 means permanently armed
   bool close_due;
   bool legacy_stop;
   bool dirty;
   datetime retry_close_after;
   datetime retry_stop_after;
};
ScrubberPositionState g_positions[SCRUBBER_CAPACITY];
int g_position_count = 0;

void Log(string text) { Print("Scrubber ", Symbol(), " magic=", MagicNumber, " ", text); }
string N(double value) { return DoubleToString(value, 8); }
void LogAction(string text)
{
   if(EnableDebugPrints)
      text += " | bid=" + N(Bid) + " ask=" + N(Ask) +
         " spread_pips=" + N(g_pip > 0.0 ? (Ask - Bid) / g_pip : 0.0) +
         " position_min_pips=" + N(position_minimum_profit_floor);
   Log(text);
}
int Direction() { return (int)TradeDirection; }
int MarketType() { return Direction() > 0 ? OP_BUY : OP_SELL; }
// side = +1 above the executable entry quote, -1 below it.
int PendingType(int side)
{
   if(Direction() > 0) return side > 0 ? OP_BUYSTOP : OP_BUYLIMIT;
   return side > 0 ? OP_SELLLIMIT : OP_SELLSTOP;
}
bool PendingMatchesDirection(int type)
{
   return type == PendingType(1) || type == PendingType(-1);
}
string PendingTypeName(int type)
{
   if(type == OP_BUYSTOP) return "BUY_STOP";
   if(type == OP_BUYLIMIT) return "BUY_LIMIT";
   if(type == OP_SELLSTOP) return "SELL_STOP";
   return "SELL_LIMIT";
}
bool OwnSelected() { return OrderSymbol() == Symbol() && OrderMagicNumber() == MagicNumber; }
bool MarketSelected() { return OwnSelected() && OrderType() == MarketType(); }
bool PendingSelected() { return OwnSelected() && OrderType() >= OP_BUYLIMIT && OrderType() <= OP_SELLSTOP; }
double ExitPrice() { return Direction() > 0 ? Bid : Ask; }
double EntryPrice() { return Direction() > 0 ? Ask : Bid; }
int SlippagePoints() { return (int)MathRound(SlippagePips * g_pip / Point); }

void Block(string reason, string code = "")
{
   if(code == "") code = reason;
   if(code != g_block_code) LogAction("ENTRY_BLOCK " + reason);
   g_block_code = code;
   g_last_block = reason;
}

void ClearBlock()
{
   if(g_block_code != "") LogAction("ENTRY_RESUMED previous=" + g_block_code);
   g_block_code = "";
   g_last_block = "";
}

double ExitReserve()
{
   if(ExitCommissionPerLot >= 0.0) return ExitCommissionPerLot * OrderLots();
   return MathMax(0.0, -OrderCommission());
}
double SelectedNet() { return OrderProfit() + OrderSwap() + OrderCommission() - ExitReserve(); }

// Metals may use the broker's forex profit-calculation mode. Classify them
// before the FX fallback so currency-pair settings never size metal orders.
enum SCRUBBER_INSTRUMENT { INSTRUMENT_UNKNOWN, INSTRUMENT_FX, INSTRUMENT_GOLD, INSTRUMENT_NAS100 };
SCRUBBER_INSTRUMENT Instrument()
{
   string sym = Symbol();
   StringToUpper(sym);
   if(StringFind(sym, "XAU") >= 0 || StringFind(sym, "GOLD") >= 0)
      return INSTRUMENT_GOLD;
   if(StringFind(sym, "US100") >= 0 || StringFind(sym, "NAS") >= 0 || StringFind(sym, "USTEC") >= 0)
      return INSTRUMENT_NAS100;
   if(StringFind(sym, "XAG") >= 0 || StringFind(sym, "SILVER") >= 0 ||
      StringFind(sym, "XPT") >= 0 || StringFind(sym, "XPD") >= 0)
      return INSTRUMENT_UNKNOWN;
   if((int)MarketInfo(Symbol(), MODE_PROFITCALCMODE) == 0)
      return INSTRUMENT_FX;
   return INSTRUMENT_UNKNOWN;
}

bool SelectFixedLotSize()
{
   SCRUBBER_INSTRUMENT instrument = Instrument();
   if(instrument == INSTRUMENT_NAS100) { g_lot_size = LotSizeNAS100; g_lot_input = "LotSizeNAS100"; }
   else if(instrument == INSTRUMENT_GOLD) { g_lot_size = LotSizeGold; g_lot_input = "LotSizeGold"; }
   else if(instrument == INSTRUMENT_FX) { g_lot_size = LotSizeFX; g_lot_input = "LotSizeFX"; }
   else { Log("INIT_FAILED unsupported instrument: no applicable FX/Nasdaq/gold lot-size input"); return false; }
   return true;
}

double AutoPipSize()
{
   if(PipSizeOverride > 0.0) return PipSizeOverride;
   SCRUBBER_INSTRUMENT instrument = Instrument();
   if(instrument == INSTRUMENT_GOLD) return 0.01;
   if(instrument == INSTRUMENT_NAS100) return 1.0;
   if(instrument == INSTRUMENT_FX)
      return (Digits == 3 || Digits == 5) ? 10.0 * Point : Point;
   return 0.0;
}

bool TradeReady()
{
   return (IsTesting() || IsConnected()) && IsTradeAllowed() && !IsTradeContextBusy();
}

double BrokerDistance()
{
   return MathMax(MarketInfo(Symbol(), MODE_STOPLEVEL), MarketInfo(Symbol(), MODE_FREEZELEVEL)) * Point + g_tick;
}

// Friday is derived from broker server calendar, never from the workstation timezone.
datetime ServerNow()
{
   datetime now = TimeCurrent();
   if(!IsTesting() && g_last_quote_server > 0)
   {
      datetime estimated = g_last_quote_server + (datetime)((uint)(GetTickCount() - g_last_quote_ms) / 1000);
      if(estimated > now) now = estimated;
   }
   return now;
}

bool EntryDeadline(datetime now, datetime &deadline)
{
   int dow = TimeDayOfWeek(now);
   datetime midnight = now - TimeHour(now) * 3600 - TimeMinute(now) * 60 - TimeSeconds(now);
   datetime friday = midnight + (5 - dow) * 86400;
   int close_seconds = -1;
   if(FridayCloseMinutesServer >= 0) close_seconds = FridayCloseMinutesServer * 60;
   else
   {
      for(uint s = 0; s < 32; ++s)
      {
         datetime from = 0, to = 0;
         if(!SymbolInfoSessionTrade(Symbol(), FRIDAY, s, from, to)) break;
         int start = (int)((long)from % 86400);
         int finish = (int)((long)to % 86400);
         if(finish <= start) finish += 86400; // midnight or overnight session
         if(finish > close_seconds) close_seconds = finish;
      }
   }
   deadline = 0;
   if(close_seconds < 0) return false;
   deadline = friday + close_seconds - MinutesBeforeFridayClose * 60;
   if(SpecialCloseServerTime > 0)
   {
      datetime special = SpecialCloseServerTime - MinutesBeforeFridayClose * 60;
      // Keep entries off through that server calendar day after an early close.
      datetime special_day_end = SpecialCloseServerTime - TimeHour(SpecialCloseServerTime) * 3600 -
                                TimeMinute(SpecialCloseServerTime) * 60 - TimeSeconds(SpecialCloseServerTime) + 86400;
      if(now < special_day_end && special < deadline) deadline = special;
   }
   return true;
}

void SampleSpread()
{
   g_spread = (Ask - Bid) / g_pip;
   if(UseSpreadJumpFilter)
   {
      g_spreads[g_sample_pos] = g_spread;
      g_sample_pos = (g_sample_pos + 1) % SpreadSampleTicks;
      if(g_sample_count < SpreadSampleTicks) ++g_sample_count;
   }
}

bool SpreadAllowed()
{
   g_spread = (Ask - Bid) / g_pip;
   g_spread_range = 0.0;
   if(UseSpreadFilter && g_spread > MaxSpreadPips) return false;
   if(!UseSpreadJumpFilter) return true;
   if(g_sample_count < SpreadSampleTicks) return false;
   double lo = g_spread, hi = g_spread;
   for(int i = 0; i < g_sample_count; ++i)
   {
      lo = MathMin(lo, g_spreads[i]); hi = MathMax(hi, g_spreads[i]);
   }
   g_spread_range = hi - lo;
   return g_spread_range <= MaxSpreadJumpPips;
}

bool DeletePending(int ticket, string reason)
{
   if(!OrderSelect(ticket, SELECT_BY_TICKET) || OrderCloseTime() != 0 || !PendingSelected()) return true;
   if(!TradeReady()) return false;
   ResetLastError();
   if(!OrderDelete(ticket, clrNONE))
   {
      LogAction("DELETE_FAILED ticket=" + IntegerToString(ticket) + " err=" + IntegerToString(GetLastError()) + " " + reason);
      return false;
   }
   LogAction("PENDING_DELETED ticket=" + IntegerToString(ticket) + " " + reason);
   return true;
}

bool DeleteAllPending(string reason)
{
   bool ok = true;
   for(int i = OrdersTotal() - 1; i >= 0; --i)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES) && PendingSelected())
         if(!DeletePending(OrderTicket(), reason)) ok = false;
   return ok;
}

bool InverseConversion()
{
   return MarketInfo(Symbol(), MODE_PROFITCALCMODE) == 0 &&
          SymbolInfoString(Symbol(), SYMBOL_CURRENCY_BASE) == AccountCurrency();
}

int PositionIndex(int ticket)
{
   for(int i = 0; i < g_position_count; ++i)
      if(g_positions[i].ticket == ticket) return i;
   return -1;
}

string PositionKey(int ticket) { return g_key + ".p2" + IntegerToString(ticket); }

void SavePosition(int i)
{
   if(IsTesting() || !g_positions[i].dirty || g_positions[i].legacy_stop) return;
   // A single value atomically stores activation, peak-per-lot and the retry
   // latch (negative = close due). Peak-per-lot survives same-ticket partial closes.
   double value = g_positions[i].peak / g_positions[i].lots;
   if(g_positions[i].close_due) value = -value;
   GlobalVariableSet(PositionKey(g_positions[i].ticket), value);
   g_positions[i].dirty = false;
}

void SaveState()
{
   if(IsTesting()) return;
   GlobalVariableSet(g_key + ".uncertain", g_uncertain_send ? 1.0 : 0.0);
   for(int i = 0; i < g_position_count; ++i) SavePosition(i);
}

void ForgetPosition(int i)
{
   if(!IsTesting()) GlobalVariableDel(PositionKey(g_positions[i].ticket));
   --g_position_count;
   if(i != g_position_count) g_positions[i] = g_positions[g_position_count];
}

string PositionDetails(int i, double net)
{
   return " ticket=" + IntegerToString(g_positions[i].ticket) + " current=" + N(net) +
      " peak=" + N(g_positions[i].peak) +
      " floor=" + N(g_positions[i].peak * (1.0 - MaximumProfitDrawdownPercent / 100.0));
}

bool SyncPositions()
{
   for(int i = g_position_count - 1; i >= 0; --i)
   {
      if(!OrderSelect(g_positions[i].ticket, SELECT_BY_TICKET))
      {
         // Closed history may be outside the terminal's selected history range.
         // Confirm absence from the live pool before retiring an unknown ticket.
         bool found = false;
         for(int n = 0; n < OrdersTotal(); ++n)
         {
            if(!OrderSelect(n, SELECT_BY_POS, MODE_TRADES)) return false;
            if(OrderTicket() == g_positions[i].ticket) { found = true; break; }
         }
         if(found) continue;
         LogAction("POSITION_RETIRED ticket=" + IntegerToString(g_positions[i].ticket) + " absent from live pool; closed history unavailable");
         ForgetPosition(i);
      }
      else if(OrderCloseTime() != 0)
      {
         LogAction("POSITION_REALIZED" + PositionDetails(i, OrderProfit() + OrderSwap() + OrderCommission()));
         ForgetPosition(i); // no other position's peak or exit latch is touched
      }
   }
   for(int n = 0; n < OrdersTotal(); ++n)
   {
      if(!OrderSelect(n, SELECT_BY_POS, MODE_TRADES)) return false;
      if(!MarketSelected()) continue;
      int ticket = OrderTicket(), i = PositionIndex(ticket);
      if(i >= 0) continue;
      if(g_position_count >= SCRUBBER_CAPACITY) return false;
      i = g_position_count++;
      g_positions[i].ticket = ticket;
      g_positions[i].lots = OrderLots();
      g_positions[i].peak = 0.0;
      g_positions[i].close_due = false;
      g_positions[i].retry_close_after = 0;
      g_positions[i].retry_stop_after = 0;
      g_positions[i].dirty = true;
      bool restored = !IsTesting() && GlobalVariableCheck(PositionKey(ticket));
      if(restored)
      {
         double value = GlobalVariableGet(PositionKey(ticket));
         if(MathIsValidNumber(value))
         {
            g_positions[i].peak = MathAbs(value) * OrderLots();
            g_positions[i].close_due = value < 0.0;
            LogAction("POSITION_RESTORED" + PositionDetails(i, SelectedNet()));
         }
         else restored = false;
      }
      // Old basket stops have no independent position state. Retire them before
      // installing this strategy's stops; never import the old aggregate peak.
      g_positions[i].legacy_stop = !restored && OrderStopLoss() > 0.0;
      SavePosition(i);
   }
   return true;
}

bool ManagePositions()
{
   bool valid = true;
   for(int i = g_position_count - 1; i >= 0; --i)
   {
      RefreshRates();
      if(!OrderSelect(g_positions[i].ticket, SELECT_BY_TICKET) || OrderCloseTime() != 0 || !MarketSelected()) continue;
      if(OrderLots() != g_positions[i].lots)
      {
         g_positions[i].peak *= OrderLots() / g_positions[i].lots;
         g_positions[i].lots = OrderLots();
         g_positions[i].dirty = true;
         LogAction("POSITION_RESIZED" + PositionDetails(i, SelectedNet()));
      }
      double net = SelectedNet();
      double slope = MarketInfo(Symbol(), MODE_TICKVALUE) * OrderLots() / g_tick;
      bool valued = slope > 0.0 && MathIsValidNumber(slope);
      if(!valued) valid = false; // already armed cash-based exits can still operate
      if(!MathIsValidNumber(net)) { valid = false; continue; }
      if(g_positions[i].peak <= 0.0)
      {
         if(ScrubberMeetsFloor(net, slope, position_minimum_profit_floor * g_pip))
         {
            g_positions[i].peak = net;
            g_positions[i].dirty = true;
            LogAction("POSITION_ARMED" + PositionDetails(i, net));
         }
      }
      else if(net > g_positions[i].peak && !g_positions[i].close_due)
      {
         g_positions[i].peak = net;
         g_positions[i].dirty = true;
      }
      if(!g_positions[i].close_due && ScrubberDrawdownHit(net, g_positions[i].peak, MaximumProfitDrawdownPercent))
      {
         g_positions[i].close_due = true;
         g_positions[i].dirty = true;
         LogAction("POSITION_DRAWDOWN_TRIGGER" + PositionDetails(i, net));
      }
      SavePosition(i);
      if(g_positions[i].close_due)
      {
         // A gap may leave this order negative: retain it until net-positive.
         // Retrying a rejected close never changes another ticket's state.
         if(net <= 0.0 || !TradeReady() || ServerNow() < g_positions[i].retry_close_after) continue;
         ResetLastError();
         if(OrderClose(OrderTicket(), OrderLots(), ExitPrice(), SlippagePoints(), clrNONE))
         {
            LogAction("POSITION_CLOSED" + PositionDetails(i, net));
            ForgetPosition(i);
         }
         else
         {
            LogAction("CLOSE_FAILED" + PositionDetails(i, net) + " err=" + IntegerToString(GetLastError()));
            g_positions[i].retry_close_after = ServerNow() + 2;
         }
         continue;
      }
      if(g_positions[i].legacy_stop)
      {
         if(OrderStopLoss() <= 0.0) g_positions[i].legacy_stop = false;
         else
         {
            if(!TradeReady() || ServerNow() < g_positions[i].retry_stop_after ||
               Direction() * (ExitPrice() - OrderStopLoss()) < BrokerDistance()) continue;
            ResetLastError();
            if(!OrderModify(OrderTicket(), OrderOpenPrice(), 0.0, OrderTakeProfit(), 0, clrNONE))
            {
               LogAction("STOP_REMOVE_FAILED" + PositionDetails(i, net) + " err=" + IntegerToString(GetLastError()));
               g_positions[i].retry_stop_after = ServerNow() + 2;
               continue;
            }
            LogAction("LEGACY_STOP_REMOVED" + PositionDetails(i, net));
            g_positions[i].legacy_stop = false;
         }
         SavePosition(i);
      }
      if(g_positions[i].peak <= 0.0 || net <= 0.0 || !valued) continue;
      double target = g_positions[i].peak * (1.0 - MaximumProfitDrawdownPercent / 100.0);
      double raw_stop = 0.0;
      double costs = OrderSwap() + OrderCommission() - ExitReserve();
      if(!ScrubberPositionStop(OrderProfit(), costs, slope, ExitPrice(), Direction(), InverseConversion(), target, raw_stop)) continue;
      double stop = NormalizeDouble(ScrubberRoundPrice(raw_stop, g_tick, Direction() > 0), Digits);
      double old_stop = OrderStopLoss();
      if(old_stop > 0.0 && Direction() * (stop - old_stop) < g_tick * 0.5) continue;
      if(Direction() * (ExitPrice() - stop) < BrokerDistance() || !TradeReady() ||
         ServerNow() < g_positions[i].retry_stop_after) continue;
      ResetLastError();
      if(OrderModify(OrderTicket(), OrderOpenPrice(), stop, OrderTakeProfit(), 0, clrNONE))
         LogAction("STOP_UPDATED" + PositionDetails(i, net) + " SL=" + N(stop));
      else
      {
         LogAction("STOP_FAILED" + PositionDetails(i, net) + " err=" + IntegerToString(GetLastError()));
         g_positions[i].retry_stop_after = ServerNow() + 2;
      }
   }
   return valid;
}

long SelectedGrid()
{
   string comment = OrderComment();
   if(StringFind(comment, "Scrubber|G=") == 0)
      return (long)StringToInteger(StringSubstr(comment, 11));
   string key = g_key + ".g" + IntegerToString(OrderTicket());
   if(!IsTesting() && GlobalVariableCheck(key)) return (long)GlobalVariableGet(key);
   return (long)MathRound(OrderOpenPrice() / g_grid);
}

bool GridOccupied(long level)
{
   for(int i = 0; i < OrdersTotal(); ++i)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES) && OwnSelected() &&
         (OrderType() == MarketType() || PendingMatchesDirection(OrderType())) && SelectedGrid() == level)
         return true;
   return false;
}

int OwnOrderCount()
{
   int n = 0;
   for(int i = 0; i < OrdersTotal(); ++i)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES) && OwnSelected()) ++n;
   return n;
}

bool EarlierPendingAtGrid(long grid, int ticket)
{
   for(int i = 0; i < OrdersTotal(); ++i)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES) && OwnSelected() &&
         PendingMatchesDirection(OrderType()) && OrderTicket() < ticket && SelectedGrid() == grid)
         return true;
   return false;
}

bool WrongDirectionExists()
{
   for(int i = 0; i < OrdersTotal(); ++i)
      if(OrderSelect(i, SELECT_BY_POS, MODE_TRADES) && OwnSelected() &&
         OrderType() <= OP_SELL && OrderType() != MarketType()) return true;
   return false;
}

void ManageGrid(datetime now)
{
   datetime deadline = 0;
   string reason = "";
   string block_code = "";
   bool schedule_known = EntryDeadline(now, deadline);
   int dow = TimeDayOfWeek(now);
   g_spread_ok = SpreadAllowed();
   if(!schedule_known) reason = "Friday session unavailable: set FridayCloseMinutesServer from symbol specification";
   else if(now >= deadline || dow == 0 || dow == 6) reason = "weekend/close cutoff";
   else if(g_expiration_denied) reason = "broker rejected required pending expiration; entries disabled for this run";
   else if(g_uncertain_send) reason = "ambiguous send timeout; reconcile broker orders and restart with resolved state";
   else if(WrongDirectionExists()) reason = "opposite own market positions exist; use their original direction";
   else if(!g_spread_ok)
   {
      block_code = "spread";
      reason = "spread too high or unstable/warmup current=" + N(g_spread) +
         " max=" + N(MaxSpreadPips) + " range=" + N(g_spread_range) + " samples=" + IntegerToString(g_sample_count);
   }
   else if(now - (datetime)MarketInfo(Symbol(), MODE_TIME) > 30) reason = "stale symbol quote";
   if(reason != "") { Block(reason, block_code); DeleteAllPending(reason); return; }
   if(!TradeReady() || MarketInfo(Symbol(), MODE_TRADEALLOWED) == 0)
   { Block("trading unavailable"); return; }
   if(now < g_retry_entry_after) { return; }
   RefreshRates();
   // Existing orders have a wider retention window than the placement window.
   // This prevents boundary oscillations from repeatedly deleting/recreating them.
   // Only NEW placements below are subject to the broker's minimum distance.
   long first_above = ScrubberFrontier(EntryPrice(), g_tick, g_grid, 1);
   long first_below = ScrubberFrontier(EntryPrice(), g_tick, g_grid, -1);
   bool cleanup_ok = true;
   double retention_distance = (PendingGridLevels + PendingRetentionExtraLevels) * g_grid;
   for(int i = OrdersTotal() - 1; i >= 0; --i)
   {
      if(!OrderSelect(i, SELECT_BY_POS, MODE_TRADES) || !PendingSelected()) continue;
      int ticket = OrderTicket();
      long grid = SelectedGrid();
      double distance = MathAbs(OrderOpenPrice() - EntryPrice());
      string delete_reason = "";
      if(!PendingMatchesDirection(OrderType())) delete_reason = "wrong trade direction";
      else if(MathAbs(OrderOpenPrice() - grid * g_grid) > g_tick * 0.5 ||
              MathAbs(OrderLots() - g_lot_size) > 1e-8 || OrderExpiration() != deadline)
         delete_reason = "settings/expiration changed";
      else if(distance > retention_distance + g_tick * 0.5)
         delete_reason = "beyond retention distance=" + N(distance) + " limit=" + N(retention_distance);
      else if(EarlierPendingAtGrid(grid, ticket)) delete_reason = "duplicate grid level";
      if(delete_reason != "")
      {
         if(!DeletePending(ticket, delete_reason)) cleanup_ok = false;
      }
      // Even when a quote touches/crosses an order, retain its ticket near market:
      // the broker may be processing activation. Do not cancel/retype it on a tick.
   }
   if(!cleanup_ok) { Block("stale pending deletion failed; no replacement exposure"); return; }
   // Alternate nearest trend/counter-trend levels so a cap or rejection cannot
   // populate an entire side before even attempting the nearest opposite level.
   for(int n = 0; n < 2 * PendingGridLevels; ++n)
   {
      if(OwnOrderCount() >= MaxOrders) { Block("MaxOrders reached"); return; }
      int side = (n % 2 == 0 ? Direction() : -Direction());
      int depth = n / 2;
      long grid = (side > 0 ? first_above : first_below) + side * depth;
      int pending_type = PendingType(side);
      double price = NormalizeDouble(grid * g_grid, Digits);
      if(price <= 0.0 || GridOccupied(grid)) { continue; }
      RefreshRates();
      if(!SpreadAllowed()) { Block("spread changed before pending send current=" + N(g_spread), "spread"); DeleteAllPending("spread changed"); return; }
      if(ServerNow() >= deadline) { DeleteAllPending("cutoff reached during placement"); return; }
      if(side * (price - EntryPrice()) < BrokerDistance())
      { continue; }
      ResetLastError();
      double margin = AccountFreeMarginCheck(Symbol(), MarketType(), g_lot_size);
      if(margin <= 0.0 || GetLastError() == 134) { Block("insufficient free margin"); return; }
      string comment = "Scrubber|G=" + IntegerToString(grid);
      ResetLastError();
      int ticket = OrderSend(Symbol(), pending_type, g_lot_size, price, SlippagePoints(), 0.0, 0.0,
                             comment, MagicNumber, deadline, clrNONE);
      if(ticket < 0)
      {
         int err = GetLastError();
         LogAction("PENDING_SEND_FAILED type=" + PendingTypeName(pending_type) + " grid=" + IntegerToString(grid) + " price=" + N(price) + " err=" + IntegerToString(err));
         g_retry_entry_after = ServerNow() + 2;
         if(err == 147) g_expiration_denied = true;
         if(err == 128) { g_uncertain_send = true; SaveState(); }
         return; // rescan next event; no blind retries that can duplicate broker orders
      }
      if(!IsTesting()) GlobalVariableSet(g_key + ".g" + IntegerToString(ticket), (double)grid);
      LogAction("PENDING_PLACED type=" + PendingTypeName(pending_type) + " ticket=" + IntegerToString(ticket) + " grid=" + IntegerToString(grid) +
          " price=" + N(price) + " lots=" + N(g_lot_size) + " expires=" + TimeToString(deadline, TIME_DATE|TIME_SECONDS));
   }
   ClearBlock();
}

void RunPendingSafetyChecks()
{
   if(!g_ready) return;
   // Live timer and tester ticks share cleanup; this never creates entries or samples candles.
   datetime deadline = 0, now = ServerNow();
   bool known = EntryDeadline(now, deadline);
   if(!known || now >= deadline || TimeDayOfWeek(now) == 0 || TimeDayOfWeek(now) == 6)
      DeleteAllPending("close cleanup");
   else if(!SpreadAllowed()) DeleteAllPending("cleanup spread guard");
}

void OnTick()
{
   if(!g_ready) return;
   RefreshRates();
   g_last_quote_server = TimeCurrent(); g_last_quote_ms = GetTickCount();
   SampleSpread();
   // Safety cleanup is independent of harvesting, in live trading and testing.
   // MT4 tester has no Timer events; each modeled tick also runs these checks.
   RunPendingSafetyChecks();
   bool synced = SyncPositions();
   bool valued = ManagePositions();
   double tick_value = MarketInfo(Symbol(), MODE_TICKVALUE);
   if(synced && valued && tick_value > 0.0 && MathIsValidNumber(tick_value)) ManageGrid(ServerNow());
   else
   {
      Block("VALUATION_UNAVAILABLE position tracking/valuation; new entries suspended");
      DeleteAllPending("valuation unavailable");
   }
   SaveState();
   if(EnableChartComment)
   {
      int armed = 0, closing = 0;
      for(int i = 0; i < g_position_count; ++i)
      {
         if(g_positions[i].peak > 0.0) ++armed;
         if(g_positions[i].close_due) ++closing;
      }
      Comment("Scrubber | ", (Direction() > 0 ? "LONG" : "SHORT"),
         " | independent position trailing",
         "\nTracked: ", g_position_count, " | armed: ", armed, " | exit pending: ", closing,
         "\nActivation net pips: ", N(position_minimum_profit_floor),
         " | giveback percent: ", N(MaximumProfitDrawdownPercent),
         "\nPip: ", N(g_pip), " | grid: ", N(g_grid), " | spread pips: ", N(g_spread),
         "\n", g_last_block);
   }
}

void OnTimer()
{
   RunPendingSafetyChecks();
}

int OnInit()
{
   if(!SelectFixedLotSize()) return INIT_PARAMETERS_INCORRECT;
   if(!MathIsValidNumber(g_lot_size) || !MathIsValidNumber(GridStepPips) ||
      !MathIsValidNumber(PipSizeOverride) || !MathIsValidNumber(MaximumProfitDrawdownPercent) ||
      !MathIsValidNumber(ExitCommissionPerLot) || !MathIsValidNumber(MaxSpreadPips) ||
      !MathIsValidNumber(MaxSpreadJumpPips) || !MathIsValidNumber(position_minimum_profit_floor))
   { Log("INIT_FAILED non-finite input"); return INIT_PARAMETERS_INCORRECT; }
   if((Direction() != 1 && Direction() != -1) || MagicNumber <= 0 || g_lot_size <= 0.0 ||
      GridStepPips <= 0.0 || PipSizeOverride < 0.0 || PendingGridLevels < 1 || PendingGridLevels > 100 ||
      PendingRetentionExtraLevels < 0 || PendingRetentionExtraLevels > 100 ||
      MaxOrders < PendingGridLevels || MaxOrders > SCRUBBER_CAPACITY ||
      MaximumProfitDrawdownPercent <= 0.0 || MaximumProfitDrawdownPercent >= 100.0 ||
      position_minimum_profit_floor < 0.0 ||
      ExitCommissionPerLot < -1.0 || (ExitCommissionPerLot < 0.0 && ExitCommissionPerLot != -1.0) ||
      SlippagePips < 0 || MaxSpreadPips < 0.0 || MaxSpreadJumpPips < 0.0 ||
      SpreadSampleTicks < 1 || SpreadSampleTicks > 1000 ||
      FridayCloseMinutesServer < -1 || FridayCloseMinutesServer > 2880 ||
      MinutesBeforeFridayClose < 1 || MinutesBeforeFridayClose > 1440 ||
      TesterMinTrades < 0 || TesterTradeBonus < 0.0 || TesterDrawdownPenalty < 0.0 ||
      TesterMaxDrawdownMoney < 0.0 || TesterMaxDrawdownPercent < 0.0)
   { Log("INIT_FAILED invalid inputs"); return INIT_PARAMETERS_INCORRECT; }
   g_tick = SymbolInfoDouble(Symbol(), SYMBOL_TRADE_TICK_SIZE);
   g_pip = AutoPipSize();
   g_grid = GridStepPips * g_pip;
   if(g_tick <= 0.0 || g_pip <= 0.0 || g_grid < g_tick ||
      MathAbs(g_grid / g_tick - MathRound(g_grid / g_tick)) > 1e-6)
   { Log("INIT_FAILED pip/grid must be known and an exact multiple of broker tick size; use PipSizeOverride"); return INIT_PARAMETERS_INCORRECT; }
   double lot_step = MarketInfo(Symbol(), MODE_LOTSTEP);
   if(lot_step <= 0.0 || g_lot_size < MarketInfo(Symbol(), MODE_MINLOT) || g_lot_size > MarketInfo(Symbol(), MODE_MAXLOT) ||
      MathAbs(g_lot_size / lot_step - MathRound(g_lot_size / lot_step)) > 1e-6)
   { Log("INIT_FAILED " + g_lot_input + " does not match broker lot limits/step"); return INIT_PARAMETERS_INCORRECT; }
   // Include broker identity: account numbers need not be unique across servers.
   uint server_hash = 2166136261;
   string server = AccountServer();
   for(int c = 0; c < StringLen(server); ++c)
      server_hash = (server_hash ^ (uint)StringGetCharacter(server, c)) * 16777619;
   g_key = "Scrub." + IntegerToString(server_hash) + "." + IntegerToString(AccountNumber()) + "." +
           Symbol() + "." + IntegerToString(MagicNumber);
   if(StringLen(g_key) > 48) { Log("INIT_FAILED state key too long"); return INIT_FAILED; }
   g_lock_key = g_key + ".owner";
   if(!IsTesting())
   {
      // Temporary variables disappear when the terminal exits. Atomic claim prevents
      // two charts using the same symbol/magic from sending duplicate grids.
      GlobalVariableTemp(g_lock_key);
      if(!GlobalVariableSetOnCondition(g_lock_key, (double)ChartID(), 0.0))
      { Log("INIT_FAILED another chart owns this symbol/magic; use a distinct MagicNumber"); return INIT_FAILED; }
      g_owns_lock = true;
   }
   g_position_count = 0;
   RefreshRates(); SyncPositions();
   if(!IsTesting()) g_uncertain_send = GlobalVariableGet(g_key + ".uncertain") > 0.0;
   g_last_quote_server = TimeCurrent(); g_last_quote_ms = GetTickCount();
   if(IsTesting())
   {
      Log("TESTER_MODE pending cleanup runs on simulated ticks; no timer required");
   }
   else
   {
      ResetLastError();
      g_timer_started = EventSetTimer(1);
      if(!g_timer_started)
      {
         int timer_error = GetLastError();
         if(g_owns_lock) GlobalVariableSetOnCondition(g_lock_key, 0.0, (double)ChartID());
         g_owns_lock = false;
         Log("INIT_FAILED cannot start live cleanup timer err=" + IntegerToString(timer_error));
         return INIT_FAILED;
      }
   }
   g_ready = true;
   datetime deadline = 0;
   bool known = EntryDeadline(ServerNow(), deadline);
   Log("INIT exit_model=PER_POSITION pip=" + N(g_pip) + " grid_price=" + N(g_grid) + " tick=" + N(g_tick) +
       " lot_input=" + g_lot_input + " fixed_lots=" + N(g_lot_size) + " account_currency=" + AccountCurrency() +
       " position_minimum_profit_floor=" + N(position_minimum_profit_floor) +
       " Friday_cutoff=" + (known ? TimeToString(deadline, TIME_DATE|TIME_SECONDS) : "UNKNOWN: entries blocked"));
   Log("Commission reserve per lot=" + N(ExitCommissionPerLot) +
       "; -1 mirrors booked commission, 0 assumes closing commission already booked. Pending expiration is required.");
   RunPendingSafetyChecks();
   return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
   if(g_timer_started) EventKillTimer();
   g_timer_started = false;
   if(g_ready)
   {
      SaveState();
      DeleteAllPending("EA deinitialization");
      if(!IsTesting()) GlobalVariablesFlush();
   }
   if(EnableChartComment) Comment("");
   if(g_owns_lock) GlobalVariableSetOnCondition(g_lock_key, 0.0, (double)ChartID());
   g_owns_lock = false;
}

double OnTester()
{
   double profit = TesterStatistics(STAT_PROFIT);
   double trades = TesterStatistics(STAT_TRADES);
   double equity_dd = TesterStatistics(STAT_EQUITY_DD);
   if(!TesterUseCustomScore) return profit;
   if(profit < TesterMinProfit || trades < TesterMinTrades ||
      (TesterUseMaxDrawdownFilter && equity_dd > TesterMaxDrawdownMoney) ||
      (TesterUseMaxDrawdownPercentFilter && TesterStatistics(STAT_EQUITY_DDREL_PERCENT) > TesterMaxDrawdownPercent))
   { Log("TESTER_REJECT profit=" + N(profit) + " trades=" + N(trades) + " equity_dd=" + N(equity_dd)); return -1000000000.0; }
   double score = profit + TesterTradeBonus * trades - TesterDrawdownPenalty * equity_dd;
   Log("TESTER_SCORE profit=" + N(profit) + " trades=" + N(trades) + " equity_dd=" + N(equity_dd) + " score=" + N(score));
   return score;
}
