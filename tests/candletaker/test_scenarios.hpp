int checks=0;
void check(bool yes, string why) { ++checks; if(!yes) throw std::runtime_error(why); }
void near(double x,double y,double tol,string why) { check(std::abs(x-y)<=tol,why+" got="+N(x)+" expected="+N(y)); }
datetime date(int year,int month,int day,int hour=0,int minute=0,int second=0) {
 std::tm t={};t.tm_year=year-1900;t.tm_mon=month-1;t.tm_mday=day;t.tm_hour=hour;t.tm_min=minute;t.tm_sec=second;
 return timegm(&t);
}
void reset() {
 orders.clear();logs.clear();globals.clear();stats.clear();selected=-1;next_ticket=100;
 Bid=100;Ask=100;Point=.01;Digits=2;tick_size=.01;tick_value=.01;
 reported_tick_value=-1;
 symbol_name="US100";base_currency="USD";account_currency="USD";profit_mode=1;
 clock_now=date(2026,11,4,12);quote_time=clock_now;monotonic_ms=0;
 testing=true;connected=true;trading=true;busy=false;session_available=true;expiration_allowed=true;timer_available=true;
 timer_start_calls=timer_kill_calls=0;
 session_start=3600;session_finish=23*3600+55*60;
 fail_close=fail_delete=fail_modify=fail_send=0;ambiguous_accept=false;
 sends=closes=deletes=modifies=0;min_stop_points=freeze_points=0;
 lot_step=.01;min_lot=.01;max_lot=100;free_margin=100000;
 TradeDirection=LONG_ONLY;LotSizeFX=LotSizeNAS100=LotSizeGold=1;GridStepPips=10;PipSizeOverride=0;PendingGridLevels=3;PendingRetentionExtraLevels=3;MaxOrders=200;
 MagicNumber=779002;minimum_candles_in_sequence=2;DojiBodyPips=1;ExitCommissionPerLot=0;
 position_minimum_profit_floor=0;
 SlippagePips=20;UseSpreadFilter=true;MaxSpreadPips=40;UseSpreadJumpFilter=false;SpreadSampleTicks=10;MaxSpreadJumpPips=20;
 FridayCloseMinutesServer=-1;MinutesBeforeFridayClose=20;SpecialCloseServerTime=0;
 TesterUseCustomScore=true;TesterMinTrades=3000;TesterMinProfit=1000;TesterTradeBonus=3;TesterDrawdownPenalty=.5;
 TesterUseMaxDrawdownFilter=true;TesterMaxDrawdownMoney=25000;TesterUseMaxDrawdownPercentFilter=false;TesterMaxDrawdownPercent=25;
 EnableDebugPrints=true;EnableChartComment=true;
 g_pip=g_tick=g_grid=g_spread=g_spread_range=g_lot_size=0;g_lot_input="";
 g_sample_count=g_sample_pos=g_position_count=0;g_spread_ok=g_ready=false;
 g_timer_started=false;
 g_expiration_denied=g_uncertain_send=false;g_retry_entry_after=g_last_quote_server=0;
 g_key=g_last_block=g_block_code=g_lock_key="";g_owns_lock=false;
 candles.clear();opened_at.clear();history_reads=0;history_failure=false;
 g_context_bar=0;g_history_ready=false;g_last_candle_kind=CANDLE_DOJI;
 trade_events.clear();modify_attempts=close_attempts=0;quote_after_modify=std::nan("");broker_stop_after_modify=false;
 // Complete single-candle favorable runs in the default historical window.
 for(int n=240;n>=0;--n) candles.push_back({clock_now-n*60,100,n%2?98.:102.});
}
int add(int type,double open,double lots=1,double commission=0,double swap=0,int magic=779002,string sym="US100") {
 int ticket=next_ticket++;
 orders.push_back({ticket,type,magic,sym,"",lots,open,0,0,commission,swap});
 opened_at[ticket]=clock_now;
 return ticket;
}
bool live(int ticket) { return OrderSelect(ticket,SELECT_BY_TICKET) && OrderCloseTime()==0; }
void tick(double bid,double ask) { Bid=bid;Ask=ask;quote_time=clock_now;OnTick(); }
int pending_count() { int n=0;for(auto &o:orders)if(!o.closed && o.type>OP_SELL && o.magic==MagicNumber)++n;return n; }
void init() { check(OnInit()==INIT_SUCCEEDED,"init succeeds"); }
bool logged(string word) { for(auto &s:logs)if(s.find(word)!=string::npos)return true;return false; }

