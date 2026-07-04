#include "cluster_graph.h"
#include "core_api.h"
#include "gpu_dispatch.h"
#include "internal_store.h"
#include "micro_harvest.h"
#include "wave_predictor.h"
#include "wave_dp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif

static void test_pipeline(void)
{
	LoadOptions opt;
	uint32_t dataset_id;
	DatasetMeta meta;
	GlobalStore *s;
	DistanceConfig cfg;
	float dist;
	int rc;
	char csv_path[1024];

	rc = core_init("trainer_test_data");
	assert(rc == 0);

	opt.pip_scale = 10000;
	opt.sort_on_load = 1;
	opt.dedupe_timestamps = 1;
	snprintf(csv_path, sizeof(csv_path), "%s/work/synthetic_mt4.csv", TEST_SOURCE_DIR);
	rc = core_import_mt4_csv(csv_path, &opt, &dataset_id);
	assert(rc == 0);

	rc = core_build_candle_features(dataset_id);
	assert(rc == 0);

	rc = core_build_character_blocks(dataset_id, 3);
	assert(rc == 0);

	rc = core_get_dataset_meta(dataset_id, &meta);
	assert(rc == 0);
	assert(meta.candle_count == 8);
	assert(meta.feature_count == 8);
	assert(meta.char_block_count == 6);
	assert(meta.q == 3);

	s = store_get();
	assert(s != NULL);
	cfg.delta = 0;
	cfg.mirrored_mode = 0;
	cfg.weighted = 0;
	cfg.w_upper = 1.0f;
	cfg.w_lower = 1.0f;
	cfg.w_body = 1.0f;
	cfg.w_range = 1.0f;
	rc = core_distance_block_pair(&s->datasets[0].chars, 0, 1, &cfg, &dist);
	assert(rc == 0);
	assert(dist >= 0.0f);

	core_shutdown();
}

static DatasetRecord *create_char_dataset(uint32_t q, uint32_t n_blocks, const int32_t *data)
{
	DatasetRecord *rec;
	uint32_t dataset_id;
	size_t total = (size_t)n_blocks * (size_t)(q * 4u);

	rec = store_alloc_dataset(&dataset_id);
	assert(rec != NULL);
	rec->meta.dataset_id = dataset_id;
	rec->meta.candle_count = n_blocks + q - 1u;
	rec->meta.feature_count = rec->meta.candle_count;
	rec->meta.char_block_count = n_blocks;
	rec->meta.q = q;
	rec->chars.n_blocks = n_blocks;
	rec->chars.q = q;
	rec->chars.dim = q * 4u;
	rec->chars.data = (int32_t *)malloc(total * sizeof(*rec->chars.data));
	assert(rec->chars.data != NULL);
	memcpy(rec->chars.data, data, total * sizeof(*data));
	return rec;
}

static DatasetRecord *create_wave_dataset(uint32_t candle_count, const Candle *candles, const CandleFeat *features)
{
	DatasetRecord *rec;
	uint32_t dataset_id;

	rec = store_alloc_dataset(&dataset_id);
	assert(rec != NULL);
	rec->meta.dataset_id = dataset_id;
	rec->meta.candle_count = candle_count;
	rec->meta.feature_count = candle_count;
	rec->candles = (Candle *)malloc((size_t)candle_count * sizeof(*rec->candles));
	rec->features = (CandleFeat *)malloc((size_t)candle_count * sizeof(*rec->features));
	assert(rec->candles != NULL);
	assert(rec->features != NULL);
	memcpy(rec->candles, candles, (size_t)candle_count * sizeof(*candles));
	memcpy(rec->features, features, (size_t)candle_count * sizeof(*features));
	return rec;
}

static GraphParams default_graph_params(uint32_t q)
{
	GraphParams params;

	memset(&params, 0, sizeof(params));
	params.q = q;
	params.distance_mode = DISTANCE_MODE_FEATURE_AWARE;
	params.delta = 0;
	params.theta = 2;
	params.mirrored_mode = 0;
	params.weighted = 1;
	params.delta_body_gate = 8;
	params.delta_range_gate = 8;
	params.delta_bullcount_gate = 2;
	params.delta_signchange_gate = 2;
	params.delta_upper_wick_gate = 8;
	params.delta_lower_wick_gate = 8;
	params.delta_lower_ratio_gate = 25;
	params.delta_upper_ratio_gate = 25;
	params.delta_total_wick_ratio_gate = 30;
	params.k_neighbors = 2;
	params.use_mutual_knn = 1;
	params.max_neighbor_distance = 0.0f;
	params.cluster_mode = GRAPH_CLUSTER_MODE_LABEL_PROPAGATION;
	params.body_mode = 0;
	params.require_same_majority_sign = 0;
	params.use_upper = 1;
	params.use_lower = 1;
	params.use_range = 1;
	params.use_body = 1;
	params.use_composition = 1;
	params.w_upper = 1.0f;
	params.w_lower = 1.0f;
	params.w_body = 2.0f;
	params.w_range = 2.0f;
	params.body_sign_penalty = 2.0f;
	params.w_bullcount = 2.0f;
	params.w_signchanges = 2.0f;
	params.w_runbull = 2.0f;
	params.w_runbear = 2.0f;
	return params;
}

