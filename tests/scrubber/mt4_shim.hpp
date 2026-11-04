// Deterministic broker simulator for executing the actual EA event handlers in C++.
// This does not replace compilation in MetaEditor or broker integration testing.
#pragma once
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using string = std::string;
using datetime = long long;
using uint = unsigned int;
#define input
enum { OP_BUY, OP_SELL, OP_BUYLIMIT, OP_SELLLIMIT, OP_BUYSTOP, OP_SELLSTOP };
enum { MODE_PROFITCALCMODE, MODE_STOPLEVEL, MODE_FREEZELEVEL, MODE_TICKVALUE,
       MODE_TIME, MODE_TRADEALLOWED, MODE_LOTSTEP, MODE_MINLOT, MODE_MAXLOT };
enum { SYMBOL_TRADE_TICK_SIZE, SYMBOL_CURRENCY_BASE };
enum { STAT_PROFIT, STAT_TRADES, STAT_EQUITY_DD, STAT_EQUITY_DDREL_PERCENT };
enum { SELECT_BY_TICKET, SELECT_BY_POS, MODE_TRADES, FRIDAY = 5 };
enum { clrNONE, TIME_DATE = 1, TIME_SECONDS = 2, INIT_SUCCEEDED = 0, INIT_FAILED = 1,
       INIT_PARAMETERS_INCORRECT = 2 };