void finish(double open,double close,double high=std::nan(""),double low=std::nan("")) {
 candles.back()=Candle(candles.back().time,open,close,high,low);
 clock_now=candles.back().time+60;
 candles.push_back({clock_now,close,close});
 tick(close,close);
}
void append_without_tick(double open,double close) {
 candles.back()=Candle(candles.back().time,open,close);
 clock_now=candles.back().time+60;candles.push_back({clock_now,close,close});
}
CandleTakerBar bar(double o,double h,double l,double c) {return {clock_now,o,h,l,c};}
void bare_position() {g_position_count=1;g_positions[0]={};g_positions[0].ticket=1;ResetSequences(0);}
bool step(int kind,int &completed) {return AdvanceSequence(0,kind,completed);}
void basic_run() {finish(100,102);finish(102,104);}
void assert_stop_before_close(int ticket) {
 string modify="modify:"+std::to_string(ticket),close="close:"+std::to_string(ticket);
 auto m=std::find(trade_events.begin(),trade_events.end(),modify);
 auto c=std::find(trade_events.begin(),trade_events.end(),close);
 check(m!=trade_events.end() && c!=trade_events.end() && m<c,"OrderModify before OrderClose");
}

void classification_tests() {
 reset();init();
 auto previous=bar(100,111,99,110);
 check(ClassifyCandle(bar(108,109,102,104),previous)==CANDLE_INSIDE,"whole wick range inside prior body ignored");
 check(ClassifyCandle(bar(108,110,100,104),previous)==CANDLE_INSIDE,"inside boundary equality accepted");
 check(ClassifyCandle(bar(108,110.5,102,104),previous)==CANDLE_BEARISH,"close inside but upper wick outside body counts");
 check(ClassifyCandle(bar(108,109,99.5,104),previous)==CANDLE_BEARISH,"close inside but lower wick outside body counts");
 check(ClassifyCandle(bar(108,112,98,104),previous)==CANDLE_OUTSIDE,"outside determined by both wicks");
 check(ClassifyCandle(bar(102,112,98,108),previous)==CANDLE_OUTSIDE,"favorable outside also ignored");
 check(ClassifyCandle(bar(108,111,98,104),previous)==CANDLE_BEARISH,"equal high not strict outside");
 check(ClassifyCandle(bar(108,112,99,104),previous)==CANDLE_BEARISH,"equal low not strict outside");
 check(ClassifyCandle(bar(110,112,109,111),previous)==CANDLE_DOJI,"exactly one pip doji");
 check(ClassifyCandle(bar(110,112,109,111.01),previous)==CANDLE_BULLISH,"body above one pip not doji");
 // Immediate predecessor, even when ignored, is always the filter reference.
 auto outside=bar(110,114,98,102);
 check(ClassifyCandle(outside,previous)==CANDLE_OUTSIDE,"preceding outside setup");
 auto next=bar(111,111.5,108.5,109);
 check(ClassifyCandle(next,outside)==CANDLE_BEARISH,"next candle can decide after outside");
 // Mirrors preserve classifications and reverse ordinary candle direction.
 auto mirror=[](CandleTakerBar b){return bar(300-b.open,300-b.low,300-b.high,300-b.close);};
 for(auto c:{bar(108,109,102,104),bar(108,112,98,104),bar(110,112,109,111),bar(108,110.5,102,104)}) {
  int k=ClassifyCandle(c,previous),rev=ClassifyCandle(mirror(c),mirror(previous));
  check(rev==(k==1||k==-1?-k:k),"classification symmetric for long and short");
 }
 CandleTakerBar c;
 check(ReadCandle(1,c),"valid OHLC read");
 candles[candles.size()-2].high=0;check(!ReadCandle(1,c),"invalid high fails history read");
 reset();init();candles.erase(candles.begin(),candles.end()-3);
 check(UpdateCandleContext(),"only two completed candles needed; no 240-bar dependency");
 int reads=history_reads;check(UpdateCandleContext(),"cached context");
 check(history_reads==reads+1,"no historical scan on every tick");
 candles.back().close=10000;check(UpdateCandleContext(),"forming candle ignored");
 reset();init();candles.erase(candles.begin(),candles.end()-2);tick(100,100);
 check(sends==0,"missing prior closed candle blocks entries");
 reset();for(auto &b:candles)b=Candle(b.time,100,100);init();tick(100,100);
 check(sends==6,"all-doji history no longer requires statistical samples");
}

