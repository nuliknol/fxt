#include "micro_harvest.h"

#include <assert.h>
#include <string.h>

static WaveToken make_wave(uint32_t start_candle, uint32_t end_candle, uint32_t class_id)
{
	WaveToken tok;

	memset(&tok, 0, sizeof(tok));
	tok.start_candle = start_candle;
	tok.end_candle = end_candle;
	tok.class_id = class_id;
	tok.duration_candles = end_candle - start_candle + 1;
	return tok;
}

static MicroHarvestSearchConfig base_config(int32_t bias)
{
	MicroHarvestSearchConfig cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.bias = bias;
	cfg.lookback_days = 14;
	cfg.spread_pips = 0;
	cfg.max_orders_min = 1;
	cfg.max_orders_max = 1;
	cfg.spacing_min_pips = 5;
	cfg.spacing_max_pips = 5;
	cfg.spacing_step_pips = 5;
	cfg.take_profit_min_pips = 4;
	cfg.take_profit_max_pips = 4;
	cfg.take_profit_step_pips = 4;
	cfg.entry_edge_min = 500;
	cfg.entry_edge_max = 500;
	cfg.entry_edge_step = 1;
	cfg.min_island_height_pips = 1;
	cfg.max_island_height_pips = 1000;
	return cfg;
}

static void test_buy_uses_intrabar_extremes(void)
{
	Candle candles[] = {
		{1, 100, 100, 100, 100, 0},
		{2, 100, 104, 95, 96, 0},
		{3, 96, 101, 96, 97, 0}
	};
	WaveToken waves[] = { make_wave(0, 2, RELATIVE_CLASS_RANGE) };
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_BUY);
	MicroHarvestResult result;
	int rc;

	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 3, waves, 1, &cfg, &result);
	assert(rc == 0);
	assert(result.total_closed_trade_count == 1);
	assert(result.total_profit_pips == 4);
	assert(result.trades[0].open_price_pips == 99);
	assert(result.trades[0].close_price_pips == 103);
	micro_harvest_result_free(&result);
}

static void test_sell_uses_intrabar_extremes(void)
{
	Candle candles[] = {
		{1, 100, 100, 100, 100, 0},
		{2, 100, 105, 96, 104, 0},
		{3, 104, 104, 99, 103, 0}
	};
	WaveToken waves[] = { make_wave(0, 2, RELATIVE_CLASS_RANGE) };
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_SELL);
	MicroHarvestResult result;
	int rc;

	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 3, waves, 1, &cfg, &result);
	assert(rc == 0);
	assert(result.total_closed_trade_count == 1);
	assert(result.total_profit_pips == 4);
	assert(result.trades[0].open_price_pips == 101);
	assert(result.trades[0].close_price_pips == 97);
	micro_harvest_result_free(&result);
}

static void test_same_candle_laddering_tracks_peak_concurrency(void)
{
	Candle candles[] = {
		{1, 100, 100, 100, 100, 0},
		{2, 100, 106, 90, 100, 0}
	};
	WaveToken waves[] = { make_wave(0, 1, RELATIVE_CLASS_RANGE) };
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_BUY);
	MicroHarvestResult result;
	int rc;

	cfg.max_orders_min = 2;
	cfg.max_orders_max = 2;
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 2, waves, 1, &cfg, &result);
	assert(rc == 0);
	assert(result.total_closed_trade_count == 2);
	assert(result.total_profit_pips == 8);
	assert(result.total_max_concurrent_open_trade_count == 2);
	assert(result.islands[0].max_concurrent_open_trade_count == 2);
	micro_harvest_result_free(&result);
}