double Bid = 100, Ask = 100, Point = .01;
int Digits = 2;
string symbol_name = "US100", base_currency = "USD", account_currency = "USD";
datetime clock_now = 1793707200; // overwritten by tests
datetime quote_time = 0;
uint monotonic_ms = 0;
bool testing = true, connected = true, trading = true, busy = false;
bool session_available = true, expiration_allowed = true, timer_available = true;
int timer_start_calls = 0, timer_kill_calls = 0;
int session_start = 3600, session_finish = 23 * 3600 + 55 * 60;
int next_ticket = 100, selected = -1, last_error = 0;
int fail_close = 0, fail_delete = 0, fail_modify = 0, fail_send = 0;
bool ambiguous_accept = false;
int sends = 0, closes = 0, deletes = 0, modifies = 0;
double tick_size = .01, tick_value = .01, min_stop_points = 0, freeze_points = 0;
double reported_tick_value = -1; // metadata outage independent of broker-valued OrderProfit
double lot_step = .01, min_lot = .01, max_lot = 100, free_margin = 100000;
int profit_mode = 1;
std::map<string, double> globals;
std::vector<string> logs;
std::map<int, double> stats;
struct Order {
 int ticket, type, magic; string symbol, comment;
 double lots, open, sl = 0, tp = 0, commission = 0, swap = 0, realized = 0;
 datetime expiration = 0, closed = 0;
};
std::vector<Order> orders;
Order &sel() { return orders.at(selected); }
template<class A, class B> double MathMax(A a, B b) { return std::max(double(a), double(b)); }
template<class A, class B> double MathMin(A a, B b) { return std::min(double(a), double(b)); }
double MathAbs(double x) { return std::abs(x); }
double MathCeil(double x) { return std::ceil(x); }
double MathFloor(double x) { return std::floor(x); }
double MathRound(double x) { return std::round(x); }
bool MathIsValidNumber(double x) { return std::isfinite(x); }
double NormalizeDouble(double x, int digits) { double s = std::pow(10., digits); return std::round(x*s)/s; }
string DoubleToString(double x, int n) { std::ostringstream s; s << std::fixed << std::setprecision(n) << x; return s.str(); }
string IntegerToString(long long x) { return std::to_string(x); }
int StringFind(const string &s, const string &p) { auto n=s.find(p); return n==string::npos ? -1 : int(n); }
string StringSubstr(const string &s, int n) { return s.substr(n); }
long long StringToInteger(const string &s) { try { return std::stoll(s); } catch(...) { return 0; } }
int StringLen(const string &s) { return int(s.size()); }
void StringToUpper(string &s) { for(char &c:s) c=char(std::toupper(c)); }
template<class... T> void Print(T... v) { std::ostringstream s; (s << ... << v); logs.push_back(s.str()); }
template<class... T> void Comment(T...) {}
string Symbol() { return symbol_name; }
string AccountCurrency() { return account_currency; }
string AccountServer() { return "TestBroker"; }
long ChartID() { return 456; }
int StringGetCharacter(const string &s, int i) { return s.at(i); }
int AccountNumber() { return 1234; }
datetime TimeCurrent() { return clock_now; }
uint GetTickCount() { return monotonic_ms; }
std::tm tmof(datetime d) { time_t t=d; return *gmtime(&t); }
int TimeDayOfWeek(datetime d) { return tmof(d).tm_wday; }
int TimeHour(datetime d) { return tmof(d).tm_hour; }
int TimeMinute(datetime d) { return tmof(d).tm_min; }
int TimeSeconds(datetime d) { return tmof(d).tm_sec; }
string TimeToString(datetime d, int) { return std::to_string(d); }
bool IsTesting() { return testing; }
bool IsConnected() { return connected; }
bool IsTradeAllowed() { return trading; }
bool IsTradeContextBusy() { return busy; }
bool RefreshRates() { return true; }
void ResetLastError() { last_error=0; }
int GetLastError() { int e=last_error; last_error=0; return e; }
bool EventSetTimer(int) {
 ++timer_start_calls;
 // Reproduce the reported MT4 tester failure; never model a working tester timer.
 if(testing || !timer_available) { last_error=4059; return false; }
 return true;
}
void EventKillTimer() { ++timer_kill_calls; }
double SymbolInfoDouble(string, int) { return tick_size; }
string SymbolInfoString(string, int) { return base_currency; }
bool SymbolInfoSessionTrade(string, int, uint idx, datetime &from, datetime &to) {
 if(!session_available || idx) return false;
 from=session_start; to=session_finish; return true;
}
double MarketInfo(string, int mode) {
 switch(mode) {
 case MODE_PROFITCALCMODE:return profit_mode;
 case MODE_STOPLEVEL:return min_stop_points;
 case MODE_FREEZELEVEL:return freeze_points;
 case MODE_TICKVALUE:return reported_tick_value>=0 ? reported_tick_value:tick_value;
 case MODE_TIME:return double(quote_time);
 case MODE_TRADEALLOWED:return trading;
 case MODE_LOTSTEP:return lot_step;
 case MODE_MINLOT:return min_lot;
 case MODE_MAXLOT:return max_lot;
 default:throw std::runtime_error("unknown MarketInfo");
 }
}
bool GlobalVariableCheck(string k) { return globals.count(k); }
double GlobalVariableGet(string k) { return globals[k]; }
void GlobalVariableSet(string k, double v) { globals[k]=v; }
bool GlobalVariableDel(string k) { return globals.erase(k)>0; }
void GlobalVariableTemp(string k) { if(!globals.count(k)) globals[k]=0; }
bool GlobalVariableSetOnCondition(string k, double v, double expected) {
 if(!globals.count(k) || globals[k]!=expected) return false;
 globals[k]=v;return true;
}
void GlobalVariablesFlush() {}
int OrdersTotal() { int n=0; for(auto &o:orders) if(!o.closed) ++n; return n; }
bool OrderSelect(int id, int by, int=0) {
 int n=0;
 for(size_t i=0;i<orders.size();++i) {
  if((by==SELECT_BY_TICKET && orders[i].ticket==id) ||
     (by==SELECT_BY_POS && !orders[i].closed && n++==id)) { selected=int(i); return true; }
 }
 return false;
}
string OrderSymbol() { return sel().symbol; }
int OrderMagicNumber() { return sel().magic; }
int OrderType() { return sel().type; }
int OrderTicket() { return sel().ticket; }
datetime OrderCloseTime() { return sel().closed; }
double OrderLots() { return sel().lots; }
double OrderOpenPrice() { return sel().open; }
double OrderStopLoss() { return sel().sl; }
double OrderTakeProfit() { return sel().tp; }
double OrderCommission() { return sel().commission; }
double OrderSwap() { return sel().swap; }
string OrderComment() { return sel().comment; }
datetime OrderExpiration() { return sel().expiration; }
double OrderProfit() {
 if(sel().closed) return sel().realized;
 if(sel().type>OP_SELL) return 0;
 double px=sel().type==OP_BUY ? Bid:Ask;
 return (sel().type==OP_BUY ? px-sel().open:sel().open-px)*tick_value/tick_size*sel().lots;
}
double AccountFreeMarginCheck(string, int, double) { return free_margin; }
int OrderSend(string symbol, int type, double lots, double price, int, double sl, double tp,
              string comment, int magic, datetime exp, int) {
 ++sends;
 if(!expiration_allowed && exp) { last_error=147; return -1; }
 int err=fail_send; fail_send=0;
 if(err && !ambiguous_accept) { last_error=err; return -1; }
 if(std::abs(price/tick_size-std::round(price/tick_size))>1e-6) { last_error=129; return -1; }
 if(type<OP_BUYLIMIT || type>OP_SELLSTOP) throw std::runtime_error("market entry forbidden");
 double reference=(type==OP_BUYSTOP || type==OP_BUYLIMIT) ? Ask:Bid;
 int side=(type==OP_BUYSTOP || type==OP_SELLLIMIT) ? 1:-1;
 if(side*(price-reference)<std::max(min_stop_points,freeze_points)*Point) { last_error=130; return -1; }
 orders.push_back({next_ticket++,type,magic,symbol,comment,lots,price,sl,tp,0,0,0,exp,0});
 if(err) { last_error=err; return -1; }
 return orders.back().ticket;
}
bool OrderDelete(int ticket, int) {
 if(!OrderSelect(ticket,SELECT_BY_TICKET)) return false;
 if(fail_delete) { --fail_delete; last_error=145; return false; }
 if(sel().type<=OP_SELL || sel().closed) { last_error=4108; return false; }
 sel().closed=clock_now; ++deletes; return true;
}
bool OrderClose(int ticket, double lots, double, int, int) {
 if(!OrderSelect(ticket,SELECT_BY_TICKET)) return false;
 if(fail_close) { --fail_close; last_error=138; return false; }
 if(sel().type>OP_SELL || sel().closed || lots!=sel().lots) return false;
 sel().realized=OrderProfit(); sel().closed=clock_now; ++closes; return true;
}
bool OrderModify(int ticket, double, double sl, double, datetime, int) {
 if(!OrderSelect(ticket,SELECT_BY_TICKET)) return false;
 if(fail_modify) { --fail_modify; last_error=130; return false; }
 if(sel().closed) return false;
 sel().sl=sl; ++modifies; return true;
}
double TesterStatistics(int id) { return stats[id]; }