void sequence_tests() {
 for(auto direction:{LONG_ONLY,SHORT_ONLY}) {
  reset();TradeDirection=direction;init();bare_position();int done=0,d=Direction();
  check(!step(d,done)&&g_positions[0].sequence_count==1,"first favorable count");
  check(!step(-d,done)&&done==1&&g_positions[0].sequence_count==0,"short run discarded on counter");
  check(!step(d,done)&&!step(-d,done),"two separate one-candle runs never accumulate");
  check(!step(d,done)&&!step(d,done),"reaching minimum does not close");
  for(int ignored:{CANDLE_DOJI,CANDLE_INSIDE,CANDLE_OUTSIDE}) {
   check(!step(ignored,done),"ignored candle no signal");
   check(g_positions[0].sequence_count==2,"ignored candle preserves count");
  }
  check(!step(d,done)&&g_positions[0].sequence_count==3,"favorable continuation beyond minimum");
  check(step(-d,done)&&done==3,"first ordinary counter triggers eligible run");
  check(g_positions[0].sequence_count==0,"run reset after counter");
  check(!step(-d,done)&&done==0,"consecutive counters cannot repeat signal");
  minimum_candles_in_sequence=1;step(d,done);check(step(-d,done),"minimum one supported");
 }
}

void execution_tests() {
 for(auto direction:{LONG_ONLY,SHORT_ONLY}) {
  reset();TradeDirection=direction;int d=Direction();
  int a=add(MarketType(),100-d*10),loser=add(MarketType(),100+d*30);
  int manual=add(MarketType(),100-d*10,1,0,0,0),scrubber=add(MarketType(),100-d*10,1,0,0,779001);
  init();tick(100,100);auto px=[&](double x){return 100+d*x;};
  finish(px(0),px(2));finish(px(2),px(4));
  check(live(a)&&modify_attempts==0,"no close/protection during favorable sequence");
  check(g_positions[PositionIndex(a)].sequence_count==2,"independent run reaches two");
  int before_deletes=deletes;finish(px(4),px(2));
  check(!live(a),"counter closes qualified winner");assert_stop_before_close(a);
  check(live(loser)&&live(manual)&&live(scrubber),"losers and foreign orders untouched");
  check(deletes==before_deletes,"profit exit keeps pending grid");
  check(logged("BREAK_EVEN_SET")&&logged("exit_sequence_count=2"),"action diagnostics record exit reason and protection");
 }
 for(auto direction:{LONG_ONLY,SHORT_ONLY}) for(double lots:{.01,.1,1.,10.}) {
  reset();TradeDirection=direction;position_minimum_profit_floor=5;ExitCommissionPerLot=-1;
  int d=Direction(),a=add(MarketType(),100-d*10,lots,-2*lots,-lots);init();
  tick(100-d,100-d);check(!g_positions[PositionIndex(a)].armed,"cost-inclusive floor below five");
  tick(100,100);check(g_positions[PositionIndex(a)].armed,"same net-pip activation across lots");
  finish(100,100+d*2);finish(100+d*2,100+d*4);finish(100+d*4,100+d*2);
  check(!live(a),"same candle exit across lot sizes");assert_stop_before_close(a);
  OrderSelect(a,SELECT_BY_TICKET);near(OrderStopLoss(),100-d*5,.011,"break-even covers commission, swap, exit reserve");
 }
 reset();position_minimum_profit_floor=15;int a=add(OP_BUY,90);init();tick(100,100);
 check(!g_positions[PositionIndex(a)].armed,"below activation retained");tick(106,106);
 basic_run();finish(104,102);check(!live(a),"activation permanent even below original floor");
 reset();position_minimum_profit_floor=50;a=add(OP_BUY,90);init();tick(100,100);basic_run();finish(104,102);
 check(live(a)&&modify_attempts==0,"unarmed winner not protected/closed");
 tick(150,150);check(live(a),"later arming cannot reuse old counter signal");
 reset();a=add(OP_BUY,90);init();tick(100,100);tick(200,200);tick(91,91);
 check(live(a)&&modify_attempts==0,"no profit drawdown trailing exit");
 basic_run();finish(104,98);check(!live(a),"counter below sequence origin still exits positive position; old reset removed");
 reset();a=add(OP_BUY,90);init();tick(100,100);finish(100,102);int b=add(OP_BUY,95);tick(102,102);
 finish(102,104);finish(104,102);check(!live(a)&&live(b),"position counts depend on own fill time");
 check(g_positions[PositionIndex(b)].sequence_count==0,"newer insufficient run reset");
 reset();clock_now+=30;a=add(OP_BUY,90);init();tick(100,100);finish(100,102);
 check(g_positions[PositionIndex(a)].sequence_count==0,"partial entry candle excluded");
 finish(102,104);check(g_positions[PositionIndex(a)].sequence_count==1,"first whole candle counted");
 // Ignored counter bars must preserve the eligible sequence until ordinary reversal.
 reset();a=add(OP_BUY,90);init();tick(100,100);finish(100,104);finish(104,110);
 finish(109,106,109.5,105.5);check(live(a)&&g_positions[PositionIndex(a)].sequence_count==2,"inside counter ignored");
 finish(106,108,111,104);check(live(a)&&g_positions[PositionIndex(a)].sequence_count==2,"outside favorable ignored");
 finish(108,107,109,106);check(live(a),"doji counter ignored");
 finish(107,104,108,103);check(!live(a),"next ordinary counter decides exit");
 // Ignore only if BOTH wicks fit the preceding BODY, not merely if close fits.
 reset();a=add(OP_BUY,90);init();tick(100,100);finish(100,104);finish(104,110);
 finish(109,106,111,105);check(!live(a),"wick outside body defeats inside filter");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();MaxSpreadPips=1;
 append_without_tick(104,102);tick(102,105);check(!live(a),"spread guard cannot block profit exit");
 reset();ExitCommissionPerLot=-1;a=add(OP_BUY,90,1,-2,-1);init();tick(100,100);basic_run();
 append_without_tick(104,102);tick(94,94);check(live(a)&&!g_positions[PositionIndex(a)].close_due,"gap to cost-negative prevents signal latch");
 tick(100,100);check(live(a),"profit recovery needs new candle signal");
 reset();init();tick(100,100);size_t lines=logs.size();int before=sends;
 for(int n=0;n<1000;++n)tick(100,100);
 check(logs.size()==lines&&sends==before&&modify_attempts==0,"idle tick logging and request spam absent");
}