static void test_graph_scale_separation(void)
{
	static const int32_t blocks[] = {
		0, 0, 1, 2,
		0, 0, 2, 3,
		0, 0, 10, 12,
		0, 0, 11, 13,
		0, 0, 100, 102,
		0, 0, 101, 103
	};
	GraphParams params = default_graph_params(1);
	DatasetRecord *rec;
	char err[256];
	float progress = 0.0f;
	int rc;

	rc = core_init("trainer_test_graph_scales");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_char_dataset(1, 6, blocks);
	params.delta_body_gate = 3;
	params.delta_range_gate = 3;
	params.k_neighbors = 2;

	rc = cluster_graph_run(rec->meta.dataset_id, &params, &progress, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(progress == 1.0f);
	assert(rec->clustering.cluster_count == 3);
	assert(rec->clustering.cluster_ids[0] == rec->clustering.cluster_ids[1]);
	assert(rec->clustering.cluster_ids[2] == rec->clustering.cluster_ids[3]);
	assert(rec->clustering.cluster_ids[4] == rec->clustering.cluster_ids[5]);
	assert(rec->clustering.cluster_ids[0] != rec->clustering.cluster_ids[2]);
	assert(rec->clustering.cluster_ids[2] != rec->clustering.cluster_ids[4]);
	assert(rec->clustering.graph_diag.n_edges == 3);
	assert(rec->clustering.graph_diag.percolated == 0);

	core_shutdown();
}

static void test_wave_inference_pipeline(void)
{
	LoadOptions opt;
	uint32_t dataset_id;
	WaveSummary summary;
	WaveToken tokens[64];
	uint32_t written = 0;
	uint32_t total = 0;
	WaveParams params;
	int rc;
	char err[256];
	char csv_path[1024];

	rc = core_init("trainer_test_wave_inference");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	opt.pip_scale = 10000;
	opt.sort_on_load = 1;
	opt.dedupe_timestamps = 1;
	snprintf(csv_path, sizeof(csv_path), "%s/work/synthetic_mt4.csv", TEST_SOURCE_DIR);
	rc = core_import_mt4_csv(csv_path, &opt, &dataset_id);
	assert(rc == 0);

	memset(&params, 0, sizeof(params));
	params.lmin_candles = 1;
	params.lmax_candles = 4;
	params.regime_context_window = 6;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 50;

	rc = wave_dp_run(dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_summary(dataset_id, &summary);
	assert(rc == 0);
	assert(summary.token_count > 0);
	assert(summary.total_fit_error > 0);
	rc = core_get_wave_tokens(dataset_id, 0, 64, tokens, &written, &total);
	assert(rc == 0);
	assert(written == total);
	assert(total == summary.token_count);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[written - 1u].end_candle == 7u);

	core_shutdown();
}

static void test_primitive_wave_ignores_inside_candle_break(void)
{
	static const Candle candles[] = {
		{0, 100, 111, 99, 110, 0},
		{1, 110, 110, 108, 109, 0},
		{2, 109, 120, 109, 119, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 10, 12},
		{0, 1, -1, 2},
		{1, 0, 10, 11}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_inside_candle");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(3, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 3;
	params.lmax_candles = 3;
	params.regime_context_window = 3;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[0].end_candle == 2u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);

	core_shutdown();
}

static void test_primitive_wave_splits_breakout_tail(void)
{
	static const Candle candles[] = {
		{0, 100, 102, 98, 98, 0},
		{1, 98, 99, 97, 97, 0},
		{2, 97, 98, 92, 92, 0},
		{3, 92, 96, 92, 95, 0},
		{4, 95, 98, 94, 94, 0},
		{5, 94, 97, 92, 93, 0},
		{6, 93, 93, 86, 91, 0},
		{7, 91, 96, 89, 95, 0},
		{8, 95, 107, 94, 107, 0},
		{9, 108, 109, 103, 109, 0}
	};
	static const CandleFeat features[] = {
		{2, 0, -2, 4},
		{1, 0, -1, 2},
		{1, 0, -5, 6},
		{1, 0, 3, 4},
		{3, 0, -1, 4},
		{3, 1, -1, 5},
		{0, 5, -2, 7},
		{1, 2, 4, 7},
		{0, 1, 12, 13},
		{0, 5, 1, 6}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_breakout_tail");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(10, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 10;
	params.lmax_candles = 10;
	params.regime_context_window = 10;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 2u);
	assert(total == 2u);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[0].end_candle == 6u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[0].dominant_direction == -1);
	assert(tokens[1].start_candle == 7u);
	assert(tokens[1].end_candle == 9u);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[1].dominant_direction == 1);

	core_shutdown();
}

static void test_primitive_wave_merges_separator_debug_flag(void)
{
	static const Candle candles[] = {
		{0, 100, 102, 100, 102, 0},
		{1, 102, 102, 100, 100, 0},
		{2, 100, 101, 99, 100, 0}
	};
	static const CandleFeat features[] = {
		{0, 0, 2, 2},
		{0, 0, -2, 2},
		{1, 1, 0, 2}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_separator_merge");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(3, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 3;
	params.regime_context_window = 3;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_ALTERNATING);

	core_shutdown();
}

static void test_primitive_wave_collapses_separator_run(void)
{
	static const Candle candles[] = {
		{0, 100, 101, 99, 100, 0},
		{1, 100, 101, 99, 100, 0},
		{2, 100, 101, 99, 100, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 0, 2},
		{1, 1, 0, 2},
		{1, 1, 0, 2}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_separator_run");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(3, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 3;
	params.regime_context_window = 3;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].class_id != PRIMITIVE_CLASS_SEPARATOR);

	core_shutdown();
}

static void test_primitive_wave_tolerates_single_counter_candle(void)
{
	static const Candle candles[] = {
		{0, 100, 105, 99, 104, 0},
		{1, 104, 108, 103, 107, 0},
		{2, 107, 110, 106, 109, 0},
		{3, 109, 110, 106, 107, 0},
		{4, 107, 112, 107, 111, 0},
		{5, 111, 115, 110, 114, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, 2, 4},
		{1, 1, -2, 4},
		{1, 0, 4, 5},
		{1, 1, 3, 5}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_counter_candle");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(6, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 6;
	params.lmax_candles = 6;
	params.regime_context_window = 6;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[0].primitive_consecutive_ratio == 1000);
	assert(tokens[0].primitive_alternating_ratio == 0);
	assert(tokens[0].primitive_max_consecutive_run == 5);

	core_shutdown();
}

static void test_primitive_wave_rejects_counter_candle_level_break(void)
{
	static const Candle candles[] = {
		{0, 100, 105, 99, 104, 0},
		{1, 104, 108, 103, 107, 0},
		{2, 107, 110, 106, 109, 0},
		{3, 109, 110, 104, 105, 0},
		{4, 105, 112, 105, 111, 0},
		{5, 111, 115, 110, 114, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, 2, 4},
		{1, 1, -4, 6},
		{1, 0, 6, 7},
		{1, 1, 3, 5}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_counter_break");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(6, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 6;
	params.lmax_candles = 6;
	params.regime_context_window = 6;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 2u);
	assert(total == 2u);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[0].end_candle == 2u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[0].dominant_direction == 1);
	assert(tokens[1].start_candle == 3u);
	assert(tokens[1].end_candle == 5u);

	core_shutdown();
}

