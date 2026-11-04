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
 MagicNumber=779001;MaximumProfitDrawdownPercent=15;ExitCommissionPerLot=0;
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
}
int add(int type,double open,double lots=1,double commission=0,double swap=0,int magic=779001,string sym="US100") {
 int ticket=next_ticket++;
 orders.push_back({ticket,type,magic,sym,"",lots,open,0,0,commission,swap});
 return ticket;
}
bool live(int ticket) { return OrderSelect(ticket,SELECT_BY_TICKET) && OrderCloseTime()==0; }
void tick(double bid,double ask) { Bid=bid;Ask=ask;quote_time=clock_now;OnTick(); }
int pending_count() { int n=0;for(auto &o:orders)if(!o.closed && o.type>OP_SELL && o.magic==MagicNumber)++n;return n; }
void init() { check(OnInit()==INIT_SUCCEEDED,"init succeeds"); }
bool logged(string word) { for(auto &s:logs)if(s.find(word)!=string::npos)return true;return false; }


double peak(int ticket) { int i=PositionIndex(ticket); return i<0?0:g_positions[i].peak; }
bool due(int ticket) { int i=PositionIndex(ticket); return i>=0&&g_positions[i].close_due; }
double positive_net() {
 double sum=0;for(size_t i=0;i<orders.size();++i){selected=int(i);if(!sel().closed&&MarketSelected())sum+=std::max(0.,SelectedNet());}return sum;
}