void protection_tests() {
 // Validate the actual broker-metadata wiring for account-base FX and CFDs,
 // in addition to the independent scalar calculation tests below.
 for(auto direction:{LONG_ONLY,SHORT_ONLY}) {
  reset();TradeDirection=direction;symbol_name="USDCHF";profit_mode=0;
  Point=tick_size=.00001;Digits=5;Bid=Ask=.9;tick_value=1./.9;
  double entry=Direction()>0?.89:.91;
  int ticket=add(MarketType(),entry,.01,-.01,-.02,779002,"USDCHF");ExitCommissionPerLot=-1;
  init();OrderSelect(ticket,SELECT_BY_TICKET);ProtectBeforeClose(PositionIndex(ticket));
  check(modifies==1,"inverse FX broker metadata installs protection");
  double stop=OrderStopLoss();
  double actual=Direction()*(stop-entry)*1000/stop-.04;
  check(actual>=-1e-8&&actual<.03,"USD-base FX stop covers costs under stop-price conversion");
 }
 for(string sym:{"US100","XAUUSD"}) {
  reset();symbol_name=sym;double quote=sym=="US100"?28500:4042;
  double slope=sym=="US100"?1:100;tick_value=slope*tick_size;Bid=Ask=quote;
  int ticket=add(OP_BUY,quote-100,1,-2,-1,779002,sym);init();
  OrderSelect(ticket,SELECT_BY_TICKET);ProtectBeforeClose(PositionIndex(ticket));
  check(modifies==1,"CFD protective stop installed");
  near(OrderStopLoss(),quote-100+3/slope,.011,"Nasdaq/gold contract-value break-even");
 }
 reset();int a=add(OP_BUY,90);init();tick(100,100);basic_run();fail_modify=1;finish(104,102);
 check(!live(a)&&logged("BREAK_EVEN_FAILED"),"failed protection never delays close");assert_stop_before_close(a);
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();min_stop_points=2000;finish(104,102);
 check(!live(a)&&modify_attempts==0&&close_attempts==1,"invalid stop distance falls through to market exit");
 check(logged("BREAK_EVEN_DEFERRED"),"stop restriction logged");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();freeze_points=2000;finish(104,102);
 check(!live(a)&&modify_attempts==0,"freeze constraint cannot prevent close attempt");
 reset();a=add(OP_BUY,90);OrderSelect(a,SELECT_BY_TICKET);sel().sl=99;init();tick(100,100);basic_run();finish(104,102);
 check(!live(a)&&modify_attempts==0,"stronger existing SL not loosened");OrderSelect(a,SELECT_BY_TICKET);near(OrderStopLoss(),99,1e-9,"old stronger SL preserved");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();fail_close=1;finish(104,102);
 check(live(a)&&g_positions[PositionIndex(a)].close_due,"failed close pending with backup SL");
 OrderSelect(a,SELECT_BY_TICKET);near(OrderStopLoss(),90,.011,"backup net break-even survives rejection");
 tick(102,102);check(close_attempts==1&&modify_attempts==1,"backoff prevents repeated broker requests");
 clock_now+=2;tick(102,102);check(!live(a)&&modify_attempts==1&&close_attempts==2,"retry avoids duplicate unchanged modify");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();fail_close=1;fail_modify=1;finish(104,102);
 clock_now+=2;tick(89,89);check(live(a)&&close_attempts==1,"retry never sends loss-making market close");
 tick(92,92);check(!live(a)&&modify_attempts==2,"retry reattempts protection and exits once profitable");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();quote_after_modify=89;finish(104,102);
 check(live(a)&&close_attempts==0&&g_positions[PositionIndex(a)].close_due,"fresh net check after modify round trip");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();broker_stop_after_modify=true;quote_after_modify=90;finish(104,102);
 check(!live(a)&&close_attempts==0&&PositionIndex(a)<0,"broker SL executing during modify cannot cause double close");
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();reported_tick_value=0;finish(104,102);
 check(!live(a)&&modify_attempts==0&&logged("BREAK_EVEN_UNAVAILABLE"),"armed cash exit survives unavailable tick value");
 // New rollover costs on a rejected exit must tighten existing break-even protection.
 reset();a=add(OP_BUY,90);init();tick(100,100);basic_run();fail_close=2;finish(104,102);
 OrderSelect(a,SELECT_BY_TICKET);sel().swap=-2;clock_now+=2;tick(102,102);
 OrderSelect(a,SELECT_BY_TICKET);near(OrderStopLoss(),92,.011,"retry accounts for changed negative rollover");
 check(modify_attempts==2,"new cost requires one tighter modify");
}
void grid_tests() {
 reset();init();tick(100,100);check(pending_count()==6,"three grid levels per side");
 for(auto &o:orders) {
  check((o.type==OP_BUYSTOP&&o.open>Ask)||(o.type==OP_BUYLIMIT&&o.open<Ask),"long pending sides");
  check(o.expiration==date(2026,11,6,23,35),"Friday expiration retained");
  OrderSelect(o.ticket,SELECT_BY_TICKET);check(SelectedGrid()==std::lround(o.open/g_grid),"new comment prefix decoded");
 }
 tick(80,80);check(pending_count()==9&&deletes==0,"retention prevents frontier churn");
 reset();TradeDirection=SHORT_ONLY;init();tick(100,100);
 for(auto &o:orders)check((o.type==OP_SELLSTOP&&o.open<Bid)||(o.type==OP_SELLLIMIT&&o.open>Bid),"short pending sides");
 reset();MaxOrders=3;init();tick(100,100);check(OwnOrderCount()==3,"order cap retained");
 reset();MaxSpreadPips=1;init();tick(100,102);check(sends==0,"spread blocks entry");
 tick(100,100);check(pending_count()==6,"spread recovery");
 tick(100,102);check(pending_count()==0,"spread removes pending exposure");
 reset();UseSpreadJumpFilter=true;SpreadSampleTicks=3;init();tick(100,100);tick(100,100);
 check(sends==0,"spread sample warmup");tick(100,100);check(sends==6,"spread sample completion");
 reset();expiration_allowed=false;init();tick(100,100);tick(100,100);
 check(sends==1&&pending_count()==0,"required expiration enforced");
 reset();fail_send=128;ambiguous_accept=true;init();tick(100,100);tick(100,100);
 check(sends==1&&pending_count()==0,"ambiguous send never duplicated");
 reset();init();tick(100,100);fail_delete=1;tick(300,300);
 check(sends==6,"delete failure blocks replacement");
 reset();fail_send=130;init();tick(100,100);tick(100,100);check(sends==1,"entry retry backoff");
 clock_now+=2;tick(100,100);check(pending_count()==6,"entry retry recovery");
 reset();init();tick(100,100);Order dup=orders[0];dup.ticket=next_ticket++;orders.push_back(dup);
 tick(100,100);check(pending_count()==6&&!live(dup.ticket),"duplicate pending cleanup");
 reset();session_available=false;init();tick(100,100);check(sends==0,"unknown Friday schedule blocks entry");
 FridayCloseMinutesServer=1395;tick(100,100);check(sends==6,"manual Friday override");
 reset();init();tick(100,100);clock_now=date(2026,11,6,23,35);OnTimer();
 check(pending_count()==0,"Friday timer cleanup");tick(100,100);check(sends==6,"Friday rearming forbidden");
 clock_now=date(2026,11,8,20);tick(100,100);check(sends==6,"Sunday blackout");
 reset();testing=false;init();tick(100,100);check(timer_start_calls==1,"live timer registered");OnDeinit(0);
 check(pending_count()==0&&timer_kill_calls==1,"deinit cancels pending and timer");
 reset();init();check(timer_start_calls==0,"tester requires no timer");
 reset();testing=false;timer_available=false;check(OnInit()==INIT_FAILED,"live timer failure aborts");
 reset();minimum_candles_in_sequence=0;check(OnInit()==INIT_PARAMETERS_INCORRECT,"invalid minimum sequence");
 reset();DojiBodyPips=-1;check(OnInit()==INIT_PARAMETERS_INCORRECT,"invalid doji");
 reset();DojiBodyPips=std::nan("");check(OnInit()==INIT_PARAMETERS_INCORRECT,"nonfinite doji");
 for(string sym:{"AUDUSD","USDCHF","USDMXN","XAUUSD","GOLD","US100","NAS100","USTEC"}) {
  reset();symbol_name=sym;
  if(sym=="AUDUSD"||sym=="USDCHF"||sym=="USDMXN") {profit_mode=0;Point=.00001;Digits=5;tick_size=.00001;}
  LotSizeFX=.01;LotSizeGold=.02;LotSizeNAS100=.03;init();
  near(g_lot_size,Instrument()==INSTRUMENT_FX?.01:Instrument()==INSTRUMENT_GOLD?.02:.03,1e-9,"instrument sizing copied");
 }
 reset();stats[STAT_PROFIT]=100;stats[STAT_TRADES]=100;check(OnTester()==-1e9,"tester minimum filters retained");
 TesterUseCustomScore=false;near(OnTester(),100,1e-9,"profit objective");
}