static void test_primitive_wave_treats_doji_as_neutral(void)
{
	static const Candle candles[] = {
		{0, 100, 105, 99, 104, 0},
		{1, 104, 108, 103, 107, 0},
		{2, 107, 110, 106, 109, 0},
		{3, 109, 110, 106, 107, 0},
		{4, 107, 108, 107, 107, 0},
		{5, 107, 112, 107, 111, 0},
		{6, 111, 115, 110, 114, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, 2, 4},
		{1, 1, -2, 4},
		{1, 0, 0, 1},
		{1, 0, 4, 5},
		{1, 1, 3, 5}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_doji_neutral");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(7, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 7;
	params.lmax_candles = 7;
	params.regime_context_window = 7;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);

	core_shutdown();
}

static void test_primitive_wave_ignores_neutral_bridge_in_primary_dp(void)
{
	static const Candle candles[] = {
		{0, 5977, 5980, 5977, 5980, 0},
		{1, 5980, 5984, 5980, 5984, 0},
		{2, 5984, 5984, 5983, 5983, 0},
		{3, 5984, 5984, 5984, 5984, 0},
		{4, 5984, 5988, 5984, 5988, 0},
		{5, 5988, 5988, 5988, 5988, 0},
		{6, 5988, 5990, 5988, 5989, 0}
	};
	static const CandleFeat features[] = {
		{0, 0, 3, 3},
		{0, 0, 4, 4},
		{0, 0, -1, 1},
		{0, 0, 0, 0},
		{0, 0, 4, 4},
		{0, 0, 0, 0},
		{1, 0, 1, 2}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_neutral_bridge");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(7, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 7;
	params.lmax_candles = 7;
	params.regime_context_window = 7;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[0].end_candle == 6u);
	assert(tokens[0].primitive_consecutive_ratio == 1000);
	assert(tokens[0].primitive_counter_body_ratio == 0);
	assert(tokens[0].primitive_minority_body_ratio == 0);

	core_shutdown();
}

static void test_primitive_wave_live_prefix_neutral_bridge(void)
{
	static const Candle candles[] = {
		{0, 5984, 5985, 5966, 5978, 38},
		{1, 5978, 5978, 5975, 5977, 22},
		{2, 5977, 5980, 5977, 5980, 8},
		{3, 5980, 5984, 5980, 5984, 25},
		{4, 5984, 5984, 5983, 5983, 9},
		{5, 5984, 5984, 5984, 5984, 7},
		{6, 5984, 5988, 5984, 5988, 16},
		{7, 5988, 5988, 5988, 5988, 9},
		{8, 5988, 5990, 5988, 5989, 8},
		{9, 5989, 5990, 5986, 5986, 22}
	};
	static const CandleFeat features[] = {
		{1, 12, -6, 19},
		{0, 2, -1, 3},
		{0, 0, 3, 3},
		{0, 0, 4, 4},
		{0, 0, -1, 1},
		{0, 0, 0, 0},
		{0, 0, 4, 4},
		{0, 0, 0, 0},
		{1, 0, 1, 2},
		{1, 0, -3, 4}
	};
	DatasetRecord *rec;
	WaveToken tokens[16];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_live_prefix");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(10, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 30;
	params.regime_context_window = 64;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 220;
	params.trend_priority_mode = 1;
	params.retrace_break_threshold = 200;
	params.trend_split_gain_min = 80;
	params.compression_min_duration = 4;
	params.expansion_min_duration = 4;
	params.v_reversal_min_trend_strength = 420;
	params.v_reversal_max_bridge_chop = 520;
	params.v_reversal_max_duration = 12;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 16, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 3u);
	assert(total == 3u);
	assert(tokens[1].start_candle == 2u);
	assert(tokens[1].end_candle == 8u);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);

	core_shutdown();
}

static void test_primitive_wave_splits_internal_trend_with_neutral_break(void)
{
	static const Candle candles[] = {
		{0, 6002, 6004, 6000, 6000, 233},
		{1, 6000, 6001, 5999, 5999, 208},
		{2, 5999, 6000, 5994, 5994, 241},
		{3, 5994, 5998, 5994, 5997, 332},
		{4, 5998, 6001, 5997, 5997, 385},
		{5, 5997, 6000, 5995, 5996, 376},
		{6, 5996, 5996, 5989, 5994, 635},
		{7, 5994, 5999, 5992, 5998, 480},
		{8, 5999, 6011, 5998, 6011, 594},
		{9, 6012, 6013, 6007, 6013, 543},
		{10, 6013, 6015, 6011, 6012, 540},
		{11, 6012, 6015, 6011, 6011, 457},
		{12, 6012, 6015, 6010, 6011, 428},
		{13, 6011, 6014, 6010, 6014, 455}
	};
	static const CandleFeat features[] = {
		{2, 0, -2, 4},
		{1, 0, -1, 2},
		{1, 0, -5, 6},
		{1, 0, 3, 4},
		{3, 0, -1, 4},
		{3, 1, -1, 5},
		{0, 5, -2, 7},
		{1, 2, 4, 7},
		{0, 1, 12, 13},
		{0, 5, 1, 6},
		{2, 1, -1, 4},
		{3, 0, -1, 4},
		{3, 1, -1, 5},
		{0, 1, 3, 4}
	};
	DatasetRecord *rec;
	WaveToken tokens[16];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_internal_trend");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(14, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 30;
	params.regime_context_window = 64;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 220;
	params.trend_priority_mode = 1;
	params.retrace_break_threshold = 200;
	params.trend_split_gain_min = 80;
	params.compression_min_duration = 4;
	params.expansion_min_duration = 4;
	params.v_reversal_min_trend_strength = 420;
	params.v_reversal_max_bridge_chop = 520;
	params.v_reversal_max_duration = 12;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 16, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 3u);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[0].end_candle == 6u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[0].dominant_direction == -1);
	assert(tokens[1].start_candle == 7u);
	assert(tokens[1].end_candle == 9u);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[1].dominant_direction == 1);
	assert(tokens[2].start_candle == 10u);
	assert(tokens[2].end_candle == 13u);
	assert(tokens[2].class_id == PRIMITIVE_CLASS_ALTERNATING);

	core_shutdown();
}

