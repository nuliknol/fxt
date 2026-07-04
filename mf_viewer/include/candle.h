#ifndef CANDLE_H
#define CANDLE_H

#include <cstdint>

struct Candle {
	double	open;
	double	high;
	double	low;
	double	close;
	double	volume;		// if available
	int64_t	timestamp_ms;
};

#endif // CANDLE_H
