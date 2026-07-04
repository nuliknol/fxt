#include "core_api.h"
#include "internal_store.h"

#include <stdlib.h>

static int32_t max_i32(int32_t a, int32_t b)
{
	return (a > b) ? a : b;
}

static int32_t min_i32(int32_t a, int32_t b)
{
	return (a < b) ? a : b;
}

int core_build_candle_features(uint32_t dataset_id)
{
	DatasetRecord *rec;
	CandleFeat *features;
	uint32_t i;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -1;
	}
	if (rec->candles == NULL || rec->meta.candle_count == 0) {
		return -2;
	}
	free(rec->features);
	rec->features = NULL;

	features = (CandleFeat *)malloc(rec->meta.candle_count * sizeof(CandleFeat));
	if (features == NULL) {
		return -3;
	}

	for (i = 0; i < rec->meta.candle_count; ++i) {
		const Candle *c = &rec->candles[i];
		int32_t mx = max_i32(c->open_pips, c->close_pips);
		int32_t mn = min_i32(c->open_pips, c->close_pips);
		features[i].u = c->high_pips - mx;
		features[i].l = mn - c->low_pips;
		features[i].b = c->close_pips - c->open_pips;
		features[i].r = c->high_pips - c->low_pips;
	}

	rec->features = features;
	rec->meta.feature_count = rec->meta.candle_count;
	return 0;
}