static void test_primitive_wave_extends_across_terminal_pullback_confirmation(void)
{
	static const Candle candles[] = {
		{0, 5993, 5996, 5982, 5995, 744},
		{1, 5995, 5996, 5986, 5992, 603},
		{2, 5992, 5995, 5990, 5991, 551},
		{3, 5991, 5993, 5977, 5991, 717},
		{4, 5991, 5998, 5989, 5995, 639},
		{5, 5995, 6005, 5995, 6001, 594},
		{6, 6001, 6009, 5999, 6002, 718},
		{7, 6002, 6014, 6002, 6012, 638},
		{8, 6012, 6018, 6012, 6017, 492},
		{9, 6017, 6017, 6012, 6014, 472},
		{10, 6014, 6016, 6010, 6015, 469},
		{11, 6015, 6017, 6014, 6014, 403},
		{12, 6014, 6020, 6014, 6018, 462},
		{13, 6017, 6022, 6016, 6018, 410}
	};
	static const CandleFeat features[] = {
		{1, 11, 2, 14},
		{1, 6, -3, 10},
		{3, 1, -1, 5},
		{2, 14, 0, 16},
		{3, 2, 4, 9},
		{4, 0, 6, 10},
		{7, 2, 1, 10},
		{2, 0, 10, 12},
		{1, 0, 5, 6},
		{0, 2, -3, 5},
		{1, 4, 1, 6},
		{2, 0, -1, 3},
		{2, 0, 4, 6},
		{4, 1, 1, 6}
	};
	DatasetRecord *rec;
	WaveToken tokens[16];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_terminal_pullback");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(14, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 30;
	params.regime_context_window = 64;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 220;
	params.trend_priority_mode = 1;
	params.retrace_break_threshold = 200;
	params.trend_split_gain_min = 80;
	params.compression_min_duration = 4;
	params.expansion_min_duration = 4;
	params.v_reversal_min_trend_strength = 420;
	params.v_reversal_max_bridge_chop = 520;
	params.v_reversal_max_duration = 12;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 16, tokens, &written, &total);
	assert(rc == 0);
	assert(written == 1u);
	assert(total == 1u);
	assert(tokens[0].start_candle == 0u);
	assert(tokens[0].end_candle == 13u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[0].dominant_direction == 1);

	core_shutdown();
}

static void test_primitive_wave_expands_consecutive_boundary(void)
{
	static const Candle candles[] = {
		{0, 100, 103, 99, 99, 0},
		{1, 99, 100, 98, 98, 0},
		{2, 98, 102, 98, 101, 0},
		{3, 101, 104, 101, 103, 0},
		{4, 103, 103, 100, 102, 0},
		{5, 102, 102, 98, 98, 0},
		{6, 98, 103, 98, 102, 0},
		{7, 102, 103, 96, 96, 0},
		{8, 96, 96, 93, 94, 0},
		{9, 94, 95, 90, 92, 0},
		{10, 92, 92, 89, 90, 0},
		{11, 90, 90, 87, 88, 0}
	};
	static const CandleFeat features[] = {
		{0, 1, -1, 4},
		{1, 0, -1, 2},
		{1, 0, 3, 4},
		{1, 0, 2, 3},
		{0, 2, -1, 3},
		{0, 0, -4, 4},
		{1, 0, 4, 5},
		{1, 0, -6, 7},
		{0, 1, -2, 3},
		{1, 0, -2, 5},
		{0, 1, -2, 3},
		{0, 1, -2, 3}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_boundary_expand");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(12, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 12;
	params.regime_context_window = 12;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written >= 2u);
	assert(tokens[0].class_id == PRIMITIVE_CLASS_ALTERNATING);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[1].start_candle == 7u);

	core_shutdown();
}

static void test_primitive_wave_extends_across_leading_inside_edge(void)
{
	static const Candle candles[] = {
		{0, 110, 112, 108, 108, 0},
		{1, 108, 109, 106, 107, 0},
		{2, 107, 110, 106, 109, 0},
		{3, 109, 111, 108, 110, 0},
		{4, 110, 113, 109, 112, 0},
		{5, 112, 113, 109, 109, 0},
		{6, 109, 110, 106, 106, 0},
		{7, 106, 108, 103, 104, 0},
		{8, 104, 105, 101, 102, 0},
		{9, 102, 103, 99, 100, 0}
	};
	static const CandleFeat features[] = {
		{0, 2, -2, 4},
		{1, 1, -1, 3},
		{1, 1, 2, 4},
		{1, 1, 1, 3},
		{1, 1, 2, 4},
		{1, 0, -3, 4},
		{1, 0, -3, 4},
		{2, 1, -2, 5},
		{1, 1, -2, 4},
		{1, 1, -2, 4}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_inside_edge");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(10, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 10;
	params.regime_context_window = 10;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 10;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written >= 2u);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[1].start_candle == 5u);
	assert(tokens[1].dominant_direction == -1);

	core_shutdown();
}

static void test_primitive_wave_rejects_opposite_leading_edge(void)
{
	static const Candle candles[] = {
		{0, 6022, 6025, 6020, 6024, 338},
		{1, 6024, 6027, 6022, 6025, 363},
		{2, 6025, 6031, 6025, 6028, 405},
		{3, 6028, 6032, 6027, 6029, 402},
		{4, 6029, 6031, 6029, 6031, 412},
		{5, 6031, 6032, 6029, 6030, 354},
		{6, 6031, 6032, 6027, 6027, 354},
		{7, 6026, 6027, 6022, 6023, 394},
		{8, 6023, 6023, 6020, 6022, 339},
		{9, 6022, 6026, 6022, 6025, 396}
	};
	static const CandleFeat features[] = {
		{1, 2, 2, 5},
		{2, 2, 1, 5},
		{3, 0, 3, 6},
		{3, 1, 1, 5},
		{0, 0, 2, 2},
		{1, 1, -1, 3},
		{1, 0, -4, 5},
		{1, 1, -3, 5},
		{0, 2, -1, 3},
		{1, 0, 3, 4}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_leading_edge_dir");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(10, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 30;
	params.regime_context_window = 64;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 220;
	params.trend_priority_mode = 1;
	params.retrace_break_threshold = 200;
	params.trend_split_gain_min = 80;
	params.compression_min_duration = 4;
	params.expansion_min_duration = 4;
	params.v_reversal_min_trend_strength = 420;
	params.v_reversal_max_bridge_chop = 520;
	params.v_reversal_max_duration = 12;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written >= 3u);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[1].dominant_direction == 1);
	assert(tokens[2].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[2].dominant_direction == -1);
	assert(tokens[2].start_candle == 6u);

	core_shutdown();
}