void scenarios() {
 reset();init();tick(100,100);check(pending_count()==6,"three grid levels on each side");
 for(auto &o:orders) { check((o.type==OP_BUYSTOP && o.open>Ask) || (o.type==OP_BUYLIMIT && o.open<Ask),"long stops above and limits below Ask");check(o.expiration==date(2026,11,6,23,35),"broker expiration"); }
 tick(100,100);check(sends==6,"no duplicate orders on repeated tick");
 tick(80,80);check(pending_count()==9,"nearby pending orders retained while new frontier is filled");check(deletes==0,"modest retreat causes no pending cancellation");

 reset();TradeDirection=SHORT_ONLY;init();tick(100,100);
 for(auto &o:orders)check((o.type==OP_SELLSTOP && o.open<Bid) || (o.type==OP_SELLLIMIT && o.open>Bid),"short stops below and limits above Bid");

 reset();init();tick(100,100);int first=orders.front().ticket;
 OrderSelect(first,SELECT_BY_TICKET);sel().type=OP_BUY;
 tick(110,110);check(pending_count()==7,"replenish after fill while retaining old frontier");
 check(OwnOrderCount()==8,"one filled plus seven pending");
 for(auto &o:orders)check(o.lots==1,"constant lot size");

 reset();MaxOrders=3;init();tick(100,100);orders[0].type=OP_BUY;tick(110,110);
 check(OwnOrderCount()==3,"combined exposure cap");

 int a=0;
 reset();init();tick(100,100);MaxSpreadPips=1;tick(100,102);
 check(pending_count()==0 && sends==6,"wide spread deletes pending and blocks new");check(logged("spread too high"),"wide spread explicitly logged");
 tick(100,100);check(pending_count()==6,"spread recovery resumes grid");

 reset();a=add(OP_BUY,90);MaxSpreadPips=1;init();tick(100,100);tick(98.5,103);
 check(!live(a),"high spread never blocks profit exit");

 reset();UseSpreadJumpFilter=true;SpreadSampleTicks=3;MaxSpreadJumpPips=.5;init();
 tick(100,100);tick(100,100);check(sends==0,"spread warmup");tick(100,100);check(sends==6,"spread warmup completed");
 tick(100,101);check(pending_count()==0,"spread jump cancels frontier");

 reset();init();tick(100,100);fail_delete=1;tick(300,300);check(sends==6,"failed delete blocks replacements");
 tick(300,300);check(pending_count()==6,"retry stale delete and replenish");

 reset();expiration_allowed=false;init();tick(100,100);check(sends==1 && pending_count()==0,"no unexpiring fallback");
 tick(100,100);check(sends==1,"expiration failure latched");

 reset();fail_send=128;ambiguous_accept=true;init();tick(100,100);check(orders.size()==1,"timeout can be accepted server-side");
 tick(100,100);check(sends==1,"ambiguous timeout never blindly resent");check(pending_count()==0,"ambiguous orders removed when visible");

 reset();init();tick(100,100);Order duplicate=orders[0];duplicate.ticket=next_ticket++;orders.push_back(duplicate);
 tick(100,100);check(pending_count()==6 && !live(duplicate.ticket),"duplicate pending level reconciled");
 reset();init();tick(100,100);freeze_points=500;tick(109,109);
 check(live(orders[0].ticket),"near-market pending retained inside freeze range");
 reset();a=add(OP_BUY,90);init();fail_modify=1;tick(100,100);check(logged("STOP_FAILED"),"stop rejection logged");
 clock_now+=2;tick(100,100);OrderSelect(a,SELECT_BY_TICKET);check(OrderStopLoss()>0,"rejected stop retried after backoff");
 reset();init();fail_send=130;tick(100,100);tick(100,100);check(sends==1,"send error backoff");
 clock_now+=2;tick(100,100);check(pending_count()==6,"send retry after backoff");

 reset();session_available=false;init();tick(100,100);check(sends==0,"missing schedule fails closed");
 FridayCloseMinutesServer=1395;tick(100,100);check(sends==6,"explicit server close override");

 reset();clock_now=date(2026,11,6,23,34,59);quote_time=clock_now;init();tick(100,100);
 clock_now++;OnTimer();check(pending_count()==0,"timer cancels exactly twenty minutes before Friday close");
 tick(100,100);check(sends==6,"cannot rearm Friday after cutoff");
 clock_now=date(2026,11,8,23);tick(100,100);check(sends==6,"Sunday gap blackout");
 clock_now=date(2026,11,9,1);tick(100,100);check(sends==12,"Monday resumes next weekly expiration");

 reset();testing=false;clock_now=date(2026,11,6,23,34,59);quote_time=clock_now;init();tick(100,100);
 monotonic_ms=2000;OnTimer();check(pending_count()==0,"cleanup timer estimates server time without ticks");

 reset();init();tick(100,100);fail_delete=1;clock_now=date(2026,11,6,23,35);OnTimer();
 check(pending_count()==1,"Friday failed deletion retained for retry");OnTimer();check(pending_count()==0,"Friday deletion retried");
 reset();init();tick(100,100);session_finish=23*3600;tick(100,100);
 for(auto &o:orders)if(!o.closed)check(o.expiration==date(2026,11,6,22,40),"broker schedule update refreshes expiration");

 reset();session_start=3*3600;session_finish=23*3600+15*60;init();datetime end=0;
 check(EntryDeadline(clock_now,end)&&end==date(2026,11,6,22,55),"Nasdaq earlier close");
 session_finish=0;check(EntryDeadline(clock_now,end)&&end==date(2026,11,6,23,40),"midnight close");
 session_start=21*3600;session_finish=2*3600;check(EntryDeadline(clock_now,end)&&end==date(2026,11,7,1,40),"overnight Friday session");
 SpecialCloseServerTime=date(2026,11,6,18);check(EntryDeadline(clock_now,end)&&end==date(2026,11,6,17,40),"holiday earlier close override");

 reset();symbol_name="XAUUSD";profit_mode=0;init();near(g_pip,.01,1e-12,"RoboForex gold pip");
 reset();symbol_name="USDCHF";profit_mode=0;Point=.00001;Digits=5;tick_size=.00001;init();near(g_pip,.0001,1e-12,"USDCHF pip");
 check(InverseConversion(),"inverse USDCHF account conversion");
 reset();symbol_name="USDMXN";profit_mode=0;Point=.00001;Digits=5;tick_size=.00001;init();near(g_pip,.0001,1e-12,"USDMXN pip");
 reset();symbol_name="UNKNOWNCFD";check(OnInit()!=INIT_SUCCEEDED,"unknown CFD pip rejected");
 PipSizeOverride=1;check(OnInit()!=INIT_SUCCEEDED,"pip override cannot size an unknown CFD using FX lots");
 reset();LotSizeNAS100=.015;check(OnInit()!=INIT_SUCCEEDED,"invalid lot step rejected rather than rounded");
 reset();GridStepPips=.015;check(OnInit()!=INIT_SUCCEEDED,"off-tick grid rejected");
 reset();MaximumProfitDrawdownPercent=100;check(OnInit()!=INIT_SUCCEEDED,"invalid drawdown");
 reset();MagicNumber=0;check(OnInit()!=INIT_SUCCEEDED,"manual magic prohibited");

 reset();add(OP_SELL,110);init();tick(100,100);check(sends==0,"direction change cannot hedge old own positions");
 reset();free_margin=0;init();tick(100,100);check(sends==0,"margin precheck");
 reset();tick_value=0;init();tick(100,100);check(sends==0,"missing valuation blocks entries");

 reset();init();stats[STAT_PROFIT]=2000;stats[STAT_TRADES]=4000;stats[STAT_EQUITY_DD]=100;
 near(OnTester(),13950,1e-9,"Harvester optimizer formula");
 stats[STAT_TRADES]=10;check(OnTester()==-1e9,"minimum trades filter");
 stats[STAT_TRADES]=4000;stats[STAT_EQUITY_DD]=30000;check(OnTester()==-1e9,"money drawdown filter");
 stats[STAT_EQUITY_DD]=100;TesterUseMaxDrawdownPercentFilter=true;stats[STAT_EQUITY_DDREL_PERCENT]=30;
 check(OnTester()==-1e9,"percentage drawdown filter");TesterUseCustomScore=false;check(OnTester()==2000,"raw profit option");
}