void restart_tests() {
 reset();testing=false;int a=add(OP_BUY,90);init();tick(100,100);basic_run();OnDeinit(0);g_ready=false;init();
 check(g_positions[PositionIndex(a)].armed&&g_positions[PositionIndex(a)].sequence_count==2,"normal restart preserves activation and sequence");
 finish(104,102);check(!live(a),"restored sequence closes");
 check(!GlobalVariableCheck(PositionKey(a)+".v"),"closed ticket globals removed");OnDeinit(0);
 reset();testing=false;a=add(OP_BUY,90);init();tick(100,100);basic_run();fail_close=1;finish(104,102);
 OnDeinit(0);g_ready=false;init();check(g_positions[PositionIndex(a)].close_due,"pending close restored");
 tick(102,102);check(!live(a)&&modify_attempts==1,"restored close does not remove or redundantly update backup stop");OnDeinit(0);
 reset();testing=false;a=add(OP_BUY,90);init();tick(100,100);finish(100,102);OnDeinit(0);
 DojiBodyPips=2;g_ready=false;init();
 check(g_positions[PositionIndex(a)].armed&&g_positions[PositionIndex(a)].sequence_count==0,"doji change resets counts not activation");OnDeinit(0);
 reset();testing=false;a=add(OP_BUY,90);init();tick(100,100);basic_run();OnDeinit(0);
 minimum_candles_in_sequence=3;g_ready=false;init();finish(104,102);
 check(live(a),"new optimized minimum applied to restored count");OnDeinit(0);
 reset();testing=false;a=add(OP_BUY,90);init();tick(100,100);OnDeinit(0);
 GlobalVariableSet(PositionKey(a)+".v",0);g_ready=false;init();
 check(!g_positions[PositionIndex(a)].armed,"incomplete snapshot rejected");OnDeinit(0);
 // v1 migration: only activation and already accepted exits survive. Old pair
 // lengths must never masquerade as a consecutive filtered sequence.
 for(int due:{0,1}) {
  reset();testing=false;a=add(OP_BUY,90);init();tick(100,100);OnDeinit(0);
  string k=PositionKey(a);GlobalVariableSet(k+".v",1);GlobalVariableSet(k+".a",1);GlobalVariableSet(k+".x",due);
  GlobalVariableSet(k+".p",20);GlobalVariableSet(k+".n",30);g_ready=false;init();
  check(g_positions[PositionIndex(a)].armed&&g_positions[PositionIndex(a)].sequence_count==0,"v1 activation migrated; old counts discarded");
  check(g_positions[PositionIndex(a)].close_due==bool(due),"v1 accepted exit retained");
  check(GlobalVariableGet(k+".v")==2,"migrated state committed as v2");
  tick(100,100);check(live(a)==!bool(due),"migration only retries previously accepted exits");OnDeinit(0);
 }
 reset();testing=false;init();GlobalVariableSet(g_lock_key,999);check(OnInit()==INIT_FAILED,"duplicate symbol/magic ownership denied");
 reset();a=add(OP_BUY,90);init();tick(100,100);
 append_without_tick(100,102);append_without_tick(102,104);append_without_tick(104,102);append_without_tick(102,104);
 tick(104,104);check(live(a)&&g_positions[PositionIndex(a)].sequence_count==1,"catchup ignores stale exit and rebuilds latest run only");
 finish(104,106);finish(106,104);check(!live(a),"new reversal after catchup qualifies");
 reset();a=add(OP_BUY,90);init();tick(100,100);finish(100,102);candles.erase(candles.end()-2);finish(102,104);
 check(live(a)&&logged("unavailable/too-long candle gap"),"missing history resets without stale close");
}