static void test_primitive_wave_rejects_bearish_lead_for_bull_run(void)
{
	static const Candle candles[] = {
		{0, 5978, 5978, 5975, 5977, 22},
		{1, 5977, 5980, 5977, 5980, 8},
		{2, 5980, 5984, 5980, 5984, 25},
		{3, 5984, 5984, 5983, 5983, 9},
		{4, 5984, 5984, 5984, 5984, 7},
		{5, 5984, 5988, 5984, 5988, 16},
		{6, 5988, 5988, 5988, 5988, 9},
		{7, 5988, 5990, 5988, 5989, 8},
		{8, 5989, 5990, 5986, 5986, 22}
	};
	static const CandleFeat features[] = {
		{0, 2, -1, 3},
		{0, 0, 3, 3},
		{0, 0, 4, 4},
		{0, 0, -1, 1},
		{0, 0, 0, 0},
		{0, 0, 4, 4},
		{0, 0, 0, 0},
		{1, 0, 1, 2},
		{1, 0, -3, 4}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params;
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_wave_bull_lead_reject");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_wave_dataset(9, candles, features);
	assert(rec != NULL);

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_PRIMITIVE;
	params.lmin_candles = 1;
	params.lmax_candles = 30;
	params.regime_context_window = 64;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 220;
	params.trend_priority_mode = 1;
	params.retrace_break_threshold = 200;
	params.trend_split_gain_min = 80;
	params.compression_min_duration = 4;
	params.expansion_min_duration = 4;
	params.v_reversal_min_trend_strength = 420;
	params.v_reversal_max_bridge_chop = 520;
	params.v_reversal_max_duration = 12;

	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(written >= 2u);
	assert(tokens[0].class_id != PRIMITIVE_CLASS_CONSECUTIVE || tokens[0].dominant_direction != 1);
	assert(tokens[1].class_id == PRIMITIVE_CLASS_CONSECUTIVE);
	assert(tokens[1].dominant_direction == 1);
	assert(tokens[1].start_candle == 1u);

	core_shutdown();
}

static void test_graph_bridge_chain_vs_legacy(void)
{
	static const int32_t chain_blocks[] = {
		0, 0, 0, 0,
		2, 0, 0, 0,
		2, 2, 0, 0
	};
	GraphParams params = default_graph_params(1);
	DatasetRecord *rec;
	char err[256];
	int rc;

	rc = core_init("trainer_test_graph_bridge");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_char_dataset(1, 3, chain_blocks);

	params.cluster_mode = GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS;
	params.distance_mode = DISTANCE_MODE_LEGACY;
	params.delta = 1;
	params.theta = 1;
	params.weighted = 0;
	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->clustering.cluster_count == 2);
	assert(rec->clustering.cluster_ids[0] == rec->clustering.cluster_ids[1]);
	assert(rec->clustering.cluster_ids[1] != rec->clustering.cluster_ids[2]);
	assert(rec->clustering.graph_diag.raw_component_count == 1);

	params = default_graph_params(1);
	params.k_neighbors = 1;
	params.use_mutual_knn = 1;
	params.delta_body_gate = 2;
	params.delta_range_gate = 2;
	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->clustering.cluster_count > 1);
	assert(rec->clustering.graph_diag.n_edges < 7);
	assert(rec->clustering.graph_diag.largest_component_fraction < 1.0f);

	core_shutdown();
}

static void test_graph_two_groups_label_prop(void)
{
	static const int32_t grouped_blocks[] = {
		0, 0, 10, 12,
		0, 0, 10, 13,
		0, 0, 11, 12,
		0, 0, 40, 42,
		0, 0, 41, 43,
		0, 0, 42, 42
	};
	GraphParams params = default_graph_params(1);
	DatasetRecord *rec;
	char err[256];
	int rc;

	rc = core_init("trainer_test_graph_groups");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_char_dataset(1, 6, grouped_blocks);
	params.delta_body_gate = 4;
	params.delta_range_gate = 4;
	params.k_neighbors = 3;

	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->clustering.cluster_count == 2);
	assert(rec->clustering.cluster_ids[0] == rec->clustering.cluster_ids[1]);
	assert(rec->clustering.cluster_ids[1] == rec->clustering.cluster_ids[2]);
	assert(rec->clustering.cluster_ids[3] == rec->clustering.cluster_ids[4]);
	assert(rec->clustering.cluster_ids[4] == rec->clustering.cluster_ids[5]);
	assert(rec->clustering.cluster_ids[0] != rec->clustering.cluster_ids[3]);
	assert(rec->clustering.graph_diag.raw_component_count == 2);
	assert(rec->clustering.graph_diag.degree_hist.bucket_0 == 0);
	assert(rec->clustering.graph_diag.cluster_size_hist.bucket_2_3 == 2);

	core_shutdown();
}

static void test_graph_directional_composition(void)
{
	static const int32_t directional_blocks[] = {
		0, 0, 5, 6, 0, 0, 5, 6, 0, 0, 5, 6, 0, 0, 5, 6,
		0, 0, 5, 6, 0, 0, 5, 6, 0, 0, 5, 6, 0, 0, -5, 6,
		0, 0, 5, 6, 0, 0, 5, 6, 0, 0, -5, 6, 0, 0, -5, 6
	};
	GraphParams params = default_graph_params(4);
	DatasetRecord *rec;
	char err[256];
	int rc;

	rc = core_init("trainer_test_graph_directional");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_char_dataset(4, 3, directional_blocks);
	params.delta_body_gate = 30;
	params.delta_range_gate = 10;
	params.delta_bullcount_gate = 4;
	params.delta_signchange_gate = 4;
	params.k_neighbors = 2;
	params.max_neighbor_distance = 15.0f;
	params.w_bullcount = 5.0f;
	params.w_signchanges = 5.0f;
	params.w_runbull = 5.0f;
	params.w_runbear = 5.0f;

	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->clustering.cluster_count == 3);
	assert(rec->clustering.graph_diag.raw_component_count == 3);

	core_shutdown();
}