void stress_ticks() {
 std::mt19937_64 rng(42);
 for(int dir : {1,-1}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;MaxOrders=20;MaxSpreadPips=2;GridStepPips=1;
  EnableDebugPrints=false;ExitCommissionPerLot=-1;init();
  int manual=add(OP_BUY,1,1,0,0,0);
  double price=100;
  for(int t=0;t<3000;++t) {
   clock_now++;price=std::max(20.,price+(int(rng()%41)-20)*.05);
   Bid=price;Ask=price+(t%71==0 ? 3.0:.02);quote_time=clock_now;
   // Broker execution precedes EA observation: pending triggers can gap and stops
   // may execute before cancellation/spread filtering is possible.
   for(size_t i=0;i<orders.size();++i) {
    auto &o=orders[i];if(o.closed || o.magic!=MagicNumber)continue;
    selected=int(i);
    if(o.type==OP_BUYSTOP && Ask>=o.open) { o.type=OP_BUY;o.open=Ask;o.commission=-.02; }
    if(o.type==OP_SELLSTOP && Bid<=o.open) { o.type=OP_SELL;o.open=Bid;o.commission=-.02; }
    if(o.type==OP_BUYLIMIT && Ask<=o.open) { o.type=OP_BUY;o.open=Ask;o.commission=-.02; }
    if(o.type==OP_SELLLIMIT && Bid>=o.open) { o.type=OP_SELL;o.open=Bid;o.commission=-.02; }
    if(o.type<=OP_SELL && o.sl>0 && (o.type==OP_BUY ? Bid<=o.sl:Ask>=o.sl)) {
     o.realized=OrderProfit();o.commission-=.02;o.closed=clock_now;
    }
   }
   OnTick();
   check(live(manual),"stress manual isolation");
   check(OwnOrderCount()<=MaxOrders,"stress exposure cap");
   check(pending_count()<=2*(PendingGridLevels+PendingRetentionExtraLevels)+1,"stress bounded frontier");
   std::map<long,int> levels;
   for(size_t i=0;i<orders.size();++i) {
    selected=int(i);auto &o=orders[i];if(o.closed || o.magic!=MagicNumber)continue;
    check(o.type==MarketType() || PendingMatchesDirection(o.type),"stress one direction");
    check(o.lots==g_lot_size,"stress constant lots");
    if(PendingMatchesDirection(o.type))check(++levels[SelectedGrid()]==1,"stress no duplicate pending grid");
   }
   logs.clear();
  }
 }
}