static void test_unclosed_orders_are_reported(void)
{
	Candle candles[] = {
		{1, 100, 100, 100, 100, 0},
		{2, 100, 100, 94, 95, 0},
		{3, 95, 95, 94, 95, 0}
	};
	WaveToken waves[] = { make_wave(0, 2, RELATIVE_CLASS_RANGE) };
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_BUY);
	MicroHarvestResult result;
	int rc;

	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 3, waves, 1, &cfg, &result);
	assert(rc == 0);
	assert(result.total_profit_pips == 0);
	assert(result.total_ignored_open_trade_count == 0);
	assert(result.selected_island_count == 0);
	assert(result.island_count == 1);
	assert(result.islands[0].ignored_open_trade_count == 1);
	assert(result.trade_count == 0);
	micro_harvest_result_free(&result);
}

static void test_weak_trend_bridge_creates_single_selected_zone(void)
{
	Candle candles[] = {
		{1, 100, 105, 95, 100, 0},
		{2, 100, 104, 96, 100, 0},
		{3, 100, 103, 98, 102, 0},
		{4, 102, 105, 96, 100, 0},
		{5, 100, 104, 95, 99, 0}
	};
	WaveToken waves[] = {
		make_wave(0, 1, RELATIVE_CLASS_RANGE),
		make_wave(2, 2, RELATIVE_CLASS_TREND),
		make_wave(3, 4, RELATIVE_CLASS_RANGE)
	};
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_SELL);
	MicroHarvestResult result;
	int rc;
	int found_bridge = 0;
	int i;

	waves[1].net_move_raw = 2;
	waves[1].relative_directional_efficiency_step = 300;
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 5, waves, 3, &cfg, &result);
	assert(rc == 0);
	for (i = 0; i < result.island_count; ++i) {
		if (result.islands[i].start_wave_index == 0 &&
			result.islands[i].end_wave_index == 2) {
			found_bridge = 1;
		}
	}
	assert(found_bridge == 1);
	micro_harvest_result_free(&result);
}

static void test_sell_bias_can_trade_bear_trend_wave(void)
{
	Candle candles[] = {
		{1, 100, 108, 99, 107, 0},
		{2, 107, 112, 101, 102, 0},
		{3, 102, 104, 96, 98, 0}
	};
	WaveToken waves[] = { make_wave(0, 2, RELATIVE_CLASS_TREND) };
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_SELL);
	MicroHarvestResult result;
	int rc;

	waves[0].dominant_direction = MH_BIAS_SELL;
	waves[0].net_move_raw = -25;
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 3, waves, 1, &cfg, &result);
	assert(rc == 0);
	assert(result.island_count == 1);
	assert(result.islands[0].selected == 1);
	assert(result.total_closed_trade_count > 0);
	micro_harvest_result_free(&result);
}

static void test_signal_gate_is_reported_and_used(void)
{
	Candle candles[] = {
		{1, 100, 104, 95, 96, 0},
		{2, 96, 103, 95, 99, 0},
		{3, 99, 106, 98, 105, 0},
		{4, 105, 105, 100, 101, 0}
	};
	WaveToken waves[] = {
		make_wave(0, 1, RELATIVE_CLASS_RANGE),
		make_wave(2, 3, RELATIVE_CLASS_RANGE)
	};
	MicroHarvestSearchConfig cfg = base_config(MH_BIAS_BUY);
	MicroHarvestResult result;
	int rc;

	waves[0].boundary_confidence = 100;
	waves[1].boundary_confidence = 900;
	cfg.take_profit_min_pips = 4;
	cfg.take_profit_max_pips = 4;
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run(candles, 4, waves, 2, &cfg, &result);
	assert(rc == 0);
	assert(result.recommended_params.signal_feature_id >= 0);
	if (result.recommended_params.signal_feature_id == 5 && result.recommended_params.signal_compare_op > 0) {
		assert(result.recommended_params.signal_threshold >= 100);
	}
	micro_harvest_result_free(&result);
}

int main(void)
{
	test_buy_uses_intrabar_extremes();
	test_sell_uses_intrabar_extremes();
	test_same_candle_laddering_tracks_peak_concurrency();
	test_unclosed_orders_are_reported();
	test_weak_trend_bridge_creates_single_selected_zone();
	test_sell_bias_can_trade_bear_trend_wave();
	test_signal_gate_is_reported_and_used();
	return 0;
}