static void test_graph_gpu_requirement(void)
{
	GraphParams params = default_graph_params(1);
	DatasetRecord *rec;
	static const int32_t blocks[] = {
		0, 0, 1, 2,
		0, 0, 2, 3,
		0, 0, 3, 4,
		0, 0, 4, 5
	};
	char err[256];
	int rc;

	rc = core_init("trainer_test_graph_gpu_requirement");
	assert(rc == 0);
	rec = create_char_dataset(1, 4, blocks);

	params.cluster_mode = GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS;
	params.distance_mode = DISTANCE_MODE_LEGACY;
	params.weighted = 0;

	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	if (gpu_dispatch_available()) {
		assert(rc == 0);
	} else {
		assert(rc == -2);
		assert(strstr(err, "GPU not available") != NULL);
	}

	core_shutdown();
}

static void test_threshold_order_invariant(void)
{
	static const int32_t blocks_a[] = {
		6, 5, 5, 5,
		1, 0, 0, 0,
		0, 0, 0, 0,
		5, 5, 5, 5
	};
	static const int32_t blocks_b[] = {
		5, 5, 5, 5,
		0, 0, 0, 0,
		6, 5, 5, 5,
		1, 0, 0, 0
	};
	GraphParams params = default_graph_params(1);
	DatasetRecord *rec_a;
	DatasetRecord *rec_b;
	int body_to_cluster_a[7];
	int body_to_cluster_b[7];
	char err[256];
	uint32_t i;
	int rc;

	rc = core_init("trainer_test_threshold_order");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	memset(body_to_cluster_a, -1, sizeof(body_to_cluster_a));
	memset(body_to_cluster_b, -1, sizeof(body_to_cluster_b));

	rec_a = create_char_dataset(1, 4, blocks_a);
	rec_b = create_char_dataset(1, 4, blocks_b);

	params.cluster_mode = GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS;
	params.distance_mode = DISTANCE_MODE_LEGACY;
	params.delta = 1;
	params.theta = 1;
	params.weighted = 0;

	rc = cluster_graph_run(rec_a->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = cluster_graph_run(rec_b->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);

	for (i = 0; i < rec_a->chars.n_blocks; ++i) {
		int body = rec_a->chars.data[i * rec_a->chars.dim + 0];
		body_to_cluster_a[body] = rec_a->clustering.cluster_ids[i];
	}
	for (i = 0; i < rec_b->chars.n_blocks; ++i) {
		int body = rec_b->chars.data[i * rec_b->chars.dim + 0];
		body_to_cluster_b[body] = rec_b->clustering.cluster_ids[i];
	}

	assert(body_to_cluster_a[0] == body_to_cluster_a[1]);
	assert(body_to_cluster_a[5] == body_to_cluster_a[6]);
	assert(body_to_cluster_a[0] != body_to_cluster_a[5]);
	assert(body_to_cluster_b[0] == body_to_cluster_b[1]);
	assert(body_to_cluster_b[5] == body_to_cluster_b[6]);
	assert(body_to_cluster_b[0] != body_to_cluster_b[5]);
	assert((body_to_cluster_a[0] == body_to_cluster_a[1]) == (body_to_cluster_b[0] == body_to_cluster_b[1]));
	assert((body_to_cluster_a[5] == body_to_cluster_a[6]) == (body_to_cluster_b[5] == body_to_cluster_b[6]));

	core_shutdown();
}

static void test_threshold_composition_gate(void)
{
	static const int32_t blocks[] = {
		0, 0, 5, 5, 0, 0, 5, 5, 0, 0, 5, 5,
		0, 0, 5, 5, 0, 0, 5, 5, 0, 0, -5, 5
	};
	GraphParams params = default_graph_params(3);
	DatasetRecord *rec;
	char err[256];
	int rc;

	rc = core_init("trainer_test_threshold_composition_gate");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_char_dataset(3, 2, blocks);
	params.cluster_mode = GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS;
	params.distance_mode = DISTANCE_MODE_LEGACY;
	params.delta = 20;
	params.theta = 1;
	params.weighted = 0;

	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->clustering.cluster_count == 2);
	assert(rec->clustering.cluster_ids[0] != rec->clustering.cluster_ids[1]);

	core_shutdown();
}

static void test_threshold_wick_aware_gate(void)
{
	static const int32_t blocks[] = {
		0, 1, 5, 6,
		0, 18, 5, 23
	};
	GraphParams params = default_graph_params(1);
	DatasetRecord *rec;
	char err[256];
	int rc;

	rc = core_init("trainer_test_threshold_wick_gate");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}

	rec = create_char_dataset(1, 2, blocks);
	params.cluster_mode = GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS;
	params.distance_mode = DISTANCE_MODE_WICK_AWARE;
	params.delta = 100;
	params.theta = 0;
	params.weighted = 0;
	params.delta_upper_wick_gate = 4;
	params.delta_lower_wick_gate = 4;
	params.delta_lower_ratio_gate = 40;
	params.delta_upper_ratio_gate = 40;
	params.delta_total_wick_ratio_gate = 40;

	rc = cluster_graph_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->clustering.cluster_count == 2);
	assert(rec->clustering.cluster_ids[0] != rec->clustering.cluster_ids[1]);

	core_shutdown();
}

static WaveParams default_relative_wave_params(void)
{
	WaveParams params;

	memset(&params, 0, sizeof(params));
	params.wave_model = WAVE_MODEL_RELATIVE;
	params.lmin_candles = 6;
	params.lmax_candles = 6;
	params.regime_context_window = 6;
	params.regime_context_mode = WAVE_CONTEXT_CENTERED;
	params.segment_penalty_lambda = 50;
	params.min_wave_move_pips = 4;
	params.norm_scale = 1000;
	params.context_range_floor = 1;
	params.epsilon = 1;
	params.relative_trend_retracement_max = 300;
	params.relative_strong_trend_retracement_max = 180;
	params.relative_trend_der_step_min = 550;
	params.relative_trend_der_body_min = 500;
	params.relative_trend_body_eff_min = 450;
	params.relative_trend_overlap_max = 350;
	params.relative_trend_sign_flip_max = 450;
	params.relative_trend_wick_bias_min = 900;
	params.relative_range_der_step_max = 500;
	params.relative_range_body_eff_max = 500;
	params.relative_range_overlap_min = 350;
	params.relative_range_sign_flip_min = 400;
	return params;
}