void lot_routing_and_contract_tests() {
 for(const string name : {"US100", "US100.p", "NAS100", "nasdaq100.cash", "USTEC"}) {
  reset();symbol_name=name;LotSizeFX=.02;LotSizeNAS100=.03;LotSizeGold=.04;init();tick(100,100);
  near(g_lot_size,.03,1e-12,"Nasdaq alias selects Nasdaq lots");
  check(g_lot_input=="LotSizeNAS100","Nasdaq setting logged");
  for(auto &o:orders)near(o.lots,.03,1e-12,"Nasdaq pending order actual lots");
  LotSizeNAS100=.07;tick(100,100);
  near(g_lot_size,.03,1e-12,"resolved lot size remains fixed during run");
 }
 for(const string name : {"XAUUSD", "XAUUSD.p", "GOLD", "gold.cash"}) {
  reset();symbol_name=name;profit_mode=0;base_currency="XAU";
  LotSizeFX=.02;LotSizeNAS100=.03;LotSizeGold=.04;init();tick(100,100);
  near(g_lot_size,.04,1e-12,"gold overrides FX calculation mode");
  for(auto &o:orders)near(o.lots,.04,1e-12,"gold pending order actual lots");
 }
 for(const string name : {"USDCHF", "USDMXN", "EURUSD.p"}) {
  reset();symbol_name=name;profit_mode=0;Point=.00001;Digits=5;tick_size=.00001;
  LotSizeFX=.02;LotSizeNAS100=.03;LotSizeGold=.04;init();tick(1.1,1.1);
  near(g_lot_size,.02,1e-12,"FX selects only FX lots");
  for(auto &o:orders)near(o.lots,.02,1e-12,"FX pending order actual lots");
 }
 reset();symbol_name="XAGUSD";profit_mode=0;check(OnInit()!=INIT_SUCCEEDED,"silver cannot use FX or gold lots");
 reset();symbol_name="GOLD";LotSizeGold=.015;check(OnInit()!=INIT_SUCCEEDED,"gold lot validated against broker step");
 reset();LotSizeFX=-1;LotSizeGold=-1;LotSizeNAS100=.01;init();
 near(g_lot_size,.01,1e-12,"unused categories do not affect selected lot size");

 // User's USD-account contract examples. Nasdaq: $1/point/lot; gold: $100/point/lot.
 // Different tick sizes verify that points, ticks and pips are not conflated.
 for(bool gold : {false,true}) for(int dir : {1,-1}) for(double lots : {.01,1.0}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;
  LotSizeFX=LotSizeNAS100=LotSizeGold=lots;
  symbol_name=gold?"XAUUSD":"US100";base_currency=gold?"XAU":"USD";profit_mode=gold?0:1;
  tick_size=gold?.01:.1;tick_value=gold?1.0:.1;
  double entry=gold?4042:28500;
  double hundred_point_profit=(gold?10000:100)*lots;
  Bid=Ask=entry;
  int ticket=add(MarketType(),entry,lots,0,0,MagicNumber,symbol_name);init();
  tick(entry+dir*100,entry+dir*100);
  near(positive_net(),hundred_point_profit,1e-8,"100-point move net cash at requested lot size");
  near(peak(ticket),hundred_point_profit,1e-8,"100-point move peak cash");
  double expected_stop=entry+dir*85;
  OrderSelect(ticket,SELECT_BY_TICKET);
  near(OrderStopLoss(),expected_stop,1e-7,"15 percent cash giveback yields 15-point retracement stop");
  near(ScrubberProjectedNet(OrderProfit(),0,tick_value*lots/tick_size,ExitPrice(),dir,false,expected_stop),hundred_point_profit*.85,1e-7,"projected position floor in USD");
  // Independently verify the adverse 100-point move without simulating a broker stop fill.
  Bid=Ask=entry-dir*100;OrderSelect(ticket,SELECT_BY_TICKET);
  near(SelectedNet(),-hundred_point_profit,1e-8,"100-point adverse move loss in USD");
  near(positive_net(),0,1e-9,"adverse move leaves no positive net profit");
 }
}

void tester_timer_regressions() {
 // Reproduce the user's AUDUSD short setup with no timer support.
 reset();symbol_name="AUDUSD";profit_mode=0;base_currency="AUD";
 Point=.00001;Digits=5;tick_size=.00001;tick_value=1;
 Bid=.65000;Ask=.65002;TradeDirection=SHORT_ONLY;
 LotSizeFX=LotSizeNAS100=LotSizeGold=.01;GridStepPips=4;
 clock_now=date(2026,10,22,0,5);quote_time=clock_now;timer_available=false;
 init();check(timer_start_calls==0 && !g_timer_started,"tester never attempts timer registration");
 check(logged("TESTER_MODE"),"tester cleanup mode logged");
 tick(Bid,Ask);check(pending_count()==6,"reported AUDUSD setup starts and places short grid");
 for(auto &o:orders)check(PendingMatchesDirection(o.type) && o.lots==.01,"AUDUSD short fixed lots");
 // No OnTimer() invocation: simulated Friday tick drives cleanup.
 clock_now=date(2026,10,23,23,35);tick(Bid,Ask);
 check(pending_count()==0,"tester Friday cleanup without timer events");
 int old_sends=sends;tick(Bid,Ask);check(sends==old_sends,"tester cannot rearm after cutoff");
 OnDeinit(0);check(timer_kill_calls==0,"tester does not kill an unregistered timer");

 reset();testing=false;init();check(timer_start_calls==1 && g_timer_started,"live timer still registered");
 OnDeinit(0);check(timer_kill_calls==1 && !g_timer_started,"live timer released");
 reset();testing=false;timer_available=false;
 check(OnInit()==INIT_FAILED,"live timer failure still blocks unsafe initialization");
 check(!g_ready && !g_owns_lock && !g_timer_started,"live timer failure releases ownership");
 check(logged("live cleanup timer err=4059"),"live timer failure logs actual error");
 check(GlobalVariableGet(g_lock_key)==0,"failed live init leaves no stale ownership lock");
 OnDeinit(0);check(timer_kill_calls==0,"failed registration not killed");
}