void random_candles() {
 std::mt19937 rng(779002);
 for(auto direction:{LONG_ONLY,SHORT_ONLY}) {
  reset();TradeDirection=direction;init();bare_position();
  int expected_count=0;
  auto previous=bar(100,102,98,101);
  for(int n=0;n<20000;++n) {
   double o=90+int(rng()%21),c=o+int(rng()%17)-8;
   auto b=bar(o,std::max(o,c)+rng()%4,std::min(o,c)-rng()%4,c);
   // Independent classification uses explicit inequalities on integer prices.
   bool outer=b.high>previous.high&&b.low<previous.low;
   bool inner=b.high<=std::max(previous.open,previous.close)&&b.low>=std::min(previous.open,previous.close);
   int kind=outer?3:inner?2:std::abs(c-o)<=1?0:c>o?1:-1;
   check(ClassifyCandle(b,previous)==kind,"random OHLC classification matches independent inequalities");
   minimum_candles_in_sequence=1+rng()%12;
   bool expected=false;int expected_done=0;
   if(kind==Direction())++expected_count;
   else if(kind==-Direction()) {expected_done=expected_count;expected=expected_count>=minimum_candles_in_sequence;expected_count=0;}
   int done=0;check(step(ClassifyCandle(b,previous),done)==expected,"random minimum-run exit");
   check(g_positions[0].sequence_count==expected_count&&done==expected_done,"random count/reset/ignored semantics");
   previous=b;
  }
 }
}