static void test_relative_wave_trend_with_shallow_pullback(void)
{
	static const Candle candles[] = {
		{0, 100, 106, 99, 105, 0},
		{1, 105, 111, 104, 110, 0},
		{2, 110, 112, 107, 108, 0},
		{3, 108, 115, 108, 114, 0},
		{4, 114, 120, 113, 119, 0},
		{5, 119, 124, 118, 123, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 5, 7},
		{1, 1, 5, 7},
		{2, 1, -2, 5},
		{1, 0, 6, 7},
		{1, 1, 5, 7},
		{1, 1, 4, 6}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveSummary summary;
	WaveParams params = default_relative_wave_params();
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_relative_trend");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_wave_dataset(6, candles, features);
	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_summary(rec->meta.dataset_id, &summary);
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(summary.params.wave_model == WAVE_MODEL_RELATIVE);
	assert(total == 1u);
	assert(tokens[0].class_id == RELATIVE_CLASS_TREND);
	assert(tokens[0].relative_retracement_ratio <= params.relative_trend_retracement_max);
	assert(tokens[0].relative_directional_efficiency_step >= params.relative_trend_der_step_min);
	core_shutdown();
}

static void test_relative_wave_ascending_sloppy_range(void)
{
	static const Candle candles[] = {
		{0, 100, 106, 99, 104, 0},
		{1, 104, 106, 101, 102, 0},
		{2, 102, 107, 101, 105, 0},
		{3, 105, 107, 102, 103, 0},
		{4, 103, 108, 102, 106, 0},
		{5, 106, 108, 103, 104, 0}
	};
	static const CandleFeat features[] = {
		{2, 1, 4, 7},
		{2, 1, -2, 5},
		{2, 1, 3, 6},
		{2, 1, -2, 5},
		{2, 1, 3, 6},
		{2, 1, -2, 5}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveParams params = default_relative_wave_params();
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	rc = core_init("trainer_test_relative_range");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_wave_dataset(6, candles, features);
	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(total == 1u);
	assert(tokens[0].class_id == RELATIVE_CLASS_RANGE);
	assert(tokens[0].relative_overlap_ratio >= params.relative_range_overlap_min);
	assert(tokens[0].relative_directional_efficiency_step <= params.relative_range_der_step_max);
	core_shutdown();
}

static void test_layered_wave_balanced_sequential_range(void)
{
	static const Candle candles[] = {
		{0, 100, 105, 99, 104, 0},
		{1, 104, 108, 103, 107, 0},
		{2, 107, 108, 102, 103, 0},
		{3, 103, 104, 100, 100, 0},
		{4, 100, 105, 99, 104, 0},
		{5, 104, 108, 103, 107, 0},
		{6, 107, 108, 103, 104, 0},
		{7, 104, 105, 101, 102, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, -4, 6},
		{1, 1, -3, 4},
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, -3, 5},
		{1, 1, -2, 4}
	};
	DatasetRecord *rec;
	WaveToken tokens[8];
	WaveSummary summary;
	WaveParams params = default_relative_wave_params();
	uint32_t written = 0;
	uint32_t total = 0;
	char err[256];
	int rc;

	params.wave_model = WAVE_MODEL_LAYERED;
	params.lmin_candles = 1;
	params.lmax_candles = 32;

	rc = core_init("trainer_test_layered_seq_range");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_wave_dataset(8, candles, features);
	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	rc = core_get_wave_summary(rec->meta.dataset_id, &summary);
	assert(rc == 0);
	rc = core_get_wave_tokens(rec->meta.dataset_id, 0, 8, tokens, &written, &total);
	assert(rc == 0);
	assert(summary.params.wave_model == WAVE_MODEL_LAYERED);
	assert(total == 1u);
	assert(tokens[0].class_id == RELATIVE_CLASS_RANGE);
	assert(tokens[0].layered_sequential_count >= 4u);
	assert(tokens[0].layered_alternating_count == 0u);
	core_shutdown();
}

static void test_micro_harvest_replay_uses_dataset_wave_params(void)
{
	static const Candle candles[] = {
		{0, 100, 105, 99, 104, 0},
		{1, 104, 108, 103, 107, 0},
		{2, 107, 108, 102, 103, 0},
		{3, 103, 104, 100, 100, 0},
		{4, 100, 105, 99, 104, 0},
		{5, 104, 108, 103, 107, 0},
		{6, 107, 108, 103, 104, 0},
		{7, 104, 105, 101, 102, 0},
		{8, 102, 107, 101, 106, 0},
		{9, 106, 110, 105, 109, 0},
		{10, 109, 110, 104, 105, 0},
		{11, 105, 106, 102, 102, 0},
		{12, 102, 107, 101, 106, 0},
		{13, 106, 110, 105, 109, 0},
		{14, 109, 110, 105, 106, 0},
		{15, 106, 107, 103, 104, 0}
	};
	static const CandleFeat features[] = {
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, -4, 6},
		{1, 1, -3, 4},
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, -3, 5},
		{1, 1, -2, 4},
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, -4, 6},
		{1, 1, -3, 4},
		{1, 1, 4, 6},
		{1, 1, 3, 5},
		{1, 1, -3, 5},
		{1, 1, -2, 4}
	};
	DatasetRecord *rec;
	MicroHarvestSearchConfig config;
	MicroHarvestResult result;
	WaveParams params = default_relative_wave_params();
	char err[256];
	int rc;

	params.wave_model = WAVE_MODEL_LAYERED;
	params.lmin_candles = 1;
	params.lmax_candles = 32;

	rc = core_init("trainer_test_micro_harvest_replay");
	assert(rc == 0);
	if (!gpu_dispatch_available()) {
		core_shutdown();
		return;
	}
	rec = create_wave_dataset(16, candles, features);
	assert(rec != NULL);
	rc = wave_dp_run(rec->meta.dataset_id, &params, NULL, NULL, err, sizeof(err));
	assert(rc == 0);
	assert(rec->waves.token_count > 0u);

	memset(&config, 0, sizeof(config));
	config.bias = MH_BIAS_BUY;
	config.lookback_days = 14;
	config.spread_pips = 0;
	config.max_orders_min = 1;
	config.max_orders_max = 2;
	config.spacing_min_pips = 2;
	config.spacing_max_pips = 6;
	config.spacing_step_pips = 2;
	config.take_profit_min_pips = 2;
	config.take_profit_max_pips = 8;
	config.take_profit_step_pips = 2;
	config.entry_edge_min = 400;
	config.entry_edge_max = 900;
	config.entry_edge_step = 100;
	config.min_island_height_pips = 1;
	config.max_island_height_pips = 1000;
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run_replay(rec->meta.dataset_id, &rec->waves.params, &config, &result, NULL);
	assert(rc == 0);
	assert(result.replay_mode == 1);
	assert(result.recommended_params.max_orders >= config.max_orders_min);
	assert(result.recommended_params.max_orders <= config.max_orders_max);
	assert(rec->meta.candle_count == 16u);
	micro_harvest_result_free(&result);
	core_shutdown();
}