void fill_pending_then_tick(double bid,double ask) {
 Bid=bid;Ask=ask;quote_time=clock_now;
 for(auto &o:orders) {
  if(o.closed || o.magic!=MagicNumber || o.symbol!=Symbol())continue;
  if((o.type==OP_BUYSTOP && Ask>=o.open) || (o.type==OP_BUYLIMIT && Ask<=o.open)) {
   o.type=OP_BUY;o.open=Ask;
  } else if((o.type==OP_SELLSTOP && Bid<=o.open) || (o.type==OP_SELLLIMIT && Bid>=o.open)) {
   o.type=OP_SELL;o.open=Bid;
  }
 }
 OnTick();
}

void two_sided_inventory_regressions() {
 for(int dir : {1,-1}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;init();tick(100,100);
  int stops=0,limits=0;
  for(auto &o:orders) {
   if(o.type==OP_BUYSTOP || o.type==OP_SELLSTOP)++stops;else ++limits;
  }
  check(stops==3 && limits==3,"both sides pre-positioned equally");
  check(logged(dir==1?"BUY_LIMIT":"SELL_LIMIT"),"limit type visible in placement log");
  std::vector<int> inventory;
  for(int level=1;level<=3;++level) {
   double counter_price=100-dir*10*level;
   fill_pending_then_tick(counter_price,counter_price);
   inventory.clear();
   for(auto &o:orders)if(!o.closed && o.magic==MagicNumber && o.type==MarketType()) inventory.push_back(o.ticket);
   check(int(inventory.size())==level,"counter-trend limit fills add inventory at every crossed grid");
   near(positive_net(),0,1e-9,"no profits on uninterrupted adverse move");
   check(closes==0,"counter-trend inventory is not liquidated");
   for(int ticket:inventory) {
    OrderSelect(ticket,SELECT_BY_TICKET);long grid=SelectedGrid();
    for(size_t i=0;i<orders.size();++i) {
     selected=int(i);
     if(!sel().closed && PendingMatchesDirection(sel().type))
      check(SelectedGrid()!=grid,"filled grid not duplicated by the other pending type");
    }
   }
  }
  double rebound=100-dir*20;
  fill_pending_then_tick(rebound,rebound);
  near(positive_net(),10,1e-8,"rebound values only profitable open inventory");
  near(peak(inventory.back()),10,1e-8,"rebound updates peak on the same timestamp");
  double giveback=rebound-dir*1.5;
  fill_pending_then_tick(giveback,giveback);
  check(!live(inventory.back()),"15 percent giveback closes profitable inventory");
  check(live(inventory[0]) && live(inventory[1]),"losing inventory remains after independent exit");
  check(PositionIndex(inventory.back())<0,"closed ticket state retired");
 }
 reset();Bid=100;Ask=102;init();tick(Bid,Ask);
 for(auto &o:orders)check((o.type==OP_BUYSTOP && o.open>Ask) || (o.type==OP_BUYLIMIT && o.open<Ask),"buy stops and limits use Ask");
 reset();TradeDirection=SHORT_ONLY;Bid=100;Ask=102;init();tick(Bid,Ask);
 for(auto &o:orders)check((o.type==OP_SELLSTOP && o.open<Bid) || (o.type==OP_SELLLIMIT && o.open>Bid),"sell stops and limits use Bid");
 reset();MaxOrders=3;init();tick(100,100);
 check(orders[0].type==OP_BUYSTOP && orders[1].type==OP_BUYLIMIT,"exposure cap still allows nearest levels on both sides");
 reset();init();tick(100,100);int limit_ticket=orders[1].ticket;
 Order duplicate=orders[1];duplicate.ticket=next_ticket++;orders.push_back(duplicate);tick(100,100);
 check(live(limit_ticket) && !live(duplicate.ticket),"duplicate limit cleaned up");
 MaxSpreadPips=1;tick(100,102);check(pending_count()==0,"spread guard removes both stop and limit types");
}

void pending_retention_regressions() {
 for(int dir : {1,-1}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;init();tick(100,100);
  std::vector<int> initial;
  for(auto &o:orders)initial.push_back(o.ticket);
  // Deliberately delay broker activation at a crossed level. The EA must not
  // cancel/retype that ticket while the broker is processing it.
  tick(99.98,99.98);tick(100.02,100.02);
  int placed_after_warmup=sends;
  for(int n=0;n<1000;++n) {
   double price=n%2 ? 99.98:100.02;
   tick(price,price);
   check(deletes==0,"boundary jitter never cancels retained orders");
   check(sends==placed_after_warmup,"boundary jitter never recreates retained orders");
  }
  for(int ticket:initial)check(live(ticket),"original pending ticket preserved across boundary jitter");

  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;init();tick(100,100);
  int at70=-1;
  for(auto &o:orders)if(o.open==70)at70=o.ticket;
  check(at70>0,"retention test initial order exists");
  tick(130,130);check(live(at70),"exact six-grid retention distance is kept");
  tick(130.02,130.02);check(!live(at70),"order removed only after leaving retention range");
  check(logged("beyond retention distance="),"distance cancellation logs actual reason");
  int after_far_delete=sends, deleted=deletes;
  for(int n=0;n<100;++n) {
   double price=n%2 ? 129.98:130.02;tick(price,price);
  }
  // Another nearby placement may be needed once at 130, but the distant 70
  // order must never be recreated while oscillating near its deletion boundary.
  check(sends<=after_far_delete+1,"retention boundary does not cause cancel/recreate churn");
  check(deletes==deleted,"retention boundary has no repeated deletion");
  for(auto &o:orders)if(!o.closed)check(o.open!=70,"deleted far level stays outside placement range");
 }
 reset();MaxOrders=6;init();tick(100,100);tick(99.98,99.98);tick(100.02,100.02);
 check(sends==6 && deletes==0 && OwnOrderCount()==6,"retained exposure respects cap without rotating orders");
 reset();PendingRetentionExtraLevels=-1;check(OnInit()==INIT_PARAMETERS_INCORRECT,"negative retention rejected");
}

