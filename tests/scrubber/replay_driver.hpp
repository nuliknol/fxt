// Synthetic broker replay of the actual EA. Not an MT4 tick-model emulator.
int main(int argc, char **argv) {
 try {
  if(argc!=8) throw std::runtime_error("lots booked_fee closing_fee reserve_fee spread_points stop_points minimum_profit_pips required");
  reset();
  double lot=std::stod(argv[1]), booked=std::stod(argv[2]), closing=std::stod(argv[3]);
  double spread=std::stod(argv[5]);
  symbol_name="AUDUSD";base_currency="AUD";profit_mode=0;
  Point=tick_size=.00001;tick_value=1;Digits=5;
  TradeDirection=SHORT_ONLY;LotSizeFX=lot;GridStepPips=4;
  ExitCommissionPerLot=std::stod(argv[4]);min_stop_points=std::stod(argv[6]);
  position_minimum_profit_floor=std::stod(argv[7]);
  EnableDebugPrints=false;EnableChartComment=false;
  bool started=false;
  int ticks=0, filled=0, closed=0, sl_fills=0, client_exits=0, triggers=0;
  double realized=0, high_equity=0, max_dd=0, max_trigger_giveback=0;
  long long ts;long bid_points;
  while(std::cin>>ts>>bid_points) {
   clock_now=quote_time=ts;
   Bid=NormalizeDouble(bid_points*Point,Digits);Ask=NormalizeDouble((bid_points+spread)*Point,Digits);
   if(!started) { init();started=true;logs.clear(); }
   std::vector<int> broker_closed;
   // Expire orders before fills; never interpolate between incoming quotes here.
   for(size_t i=0;i<orders.size();++i) {
    auto &o=orders[i];if(o.closed)continue;selected=int(i);
    if(o.type>OP_SELL && o.expiration && clock_now>=o.expiration) {o.closed=clock_now;continue;}
    bool stop=o.type==OP_SELLSTOP && Bid<=o.open;
    bool limit=o.type==OP_SELLLIMIT && Bid>=o.open;
    if(stop || limit) {
     o.type=OP_SELL;if(stop)o.open=Bid; // stops fill at first quote; limits at requested price
     o.commission=-booked*o.lots;++filled;
    }
    if(o.type==OP_SELL && o.sl>0 && Ask>=o.sl) {
     o.realized=OrderProfit();o.commission-=closing*o.lots;o.closed=clock_now;
     broker_closed.push_back(o.ticket);++sl_fills;
    }
   }
   OnTick();++ticks;
   for(auto &line:logs) {
    if(line.find("POSITION_CLOSED")!=string::npos)++client_exits;
    auto at=line.find("POSITION_DRAWDOWN_TRIGGER");
    if(at!=string::npos) {
     double current=std::stod(line.substr(line.find(" current=",at)+9));
     double peak=std::stod(line.substr(line.find(" peak=",at)+6));
     ++triggers;if(peak>0)max_trigger_giveback=std::max(max_trigger_giveback,100*(1-current/peak));
    }
   }
   logs.clear();
   for(auto &o:orders)if(o.closed && o.type<=OP_SELL) {
    if(std::find(broker_closed.begin(),broker_closed.end(),o.ticket)==broker_closed.end())
     o.commission-=closing*o.lots;
    realized+=o.realized+o.commission+o.swap;++closed;
   }
   // OnTick has consumed broker closures and remembered live tickets.
   orders.erase(std::remove_if(orders.begin(),orders.end(),[](const Order &o){return o.closed!=0;}),orders.end());
   double floating=0;
   for(size_t i=0;i<orders.size();++i)if(orders[i].type<=OP_SELL) {
    selected=int(i);floating+=OrderProfit()+OrderCommission()+OrderSwap()-closing*OrderLots();
   }
   int tracked=0;
   for(auto &o:orders)if(o.type<=OP_SELL)++tracked;
   check(g_position_count==tracked,"replay state count matches live inventory after every event");
   double equity=realized+floating;high_equity=std::max(high_equity,equity);max_dd=std::max(max_dd,high_equity-equity);
  }
  if(!started)throw std::runtime_error("no quotes supplied");
  double floating=0;int open=0;
  for(size_t i=0;i<orders.size();++i)if(orders[i].type<=OP_SELL) {
   selected=int(i);floating+=OrderProfit()+OrderCommission()+OrderSwap()-closing*OrderLots();++open;
  }
  std::cout<<std::setprecision(12)<<"{\"ticks\":"<<ticks<<",\"fills\":"<<filled
   <<",\"closed\":"<<closed<<",\"open\":"<<open<<",\"pending\":"<<pending_count()
   <<",\"realized_usd\":"<<realized<<",\"liquidation_net_usd\":"<<floating
   <<",\"total_net_usd\":"<<realized+floating<<",\"max_equity_drawdown_usd\":"<<max_dd
   <<",\"broker_sl_fills\":"<<sl_fills<<",\"client_position_exits\":"<<client_exits
   <<",\"drawdown_triggers\":"<<triggers<<",\"max_trigger_giveback_percent\":"<<max_trigger_giveback<<"}\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}
 return 0;
}