static void test_wave_predictor_repeating_window_signal(void)
{
	DatasetRecord *rec;
	WaveToken *tokens;
	uint32_t dataset_id = 0u;
	char json[65536];
	int rc;

	rc = core_init("trainer_test_wave_predictor");
	assert(rc == 0);
	rec = store_alloc_dataset(&dataset_id);
	assert(rec != NULL);
	rec->meta.dataset_id = dataset_id;
	rec->meta.candle_count = 48u;
	rec->meta.feature_count = 48u;
	rec->waves.token_count = 12u;
	rec->waves.tokens = (WaveToken *)calloc(rec->waves.token_count, sizeof(*rec->waves.tokens));
	assert(rec->waves.tokens != NULL);
	rec->waves.params.wave_model = WAVE_MODEL_LAYERED;
	tokens = rec->waves.tokens;

	for (uint32_t i = 0u; i < rec->waves.token_count; ++i) {
		tokens[i].start_candle = i * 4u;
		tokens[i].end_candle = i * 4u + 3u;
		tokens[i].duration_candles = 4u;
		tokens[i].boundary_confidence = 700;
	}
	for (uint32_t block = 0u; block < 3u; ++block) {
		uint32_t base = block * 4u;

		tokens[base + 0u].class_id = RELATIVE_CLASS_TREND;
		tokens[base + 0u].dominant_direction = 1;
		tokens[base + 0u].net_move_raw = 160;
		tokens[base + 0u].relative_directional_efficiency_step = 820;
		tokens[base + 0u].relative_retracement_ratio = 120;
		tokens[base + 0u].relative_overlap_ratio = 140;

		tokens[base + 1u].class_id = RELATIVE_CLASS_RANGE;
		tokens[base + 1u].dominant_direction = 0;
		tokens[base + 1u].net_move_raw = 8;
		tokens[base + 1u].relative_directional_efficiency_step = 310;
		tokens[base + 1u].relative_retracement_ratio = 620;
		tokens[base + 1u].relative_overlap_ratio = 650;

		tokens[base + 2u].class_id = RELATIVE_CLASS_TREND;
		tokens[base + 2u].dominant_direction = -1;
		tokens[base + 2u].net_move_raw = -150;
		tokens[base + 2u].relative_directional_efficiency_step = 780;
		tokens[base + 2u].relative_retracement_ratio = 170;
		tokens[base + 2u].relative_overlap_ratio = 160;

		tokens[base + 3u].class_id = RELATIVE_CLASS_RANGE;
		tokens[base + 3u].dominant_direction = 0;
		tokens[base + 3u].net_move_raw = 6;
		tokens[base + 3u].relative_directional_efficiency_step = 280;
		tokens[base + 3u].relative_retracement_ratio = 680;
		tokens[base + 3u].relative_overlap_ratio = 700;
	}

	rc = wave_predictor_encode_json(rec->meta.dataset_id, 8u, 1u, 2u, 0, json, sizeof(json));
	assert(rc > 0);
	assert(strstr(json, "\"predicted_class\":\"range\"") != NULL);
	assert(strstr(json, "\"predicted_direction\":\"bearish\"") != NULL);
	core_shutdown();
}

int main(void)
{
	test_pipeline();
	test_graph_scale_separation();
	test_wave_inference_pipeline();
	test_primitive_wave_ignores_inside_candle_break();
	test_primitive_wave_splits_breakout_tail();
	test_primitive_wave_merges_separator_debug_flag();
	test_primitive_wave_collapses_separator_run();
	test_primitive_wave_tolerates_single_counter_candle();
	test_primitive_wave_rejects_counter_candle_level_break();
	test_primitive_wave_treats_doji_as_neutral();
	test_primitive_wave_ignores_neutral_bridge_in_primary_dp();
	test_primitive_wave_live_prefix_neutral_bridge();
	test_primitive_wave_splits_internal_trend_with_neutral_break();
	test_primitive_wave_extends_across_terminal_pullback_confirmation();
	test_primitive_wave_expands_consecutive_boundary();
	test_primitive_wave_extends_across_leading_inside_edge();
	test_primitive_wave_rejects_opposite_leading_edge();
	test_primitive_wave_rejects_bearish_lead_for_bull_run();
	test_graph_bridge_chain_vs_legacy();
	test_graph_two_groups_label_prop();
	test_graph_directional_composition();
	test_graph_gpu_requirement();
	test_threshold_order_invariant();
	test_threshold_composition_gate();
	test_threshold_wick_aware_gate();
	test_relative_wave_trend_with_shallow_pullback();
	test_relative_wave_ascending_sloppy_range();
	test_layered_wave_balanced_sequential_range();
	test_micro_harvest_replay_uses_dataset_wave_params();
	test_wave_predictor_repeating_window_signal();
	printf("test_core ok\n");
	return 0;
}
