// Additional candle APIs for the existing deterministic MT4 broker shim.
#pragma once
#define OrderModify BaseOrderModify
#define OrderClose BaseOrderClose
#include "../scrubber/mt4_shim.hpp"
#undef OrderModify
#undef OrderClose
enum { PERIOD_M1 = 1 };
struct Candle {
 datetime time; double open, close, high, low;
 Candle(datetime t,double o,double c,double h=std::nan(""),double l=std::nan("")):
  time(t),open(o),close(c),high(std::isnan(h)?std::max(o,c)+.5:h),low(std::isnan(l)?std::min(o,c)-.5:l) {}
};
std::vector<Candle> candles; // chronological; final record is the forming bar
std::map<int, datetime> opened_at;
int history_reads = 0;
bool history_failure = false;
std::vector<string> trade_events;
int modify_attempts=0,close_attempts=0;
double quote_after_modify=std::nan("");
bool broker_stop_after_modify=false;
bool OrderModify(int ticket,double price,double sl,double tp,datetime expiration,int color) {
 ++modify_attempts;trade_events.push_back("modify:"+std::to_string(ticket));
 if(!OrderSelect(ticket,SELECT_BY_TICKET))return false;
 double distance=(sel().type==OP_BUY?Bid-sl:sl-Ask);
 if(sl>0 && (distance<min_stop_points*Point || distance<=freeze_points*Point)) {
  last_error=130;return false;
 }
 bool result=BaseOrderModify(ticket,price,sl,tp,expiration,color);
 if(std::isfinite(quote_after_modify)) {Bid=Ask=quote_after_modify;quote_after_modify=std::nan("");}
 if(result && broker_stop_after_modify && !sel().closed) {
  sel().realized=OrderProfit();sel().closed=clock_now;
 }
 return result;
}
bool OrderClose(int ticket,double lots,double price,int slippage,int color) {
 ++close_attempts;trade_events.push_back("close:"+std::to_string(ticket));
 return BaseOrderClose(ticket,lots,price,slippage,color);
}
double MathSqrt(double x) { return std::sqrt(x); }
string StringSubstr(const string &s, int n, int len) { return s.substr(n, len); }
int iBars(string, int tf) {
 if(tf != PERIOD_M1) throw std::runtime_error("CandleTaker must use M1");
 return int(candles.size());
}
Candle candle_at(int shift) {
 ++history_reads;
 if(history_failure || shift < 0 || shift >= int(candles.size())) {
  last_error=4066; return {0,0,0};
 }
 return candles[candles.size()-1-shift];
}
datetime iTime(string, int tf, int shift) { iBars("",tf); return candle_at(shift).time; }
double iOpen(string, int tf, int shift) { iBars("",tf); return candle_at(shift).open; }
double iClose(string, int tf, int shift) { iBars("",tf); return candle_at(shift).close; }
double iHigh(string, int tf, int shift) { iBars("",tf); return candle_at(shift).high; }
double iLow(string, int tf, int shift) { iBars("",tf); return candle_at(shift).low; }
int iBarShift(string, int tf, datetime time, bool exact) {
 iBars("",tf);
 for(int shift=0;shift<int(candles.size());++shift) {
  const auto &b=candles[candles.size()-1-shift];
  if(time>=b.time && time<b.time+60) return shift;
 }
 return exact ? -1 : int(candles.size())-1;
}
datetime OrderOpenTime() {
 auto it=opened_at.find(OrderTicket());
 return it==opened_at.end() ? clock_now : it->second;
}