void numerical_tests() {
 check(ScrubberMeetsFloor(5,1,5),"inclusive activation");
 check(!ScrubberMeetsFloor(4.99,1,5),"below activation");
 check(!ScrubberMeetsFloor(0,1,0),"zero is not positive");
 check(ScrubberDrawdownHit(80,100,20),"inclusive drawdown");
 std::mt19937_64 rng(20261106);
 std::uniform_real_distribution<double> u(0,1);
 for(int k=0;k<10000;++k) {
  int dir=k%2?1:-1;bool inverse=k%3==0;
  double x=10+u(rng)*1000,slope=1+u(rng)*1000;
  double costs=-u(rng)*10,net=(.01+u(rng)*.2)*slope*x,gross=net-costs;
  double target=net*(.1+.8*u(rng)),p=0;
  check(ScrubberPositionStop(gross,costs,slope,x,dir,inverse,target,p),"random per-position stop exists");
  near(ScrubberProjectedNet(gross,costs,slope,x,dir,inverse,p),target,1e-6,"independent substituted stop equation");
  check(dir*(x-p)>0,"stop on adverse side");
  double scaled=0;
  check(ScrubberPositionStop(gross*.01,costs*.01,slope*.01,x,dir,inverse,target*.01,scaled),"scaled root exists");
  near(p,scaled,1e-8,"stop independent of lot scale");
 }
 double p=0;check(!ScrubberPositionStop(10,0,0,100,1,false,8,p),"invalid valuation rejected");
 check(!ScrubberPositionStop(10,0,1,100,1,false,10,p),"already breached floor uses market fallback");
 near(ScrubberRoundPrice(100.021,.01,true),100.03,1e-8,"long inward rounding");
 near(ScrubberRoundPrice(100.029,.01,false),100.02,1e-8,"short inward rounding");
}

void independent_positions() {
 for(int dir:{1,-1})for(double lot:{.01,.1,1.,10.}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;LotSizeNAS100=lot;
  position_minimum_profit_floor=5;MaximumProfitDrawdownPercent=20;
  int old=add(MarketType(),100-dir*50,lot),young=add(MarketType(),100-dir*10,lot);
  int small=add(MarketType(),100-dir*3,lot),loser=add(MarketType(),100+dir*10,lot);
  int manual=add(MarketType(),100-dir*10,lot,0,0,0);
  int other=add(MarketType(),100-dir*10,lot,0,0,123);
  int symbol=add(MarketType(),100-dir*10,lot,0,0,MagicNumber,"OTHER");
  init();tick(100,100);
  near(peak(old),50*lot,1e-8,"old position own peak");near(peak(young),10*lot,1e-8,"young position own peak");
  check(live(old),"old live");near(OrderStopLoss(),100-dir*10,1e-8,"old independent stop");
  check(live(young),"young live");near(OrderStopLoss(),100-dir*2,1e-8,"young independent stop");
  check(peak(small)==0&&peak(loser)==0,"small profit and loser unarmed");
  int sent=sends,deleted=deletes;tick(100-dir*2,100-dir*2);
  check(!live(young)&&live(old)&&live(small)&&live(loser),"only young position triggers");
  near(peak(old),50*lot,1e-8,"closing young does not reset old peak");
  check(live(manual)&&live(other)&&live(symbol),"manual other magic and symbol isolated");
  check(deletes==deleted&&sends<=sent+1,"exit retains pending grid; only free level may replenish");
  // No candle boundary: new activation and exit can happen at the same timestamp.
  int again=add(MarketType(),100-dir*12,lot);tick(100-dir*2,100-dir*2);tick(100-dir*4,100-dir*4);
  check(!live(again)&&live(old),"multiple independent exits within same candle");
 }
 for(int dir:{1,-1}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;position_minimum_profit_floor=5;MaximumProfitDrawdownPercent=20;
  int a=add(MarketType(),100-dir*5);init();tick(100,100);
  near(peak(a),5,1e-9,"activation at floor");check(live(a),"armed at floor");
  near(OrderStopLoss(),100-dir,1e-8,"stop permitted below activation floor");
  tick(100-dir*.5,100-dir*.5);check(live(a)&&peak(a)==5,"4.5 pips remains armed without exit");
  tick(100-dir,100-dir);check(!live(a),"exit at four pips, full 20 percent retracement");

  // Former artificial drawdown: 20 + 8.2 falls to 19.7 + 7.9.
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;position_minimum_profit_floor=8;MaximumProfitDrawdownPercent=20;
  int old=add(MarketType(),100-dir*20),young=add(MarketType(),100-dir*8.2);
  init();tick(100,100);tick(100-dir*.3,100-dir*.3);
  check(live(old)&&live(young)&&closes==0,"crossing activation level no longer causes artificial drawdown");
  near(peak(young),8.2,1e-8,"activation is permanent");
  check(live(young),"young selected");near(OrderStopLoss(),100-dir*1.64,1e-8,"no floor-boundary early stop");
  // Actual broker stop of young does not cause older winner to exit.
  Bid=Ask=100-dir*1.64;OrderSelect(young,SELECT_BY_TICKET);sel().realized=OrderProfit();sel().closed=clock_now;
  tick(Bid,Ask);check(live(old)&&!due(old)&&closes==0,"broker stop cannot cascade into another exit");
  near(peak(old),20,1e-8,"broker realization preserves other peak");
 }
}

