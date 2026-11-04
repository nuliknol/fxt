#ifndef SCRUBBER_PROFIT_MATH_MQH
#define SCRUBBER_PROFIT_MATH_MQH
#define SCRUBBER_CAPACITY 1024

// Pure per-position numerical code, compiled unchanged in the native tests.
bool ScrubberMeetsFloor(const double net, const double slope, const double minimum_move)
{
   if(net <= 0.0 || !MathIsValidNumber(net)) return false;
   if(minimum_move <= 0.0) return true;
   if(slope <= 0.0 || !MathIsValidNumber(slope)) return false;
   double required = minimum_move * slope;
   return net >= required - required * 1e-9;
}

bool ScrubberDrawdownHit(const double net, const double peak, const double percent)
{
   return peak > 0.0 && net <= peak * (1.0 - percent / 100.0);
}

double ScrubberProjectedNet(const double gross, const double costs, const double slope,
                           const double exit_price, const int direction,
                           const bool inverse, const double price)
{
   double projected = gross + direction * (price - exit_price) * slope;
   if(inverse && price > 0.0) projected *= exit_price / price;
   return projected + costs;
}

// Solve one order's projected net profit = target, with no eligibility cutoffs.
// When FX base == deposit currency, conversion at the candidate exit matters.
bool ScrubberPositionStop(const double gross, const double costs, const double slope,
                          const double exit_price, const int direction, const bool inverse,
                          const double target, double &price)
{
   double net = gross + costs;
   if(slope <= 0.0 || exit_price <= 0.0 || target <= 0.0 || net <= target ||
      !MathIsValidNumber(slope) || !MathIsValidNumber(net) || !MathIsValidNumber(target)) return false;
   double distance = (net - target) / slope;
   if(inverse)
   {
      double denominator = slope * exit_price - direction * (target - costs);
      if(denominator <= 0.0) return false;
      distance = (net - target) * exit_price / denominator;
   }
   price = exit_price - direction * distance;
   return price > 0.0 && MathIsValidNumber(price);
}

double ScrubberRoundPrice(const double price, const double tick_size, const bool upward)
{
   if(tick_size <= 0.0) return 0.0;
   double units = price / tick_size;
   return (upward ? MathCeil(units - 1e-8) : MathFloor(units + 1e-8)) * tick_size;
}

long ScrubberFrontier(const double entry_price, const double distance,
                     const double grid_step, const int direction)
{
   double boundary = entry_price + direction * distance;
   if(direction > 0) return (long)MathCeil(boundary / grid_step - 1e-8);
   return (long)MathFloor(boundary / grid_step + 1e-8);
}
#endif
