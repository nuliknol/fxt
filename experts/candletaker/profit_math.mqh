#ifndef CANDLETAKER_PROFIT_MATH_MQH
#define CANDLETAKER_PROFIT_MATH_MQH
#define CANDLETAKER_CAPACITY 1024

// Pure per-position numerical code, compiled unchanged in the native tests.
bool CandleTakerMeetsFloor(const double net, const double slope, const double minimum_move)
{
   if(net <= 0.0 || !MathIsValidNumber(net)) return false;
   if(minimum_move <= 0.0) return true;
   if(slope <= 0.0 || !MathIsValidNumber(slope)) return false;
   double required = minimum_move * slope;
   return net >= required - required * 1e-9;
}

double CandleTakerProjectedNet(const double gross, const double costs, const double slope,
                           const double exit_price, const int direction,
                           const bool inverse, const double price)
{
   double projected = gross + direction * (price - exit_price) * slope;
   if(inverse && price > 0.0) projected *= exit_price / price;
   return projected + costs;
}

// Solve projected net profit = zero, reserving commission and signed swap.
// When FX base == deposit currency, conversion at the stop price matters.
bool CandleTakerBreakEven(const double gross, const double costs, const double slope,
                          const double exit_price, const int direction, const bool inverse,
                          double &price)
{
   double net = gross + costs;
   if(slope <= 0.0 || exit_price <= 0.0 || net <= 0.0 ||
      (direction != 1 && direction != -1) || !MathIsValidNumber(slope) ||
      !MathIsValidNumber(exit_price) || !MathIsValidNumber(gross) || !MathIsValidNumber(costs)) return false;
   double distance = net / slope;
   if(inverse)
   {
      double denominator = slope * exit_price + direction * costs;
      if(denominator <= 0.0 || !MathIsValidNumber(denominator)) return false;
      distance = net * exit_price / denominator;
   }
   price = exit_price - direction * distance;
   return price > 0.0 && MathIsValidNumber(price);
}

double CandleTakerRoundPrice(const double price, const double tick_size, const bool upward)
{
   if(tick_size <= 0.0) return 0.0;
   double units = price / tick_size;
   return (upward ? MathCeil(units - 1e-8) : MathFloor(units + 1e-8)) * tick_size;
}

long CandleTakerFrontier(const double entry_price, const double distance,
                     const double grid_step, const int direction)
{
   double boundary = entry_price + direction * distance;
   if(direction > 0) return (long)MathCeil(boundary / grid_step - 1e-8);
   return (long)MathFloor(boundary / grid_step + 1e-8);
}
#endif