void costs_gaps_and_retries() {
 for(int dir:{1,-1}) {
  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;position_minimum_profit_floor=5;MaximumProfitDrawdownPercent=20;
  ExitCommissionPerLot=-1;
  int a=add(MarketType(),100-dir*10,1,-2,-1); // net = 10 -2 -1 -2 = 5
  init();tick(100,100);near(peak(a),5,1e-9,"activation includes swap and both commission components");
  tick(100-dir,100-dir);check(!live(a),"net four-pip exit includes costs");

  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;position_minimum_profit_floor=5;MaximumProfitDrawdownPercent=20;
  a=add(MarketType(),100-dir*10);int b=add(MarketType(),100-dir*50);
  init();tick(100,100);fail_close=1;tick(100-dir*2.1,100-dir*2.1);
  check(live(a)&&due(a)&&live(b)&&!due(b),"failed close latches only one ticket");
  int attempts=closes;tick(100-dir,100-dir);check(live(a)&&closes==attempts,"close retry backoff");
  clock_now+=2;tick(100-dir,100-dir);check(!live(a)&&live(b),"recovery retries only due ticket");

  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;position_minimum_profit_floor=5;MaximumProfitDrawdownPercent=20;min_stop_points=100000;
  a=add(MarketType(),100-dir*10);b=add(MarketType(),100+dir*10);
  init();tick(100,100);tick(100-dir*20,100-dir*20);
  check(live(a)&&live(b)&&due(a)&&peak(a)==10,"gap through breakeven retains losing position and its state");
  tick(100-dir*9,100-dir*9);check(!live(a)&&live(b),"armed gap loser closes when net-positive again");

  reset();TradeDirection=dir==1?LONG_ONLY:SHORT_ONLY;
  a=add(MarketType(),100-dir*10);b=add(MarketType(),100-dir*100);init();tick(100,100);
  Bid=Ask=100-dir*20;OrderSelect(a,SELECT_BY_TICKET);sel().realized=OrderProfit();sel().closed=clock_now;
  tick(Bid,Ask);
  // b itself has now retraced 20% (>15%), so it legitimately exits on its OWN trigger.
  check(!live(a)&&!live(b),"other stop loss does not suppress independent valid exit");
  check(logged("POSITION_DRAWDOWN_TRIGGER"),"independent closure has its own trigger");
 }
 reset();position_minimum_profit_floor=5;int a=add(OP_BUY,90);init();tick(100,100);reported_tick_value=0;tick(98,98);
 check(!live(a),"already armed cash exit works without pip valuation");
 reset();position_minimum_profit_floor=5;tick_value=0;a=add(OP_BUY,90);init();tick(100,100);
 check(peak(a)==0&&sends==0,"missing pip valuation cannot activate or add exposure");
 reset();a=add(OP_BUY,90);init();tick(100,100);OrderSelect(a,SELECT_BY_TICKET);sel().swap=-2;tick(100,100);
 check(!live(a),"rollover included in individual drawdown");
}