void break_even_math_tests() {
 std::mt19937 rng(2000779);
 for(int n=0;n<10000;++n) for(int direction:{-1,1}) for(bool inverse:{false,true}) {
  double entry=1.+(rng()%100000)/10000.,lots=(1+rng()%100)/100.;
  double units=100000*lots;
  double quote=entry+direction*(.01+(rng()%100)/10000.);
  double costs=-double(1+rng()%20)*lots;
  double gross=direction*(quote-entry)*units/(inverse?quote:1.);
  double slope=units/(inverse?quote:1.);
  double stop=0;
  check(CandleTakerBreakEven(gross,costs,slope,quote,direction,inverse,stop),"valid break-even solution");
  // Independently solve signed contract P/L + fixed costs = 0.
  double expected=inverse?direction*units*entry/(direction*units+costs):entry-direction*costs/units;
  near(stop,expected,1e-10,"linear/inverse break-even analytical reference");
  double actual_net=direction*(stop-entry)*units/(inverse?stop:1.)+costs;
  near(actual_net,0,1e-7,"cost-inclusive break-even");
  double rounded=CandleTakerRoundPrice(stop,.00001,direction>0);
  double rounded_net=direction*(rounded-entry)*units/(inverse?rounded:1.)+costs;
  check(rounded_net>=-1e-7,"directional rounding never materially underfunds costs");
 }
 double price=0;
 check(!CandleTakerBreakEven(1,-2,1,100,1,false,price),"negative net no protective solve");
 check(!CandleTakerBreakEven(1,0,0,100,1,false,price),"zero tick value rejected");
 check(!CandleTakerBreakEven(1,0,1,100,0,false,price),"invalid direction rejected");
 check(!CandleTakerBreakEven(1,std::nan(""),1,100,1,false,price),"invalid costs rejected");
}

int main() {
 try {
  classification_tests();sequence_tests();execution_tests();protection_tests();grid_tests();restart_tests();random_candles();break_even_math_tests();
  std::cout<<"PASS: "<<checks<<" assertions; 40,000 randomized OHLC transitions, 40,000 break-even calculations, actual-handler integration scenarios\n";
 } catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