void restart_migration_and_logging() {
 reset();int absent=add(OP_BUY,90),remaining=add(OP_BUY,50);init();tick(100,100);
 orders.erase(std::remove_if(orders.begin(),orders.end(),[absent](const Order &o){return o.ticket==absent;}),orders.end());
 tick(100,100);
 check(PositionIndex(absent)<0&&peak(remaining)==50&&!due(remaining),"missing closed history cannot leak state or affect other tickets");
 reset();testing=false;position_minimum_profit_floor=5;MaximumProfitDrawdownPercent=20;
 int a=add(OP_BUY,90),b=add(OP_BUY,50);init();tick(100,100);fail_close=1;tick(97.9,97.9);
 check(due(a),"pending exit before restart");double bp=peak(b);OnDeinit(0);
 g_ready=false;g_position_count=0;init();
 check(due(a)&&peak(a)==10&&peak(b)==bp,"restart restores independent peaks and retry latch");
 tick(99,99);check(!live(a)&&live(b),"restored retry only closes its own ticket");
 check(!GlobalVariableCheck(PositionKey(a)),"closed state removed");
 check(OnInit()==INIT_FAILED,"second same symbol/magic chart rejected");

 reset();testing=false;position_minimum_profit_floor=5;a=add(OP_BUY,95);init();tick(100,100);OnDeinit(0);
 g_ready=false;g_position_count=0;Bid=Ask=99.5;position_minimum_profit_floor=8;init();tick(99.5,99.5);
 check(live(a)&&peak(a)==5,"armed state survives restart below activation and changed input");
 OrderSelect(a,SELECT_BY_TICKET);sel().lots=.5;tick(99.5,99.5);
 near(peak(a),2.5,1e-9,"same-ticket partial close scales peak with remaining volume");

 reset();testing=false;position_minimum_profit_floor=5;a=add(OP_BUY,90);OrderSelect(a,SELECT_BY_TICKET);sel().sl=99.9;
 init();globals[g_key+".peak"]=1000;globals[g_key+".harvest"]=1;
 tick(100,100);check(live(a)&&peak(a)==10&&!due(a),"old aggregate state never imported");
 OrderSelect(a,SELECT_BY_TICKET);near(OrderStopLoss(),98.5,1e-8,"old basket stop replaced with independent stop");
 check(logged("LEGACY_STOP_REMOVED"),"migration reported");

 reset();a=add(OP_BUY,90);OrderSelect(a,SELECT_BY_TICKET);sel().sl=99.9;init();fail_modify=1;tick(100,100);
 check(g_positions[PositionIndex(a)].legacy_stop,"failed migration remains pending");
 clock_now+=2;tick(100,100);check(!g_positions[PositionIndex(a)].legacy_stop,"migration retries safely");

 reset();init();tick(100,100);logs.clear();for(int i=0;i<100;++i){clock_now++;tick(100,100);OnTimer();}
 check(logs.empty(),"idle ticks/timers have no log spam");
 reset();position_minimum_profit_floor=5;a=add(OP_BUY,96);init();tick(100,100);logs.clear();
 for(int i=0;i<100;++i) { tick(100,100); }
 check(logs.empty()&&peak(a)==0,"unarmed positions do not log every tick");
 reset();a=add(OP_BUY,90);init();tick(100,100);logs.clear();
 for(int i=0;i<100;++i) { tick(100,100); }
 check(logs.empty(),"armed unchanged stops do not repeat logs");
 reset();MaxSpreadPips=1;init();tick(100,102);logs.clear();
 for(int i=0;i<100;++i) { tick(100,102+i*.01); }
 check(logs.empty(),"spread block deduplicated");
 reset();position_minimum_profit_floor=-1;check(OnInit()==INIT_PARAMETERS_INCORRECT,"negative activation rejected");
 reset();position_minimum_profit_floor=std::nan("");check(OnInit()==INIT_PARAMETERS_INCORRECT,"nonfinite activation rejected");
}
int main() {
 try {
  check(LotSizeFX==.01&&LotSizeNAS100==.01&&LotSizeGold==.01,"lot defaults");
  check(position_minimum_profit_floor==0,"activation default remains zero");
  numerical_tests();scenarios();stress_ticks();lot_routing_and_contract_tests();tester_timer_regressions();
  two_sided_inventory_regressions();pending_retention_regressions();independent_positions();
  costs_gaps_and_retries();restart_migration_and_logging();
  std::cout<<"PASS: "<<checks<<" assertions; 10,000 randomized position calculations, 6,000 broker ticks, and independent-position regressions\n";
 }catch(const std::exception &e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";for(auto &s:logs)std::cerr<<s<<"\n";return 1;}
 return 0;
}
