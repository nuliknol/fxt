#include "core_api.h"
#include "internal_store.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	uint32_t token_index;
	int32_t fit_error;
} WaveTokenSortEntry;

static const char *wave_model_name(int32_t wave_model);

static int compare_wave_token_fit_error_asc(const void *lhs, const void *rhs)
{
	const WaveTokenSortEntry *a = (const WaveTokenSortEntry *)lhs;
	const WaveTokenSortEntry *b = (const WaveTokenSortEntry *)rhs;

	if (a->fit_error != b->fit_error) {
		return (a->fit_error < b->fit_error) ? -1 : 1;
	}
	if (a->token_index < b->token_index) {
		return -1;
	}
	if (a->token_index > b->token_index) {
		return 1;
	}
	return 0;
}

static int append(char *dst, size_t cap, size_t *used, const char *fmt, ...)
{
	va_list ap;
	int wrote;

	if (*used >= cap) {
		return -1;
	}
	va_start(ap, fmt);
	wrote = vsnprintf(dst + *used, cap - *used, fmt, ap);
	va_end(ap);
	if (wrote < 0) {
		return -1;
	}
	*used += (size_t)wrote;
	if (*used >= cap) {
		return -1;
	}
	return 0;
}

static int append_histogram_json(char *out, size_t out_cap, size_t *used, const HistogramSummary *hist)
{
	return append(out,
		out_cap,
		used,
		"{\"bucket_0\":%u,\"bucket_1\":%u,\"bucket_2_3\":%u,\"bucket_4_7\":%u,\"bucket_8_15\":%u,\"bucket_16_plus\":%u}",
		hist->bucket_0,
		hist->bucket_1,
		hist->bucket_2_3,
		hist->bucket_4_7,
		hist->bucket_8_15,
		hist->bucket_16_plus);
}

static const char *micro_harvest_bias_name(int32_t bias)
{
	return (bias == MH_BIAS_SELL) ? "SELL" : "BUY";
}

static const char *micro_harvest_signal_feature_name(int32_t feature_id)
{
	switch (feature_id) {
	case 0:
		return "none";
	case 1:
		return "retracement_ratio";
	case 2:
		return "second_best_error";
	case 3:
		return "relative_overlap_ratio";
	case 4:
		return "relative_directional_efficiency_step";
	case 5:
		return "boundary_confidence";
	case 6:
		return "weak_fit";
	case 7:
		return "net_move_raw_abs";
	case 8:
		return "duration_candles";
	case 9:
		return "relative_wick_to_body_ratio";
	case 10:
		return "ambiguity_margin";
	case 11:
		return "fit_error";
	case 12:
		return "relative_retracement_ratio";
	case 13:
		return "relative_trend_quality_raw";
	case 14:
		return "knn_margin";
	case 15:
		return "sign_change_density";
	case 16:
		return "primitive_alternating_ratio";
	case 17:
		return "directional_purity_body";
	default:
		return "unknown";
	}
}

static const char *micro_harvest_signal_compare_name(int32_t compare_op)
{
	if (compare_op < 0) {
		return "<=";
	}
	if (compare_op > 0) {
		return ">=";
	}
	return "off";
}

static int append_micro_harvest_result_json(char *out, size_t out_cap, size_t *used, uint32_t dataset_id, const MicroHarvestResult *result)
{
	int32_t i;

	if (append(out,
		out_cap,
		used,
		"{\"kind\":\"micro_harvest_result\",\"dataset_id\":%u,\"bias\":\"%s\","
		"\"replay_mode\":%s,\"replay_source_path\":\"%s\","
		"\"recommended_parameters\":{\"max_orders\":%d,\"min_spacing_pips\":%d,"
		"\"take_profit_pips\":%d,\"spread_pips\":%d,\"entry_edge_fraction\":%d,"
		"\"min_island_height_pips\":%d,\"max_island_height_pips\":%d,"
		"\"signal_feature_id\":%d,\"signal_feature_name\":\"%s\","
		"\"signal_compare_op\":%d,\"signal_compare_name\":\"%s\","
		"\"signal_threshold\":%d},"
		"\"simulation_summary\":{\"closed_profit_pips\":%d,\"closed_trade_count\":%d,"
		"\"ignored_open_trade_count\":%d,\"max_concurrent_open_trade_count\":%d,"
		"\"selected_island_count\":%d},"
		"\"islands\":[",
		dataset_id,
		micro_harvest_bias_name(result->recommended_params.bias),
		result->replay_mode ? "true" : "false",
		result->replay_source_path,
		result->recommended_params.max_orders,
		result->recommended_params.min_spacing_pips,
		result->recommended_params.take_profit_pips,
		result->recommended_params.spread_pips,
		result->recommended_params.entry_edge_fraction,
		result->recommended_params.min_island_height_pips,
		result->recommended_params.max_island_height_pips,
		result->recommended_params.signal_feature_id,
		micro_harvest_signal_feature_name(result->recommended_params.signal_feature_id),
		result->recommended_params.signal_compare_op,
		micro_harvest_signal_compare_name(result->recommended_params.signal_compare_op),
		result->recommended_params.signal_threshold,
		result->total_profit_pips,
		result->total_closed_trade_count,
		result->total_ignored_open_trade_count,
		result->total_max_concurrent_open_trade_count,
		result->selected_island_count) != 0) {
		return -1;
	}
	for (i = 0; i < result->island_count; ++i) {
		const MicroHarvestIsland *island = &result->islands[i];

		if (i > 0 && append(out, out_cap, used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			used,
			"{\"index\":%d,\"selected\":%s,\"start_candle_index\":%d,\"end_candle_index\":%d,"
			"\"start_wave_index\":%d,\"end_wave_index\":%d,\"low_price_pips\":%d,"
			"\"high_price_pips\":%d,\"height_pips\":%d,\"profit_pips\":%d,"
			"\"closed_trade_count\":%d,\"ignored_open_trade_count\":%d,"
			"\"max_concurrent_open_trade_count\":%d}",
			island->index,
			island->selected ? "true" : "false",
			island->start_candle_index,
			island->end_candle_index,
			island->start_wave_index,
			island->end_wave_index,
			island->low_price_pips,
			island->high_price_pips,
			island->height_pips,
			island->profit_pips,
			island->closed_trade_count,
			island->ignored_open_trade_count,
			island->max_concurrent_open_trade_count) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, used, "],\"trades\":[") != 0) {
		return -1;
	}
	for (i = 0; i < result->trade_count; ++i) {
		const MicroHarvestTrade *trade = &result->trades[i];

		if (i > 0 && append(out, out_cap, used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			used,
			"{\"island_index\":%d,\"direction\":\"%s\",\"open_candle_index\":%d,"
			"\"close_candle_index\":%d,\"open_price_pips\":%d,\"close_price_pips\":%d,"
			"\"take_profit_pips\":%d,\"profit_pips\":%d,\"closed\":%s}",
			trade->island_index,
			micro_harvest_bias_name(trade->direction),
			trade->open_candle_index,
			trade->close_candle_index,
			trade->open_price_pips,
			trade->close_price_pips,
			trade->take_profit_pips,
			trade->profit_pips,
			trade->closed ? "true" : "false") != 0) {
			return -1;
		}
	}
	return append(out, out_cap, used, "]}");
}

int json_encode_dataset_list(char *out, size_t out_cap)
{
	DatasetMeta metas[64];
	WaveSummary summary;
	uint32_t written;
	size_t used = 0;
	uint32_t i;

	if (core_list_datasets(metas, 64, &written) != 0) {
		return -1;
	}
	if (append(out, out_cap, &used, "{\"datasets\":[") != 0) {
		return -1;
	}
	for (i = 0; i < written && i < 64; ++i) {
		int has_waves = 0;
		const char *model_name = "";
		uint32_t wave_token_count = 0;

		if (core_get_wave_summary(metas[i].dataset_id, &summary) == 0 && summary.token_count > 0) {
			has_waves = 1;
			model_name = wave_model_name(summary.params.wave_model);
			wave_token_count = summary.token_count;
		}
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"dataset_id\":%u,\"name\":\"%s\",\"candles\":%u,\"features\":%u,\"char_blocks\":%u,\"q\":%u,"
			"\"has_waves\":%s,\"wave_model\":\"%s\",\"wave_token_count\":%u}",
			metas[i].dataset_id,
			metas[i].name,
			metas[i].candle_count,
			metas[i].feature_count,
			metas[i].char_block_count,
			metas[i].q,
			has_waves ? "true" : "false",
			model_name,
			wave_token_count) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_dataset_detail(uint32_t dataset_id, char *out, size_t out_cap)
{
	DatasetMeta meta;

	if (core_get_dataset_meta(dataset_id, &meta) != 0) {
		return -1;
	}
	return snprintf(out,
		out_cap,
		"{\"dataset_id\":%u,\"name\":\"%s\",\"source_path\":\"%s\",\"pip_scale\":%u,\"candles\":%u,\"features\":%u,\"char_blocks\":%u,\"q\":%u}",
		meta.dataset_id,
		meta.name,
		meta.source_path,
		meta.pip_scale,
		meta.candle_count,
		meta.feature_count,
		meta.char_block_count,
		meta.q);
}

int json_encode_candle_window(uint32_t dataset_id, uint32_t start, uint32_t count, char *out, size_t out_cap)
{
	Candle items[2048];
	uint32_t written = 0;
	uint32_t i;
	size_t used = 0;

	if (count > 2048) {
		count = 2048;
	}
	if (core_get_candles(dataset_id, start, count, items, &written) != 0) {
		return -1;
	}
	if (append(out, out_cap, &used, "{\"dataset_id\":%u,\"start\":%u,\"count\":%u,\"candles\":[", dataset_id, start, written) != 0) {
		return -1;
	}
	for (i = 0; i < written; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"ts_epoch\":%d,\"open_pips\":%d,\"high_pips\":%d,\"low_pips\":%d,\"close_pips\":%d,\"volume\":%d}",
			items[i].ts_epoch,
			items[i].open_pips,
			items[i].high_pips,
			items[i].low_pips,
			items[i].close_pips,
			items[i].volume) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_feature_window(uint32_t dataset_id, uint32_t start, uint32_t count, char *out, size_t out_cap)
{
	CandleFeat items[2048];
	uint32_t written = 0;
	uint32_t i;
	size_t used = 0;

	if (count > 2048) {
		count = 2048;
	}
	if (core_get_features(dataset_id, start, count, items, &written) != 0) {
		return -1;
	}
	if (append(out, out_cap, &used, "{\"dataset_id\":%u,\"start\":%u,\"count\":%u,\"features\":[", dataset_id, start, written) != 0) {
		return -1;
	}
	for (i = 0; i < written; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"u\":%d,\"l\":%d,\"b\":%d,\"r\":%d}",
			items[i].u,
			items[i].l,
			items[i].b,
			items[i].r) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_job_list(char *out, size_t out_cap)
{
	JobSummary jobs[128];
	uint32_t written;
	size_t used = 0;
	uint32_t i;

	if (core_list_jobs(jobs, 128, &written) != 0) {
		return -1;
	}
	if (append(out, out_cap, &used, "{\"jobs\":[") != 0) {
		return -1;
	}
	for (i = 0; i < written && i < 128; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"job_id\":%llu,\"dataset_id\":%u,\"job_type\":%u,\"status\":%u,\"progress\":%.3f,\"error\":\"%s\"}",
			(unsigned long long)jobs[i].job_id,
			jobs[i].dataset_id,
			jobs[i].job_type,
			jobs[i].status,
			jobs[i].progress,
			jobs[i].error_message) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_job_detail(uint64_t job_id, char *out, size_t out_cap)
{
	JobSummary j;
	if (core_get_job(job_id, &j) != 0) {
		return -1;
	}
	return snprintf(out,
		out_cap,
		"{\"job_id\":%llu,\"dataset_id\":%u,\"job_type\":%u,\"status\":%u,\"progress\":%.3f,\"created_ts\":%lld,\"updated_ts\":%lld,\"error\":\"%s\"}",
		(unsigned long long)j.job_id,
		j.dataset_id,
		j.job_type,
		j.status,
		j.progress,
		(long long)j.created_ts,
		(long long)j.updated_ts,
		j.error_message);
}

int json_encode_micro_harvest_latest(uint32_t dataset_id, char *out, size_t out_cap)
{
	DatasetRecord *rec;
	size_t used = 0u;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL || !rec->micro_harvest.available) {
		return -1;
	}
	if (append_micro_harvest_result_json(out, out_cap, &used, dataset_id, &rec->micro_harvest.latest_result) != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_cluster_list(uint32_t dataset_id, char *out, size_t out_cap)
{
	ClusterSummary items[512];
	DatasetRecord *rec;
	uint32_t written;
	uint32_t i;
	size_t used = 0;

	if (core_get_cluster_summaries(dataset_id, items, 512, &written) != 0) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (append(out,
		out_cap,
		&used,
		"{\"dataset_id\":%u,\"algorithm_type\":%u,\"graph_cluster_mode\":%u,\"clusters\":[",
		dataset_id,
		rec != NULL ? rec->clustering.algorithm_type : 0u,
		rec != NULL ? rec->clustering.graph_cluster_mode : 0u) != 0) {
		return -1;
	}
	for (i = 0; i < written && i < 512; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"cluster_id\":%u,\"size\":%u,\"representative_index\":%u,\"score\":%.3f}",
			items[i].cluster_id,
			items[i].size,
			items[i].representative_index,
			items[i].score) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]") != 0) {
		return -1;
	}
	if (rec != NULL) {
		if (append(out, out_cap, &used, ",\"graph_diagnostics\":{") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"\"n_nodes\":%u,\"n_edges\":%u,\"average_degree\":%.3f,\"raw_component_count\":%u,\"largest_component_size\":%u,\"largest_component_fraction\":%.3f,\"percolated\":%s,\"degree_hist\":",
			rec->clustering.graph_diag.n_nodes,
			rec->clustering.graph_diag.n_edges,
			rec->clustering.graph_diag.average_degree,
			rec->clustering.graph_diag.raw_component_count,
			rec->clustering.graph_diag.largest_component_size,
			rec->clustering.graph_diag.largest_component_fraction,
			rec->clustering.graph_diag.percolated ? "true" : "false") != 0) {
			return -1;
		}
		if (append_histogram_json(out, out_cap, &used, &rec->clustering.graph_diag.degree_hist) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, ",\"cluster_size_hist\":") != 0) {
			return -1;
		}
		if (append_histogram_json(out, out_cap, &used, &rec->clustering.graph_diag.cluster_size_hist) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, "}") != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_cluster_member_list(uint32_t dataset_id, uint32_t cluster_id, uint32_t start, uint32_t count, char *out, size_t out_cap)
{
	Occurrence items[256];
	uint32_t written = 0;
	uint32_t total = 0;
	DatasetRecord *rec;
	uint32_t i;
	size_t used = 0;

	if (count > 256u) {
		count = 256u;
	}
	rec = store_find_dataset(dataset_id);
	if (core_get_cluster_occurrences(dataset_id, cluster_id, start, count, items, &written, &total) != 0 || rec == NULL) {
		return -1;
	}
	if (append(out,
		out_cap,
		&used,
		"{\"dataset_id\":%u,\"cluster_id\":%u,\"start\":%u,\"count\":%u,\"total\":%u,\"q\":%u,\"members\":[",
		dataset_id,
		cluster_id,
		start,
		written,
		total,
		rec->chars.q) != 0) {
		return -1;
	}
	for (i = 0; i < written; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"member_index\":%u,\"start_candle\":%u,\"end_candle\":%u,\"start_ts\":%d,\"end_ts\":%d,\"object_id\":%u,\"cluster_id\":%u}",
			start + i,
			items[i].start_candle,
			items[i].end_candle,
			items[i].start_ts,
			items[i].end_ts,
			items[i].object_id,
			items[i].cluster_id) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_word_list(uint32_t dataset_id, char *out, size_t out_cap)
{
	WordSummary items[1024];
	uint32_t written;
	uint32_t i;
	size_t used = 0;

	if (core_get_word_summaries(dataset_id, items, 1024, &written) != 0) {
		return -1;
	}
	if (append(out, out_cap, &used, "{\"dataset_id\":%u,\"words\":[", dataset_id) != 0) {
		return -1;
	}
	for (i = 0; i < written && i < 1024; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"word_id\":%u,\"length_chars\":%u,\"frequency\":%u}",
			items[i].word_id,
			items[i].length_chars,
			items[i].frequency) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int json_encode_phrase_list(uint32_t dataset_id, char *out, size_t out_cap)
{
	PhraseSummary items[1024];
	uint32_t written;
	uint32_t i;
	size_t used = 0;

	if (core_get_phrase_summaries(dataset_id, items, 1024, &written) != 0) {
		return -1;
	}
	if (append(out, out_cap, &used, "{\"dataset_id\":%u,\"phrases\":[", dataset_id) != 0) {
		return -1;
	}
	for (i = 0; i < written && i < 1024; ++i) {
		if (i > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"phrase_id\":%u,\"token_len\":%u,\"frequency\":%u,\"avg_duration_chars\":%.3f,\"avg_duration_candles\":%.3f}",
			items[i].phrase_id,
			items[i].token_len,
			items[i].frequency,
			items[i].avg_duration_chars,
			items[i].avg_duration_candles) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

static int wave_class_id_from_name(const char *name)
{
	if (name == NULL || name[0] == '\0' || strcmp(name, "all") == 0) {
		return -1;
	}
	if (strcmp(name, "strong_trend") == 0 || strcmp(name, "main_trend") == 0) {
		return WAVE_CLASS_STRONG_TREND;
	}
	if (strcmp(name, "trend") == 0) {
		return RELATIVE_CLASS_TREND;
	}
	if (strcmp(name, "range") == 0) {
		return RELATIVE_CLASS_RANGE;
	}
	if (strcmp(name, "weak_trend") == 0) {
		return WAVE_CLASS_WEAK_TREND;
	}
	if (strcmp(name, "retracement") == 0) {
		return WAVE_CLASS_RETRACEMENT;
	}
	if (strcmp(name, "alternating_chop") == 0) {
		return WAVE_CLASS_ALTERNATING_CHOP;
	}
	if (strcmp(name, "mixed_compression") == 0) {
		return WAVE_CLASS_MIXED_COMPRESSION;
	}
	if (strcmp(name, "volatile_expansion") == 0) {
		return WAVE_CLASS_VOLATILE_EXPANSION;
	}
	if (strcmp(name, "v_shaped_reversal") == 0) {
		return WAVE_CLASS_V_SHAPED_REVERSAL;
	}
	return -2;
}

static int wave_primitive_filter_id_from_name(const char *name)
{
	if (name == NULL || name[0] == '\0' || strcmp(name, "all") == 0) {
		return -1;
	}
	if (strcmp(name, "sequential_string") == 0 || strcmp(name, "consecutive_string") == 0) {
		return 0;
	}
	if (strcmp(name, "alternating_string") == 0) {
		return 1;
	}
	if (strcmp(name, "separator_candle") == 0) {
		return 2;
	}
	return -2;
}

static const char *wave_model_name(int32_t wave_model)
{
	if (wave_model == WAVE_MODEL_PRIMITIVE) {
		return "primitive_waves";
	}
	if (wave_model == WAVE_MODEL_RELATIVE_SEEDED) {
		return "relative_waves_seeded";
	}
	if (wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) {
		return "relative_waves_iterative";
	}
	if (wave_model == WAVE_MODEL_LAYERED) {
		return "layered_waves";
	}
	if (wave_model == WAVE_MODEL_RELATIVE_2_STAGE) {
		return "relative_waves_2_stage";
	}
	if (wave_model == WAVE_MODEL_RELATIVE) {
		return "relative_waves";
	}
	if (wave_model == WAVE_MODEL_LEGACY) {
		return "legacy_waves";
	}
	return "legacy_waves";
}

static const char *wave_primitive_type_name(const WaveToken *tok)
{
	if (tok == NULL) {
		return "sequential_string";
	}
	if (tok->class_id == PRIMITIVE_CLASS_SEPARATOR) {
		return "separator_candle";
	}
	if (tok->class_id == PRIMITIVE_CLASS_ALTERNATING) {
		return "alternating_string";
	}
	return "sequential_string";
}

static const char *wave_token_class_name(const WaveToken *tok, int32_t wave_model)
{
	if (tok == NULL) {
		return "sequential_string";
	}
	if (wave_model == WAVE_MODEL_PRIMITIVE) {
		return wave_primitive_type_name(tok);
	}
	if (wave_model == WAVE_MODEL_RELATIVE ||
		wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		wave_model == WAVE_MODEL_RELATIVE_ITERATIVE ||
		wave_model == WAVE_MODEL_LAYERED) {
		return (tok->class_id == RELATIVE_CLASS_RANGE) ? "range" : "trend";
	}
	if (tok->class_id == WAVE_CLASS_WEAK_TREND) {
		return "weak_trend";
	}
	if (tok->class_id == WAVE_CLASS_RETRACEMENT) {
		return "retracement";
	}
	if (tok->class_id == WAVE_CLASS_ALTERNATING_CHOP) {
		return "alternating_chop";
	}
	if (tok->class_id == WAVE_CLASS_MIXED_COMPRESSION) {
		return "mixed_compression";
	}
	if (tok->class_id == WAVE_CLASS_VOLATILE_EXPANSION) {
		return "volatile_expansion";
	}
	if (tok->class_id == WAVE_CLASS_V_SHAPED_REVERSAL) {
		return "v_shaped_reversal";
	}
	return "strong_trend";
}

static const char *wave_primitive_direction_name(const WaveToken *tok)
{
	if (tok == NULL) {
		return "neutral";
	}
	if (tok->dominant_direction > 0) {
		return "bull";
	}
	if (tok->dominant_direction < 0) {
		return "bear";
	}
	return "neutral";
}

static const char *wave_token_debug_marker(const WaveToken *tok)
{
	if (tok == NULL) {
		return "-";
	}
	if ((tok->debug_flags & WAVE_TOKEN_FLAG_SEPARATOR_LEFT_ADDED) &&
		(tok->debug_flags & WAVE_TOKEN_FLAG_SEPARATOR_RIGHT_ADDED)) {
		return "LR";
	}
	if (tok->debug_flags & WAVE_TOKEN_FLAG_SEPARATOR_LEFT_ADDED) {
		return "L";
	}
	if (tok->debug_flags & WAVE_TOKEN_FLAG_SEPARATOR_RIGHT_ADDED) {
		return "R";
	}
	return "-";
}

static int32_t json_penalty_low(int32_t value, int32_t floor)
{
	if (value >= floor) {
		return 0;
	}
	return floor - value;
}

static int32_t json_penalty_high(int32_t value, int32_t ceiling)
{
	if (value <= ceiling) {
		return 0;
	}
	return value - ceiling;
}

static int32_t json_max_i32(int32_t a, int32_t b)
{
	return (a > b) ? a : b;
}

static int append_penalty_term_json(char *out,
	size_t out_cap,
	size_t *used,
	const char *name,
	int32_t value,
	int32_t threshold,
	int32_t weight,
	int32_t penalty,
	const char *mode)
{
	return append(out,
		out_cap,
		used,
		"{\"name\":\"%s\",\"mode\":\"%s\",\"value\":%d,\"threshold\":%d,\"weight\":%d,\"penalty\":%d}",
		name,
		mode,
		value,
		threshold,
		weight,
		penalty);
}

static int append_layered_penalty_trace_json(char *out,
	size_t out_cap,
	size_t *used,
	const WaveSummary *summary,
	const DatasetRecord *rec,
	const WaveToken *tok)
{
	const WaveParams *params;
	int32_t drift_floor;
	int32_t trend_sign_ceiling;
	int32_t trend_overlap_ceiling;
	int32_t trend_retrace_ceiling;
	int32_t range_sign_floor;
	int32_t range_overlap_floor;
	int32_t seq_count_ratio;
	int32_t alt_count_ratio;
	int32_t total_child_move_sum = 0;
	int32_t aligned_seq_move_sum = 0;
	int32_t counter_seq_move_sum = 0;
	int32_t aligned_alt_move_sum = 0;
	int32_t counter_alt_move_sum = 0;
	int32_t alternating_move_sum = 0;
	int32_t max_child_move = 0;
	int32_t aligned_seq_share;
	int32_t counter_seq_share;
	int32_t aligned_alt_share;
	int32_t counter_alt_share;
	int32_t alternating_share;
	int32_t avg_child_move;
	int32_t range_variance_ratio;
	int32_t dp_bias;
	int32_t move_raw;
	int32_t candle_span;
	uint32_t child_count;
	uint32_t child_index;
	int first = 1;

	if (summary == NULL || rec == NULL || tok == NULL) {
		return 0;
	}
	if (summary->params.wave_model != WAVE_MODEL_LAYERED ||
		rec->waves.layer1_tokens == NULL ||
		tok->layered_child_end_index < tok->layered_child_start_index ||
		tok->layered_child_end_index >= rec->waves.layer1_token_count) {
		return append(out,
			out_cap,
			used,
			",\"penalty_trace\":{\"model\":\"%s\",\"fit_error\":%d,\"second_best_error\":%d}",
			wave_model_name(summary->params.wave_model),
			tok->fit_error,
			tok->second_best_error);
	}
	params = &summary->params;
	drift_floor = json_max_i32(params->minimum_pips_for_trend * 5, 28);
	trend_sign_ceiling = json_max_i32(params->relative_trend_sign_flip_max - 40, 220);
	trend_overlap_ceiling = json_max_i32(params->relative_trend_overlap_max + 80, 180);
	trend_retrace_ceiling = json_max_i32(params->relative_trend_retracement_max + 120, 220);
	range_sign_floor = json_max_i32(params->relative_range_sign_flip_min - 120, 180);
	range_overlap_floor = json_max_i32(params->relative_range_overlap_min - 140, 120);
	child_count = tok->layered_child_end_index - tok->layered_child_start_index + 1u;
	for (child_index = tok->layered_child_start_index; child_index <= tok->layered_child_end_index; ++child_index) {
		const WaveToken *child = &rec->waves.layer1_tokens[child_index];
		int32_t move = child->net_move_raw >= 0 ? child->net_move_raw : -child->net_move_raw;

		total_child_move_sum += move;
		if (move > max_child_move) {
			max_child_move = move;
		}
		if (child->class_id == PRIMITIVE_CLASS_ALTERNATING) {
			alternating_move_sum += move;
			if (tok->dominant_direction >= 0) {
				if (child->dominant_direction >= 0) {
					aligned_alt_move_sum += move;
				} else {
					counter_alt_move_sum += move;
				}
			} else {
				if (child->dominant_direction <= 0) {
					aligned_alt_move_sum += move;
				} else {
					counter_alt_move_sum += move;
				}
			}
		} else if (child->class_id == PRIMITIVE_CLASS_CONSECUTIVE) {
			if (tok->dominant_direction >= 0) {
				if (child->dominant_direction >= 0) {
					aligned_seq_move_sum += move;
				} else {
					counter_seq_move_sum += move;
				}
			} else {
				if (child->dominant_direction <= 0) {
					aligned_seq_move_sum += move;
				} else {
					counter_seq_move_sum += move;
				}
			}
		}
	}
	seq_count_ratio = (int32_t)(((int64_t)tok->layered_sequential_count * params->norm_scale) /
		json_max_i32((int32_t)child_count, 1));
	alt_count_ratio = (int32_t)(((int64_t)tok->layered_alternating_count * params->norm_scale) /
		json_max_i32((int32_t)child_count, 1));
	aligned_seq_share = (int32_t)(((int64_t)aligned_seq_move_sum * params->norm_scale) /
		json_max_i32(total_child_move_sum, 1));
	counter_seq_share = (int32_t)(((int64_t)counter_seq_move_sum * params->norm_scale) /
		json_max_i32(total_child_move_sum, 1));
	aligned_alt_share = (int32_t)(((int64_t)aligned_alt_move_sum * params->norm_scale) /
		json_max_i32(total_child_move_sum, 1));
	counter_alt_share = (int32_t)(((int64_t)counter_alt_move_sum * params->norm_scale) /
		json_max_i32(total_child_move_sum, 1));
	alternating_share = (int32_t)(((int64_t)alternating_move_sum * params->norm_scale) /
		json_max_i32(total_child_move_sum, 1));
	avg_child_move = total_child_move_sum / json_max_i32((int32_t)child_count, 1);
	range_variance_ratio = (int32_t)(((int64_t)max_child_move * params->norm_scale) /
		json_max_i32(avg_child_move, 1));
	move_raw = tok->net_move_raw >= 0 ? tok->net_move_raw : -tok->net_move_raw;
	candle_span = (int32_t)(tok->end_candle - tok->start_candle + 1u);
	dp_bias = (tok->class_id == RELATIVE_CLASS_TREND)
		? (-(move_raw * 4) + candle_span * json_max_i32(params->minimum_pips_for_trend, 1) * 3 - params->segment_penalty_lambda)
		: (move_raw * 3 - candle_span * json_max_i32(params->minimum_pips_for_trend, 1) * 10);
	if (append(out,
		out_cap,
		used,
		",\"penalty_trace\":{\"model\":\"layered_waves\",\"chosen_class\":\"%s\",\"chosen_error\":%d,\"second_best_error\":%d,\"dp_bias\":%d,"
		"\"metrics\":{\"child_count\":%u,\"aligned_seq_share\":%d,\"counter_seq_share\":%d,\"aligned_alt_share\":%d,\"counter_alt_share\":%d,\"alternating_share\":%d,"
		"\"seq_count_ratio\":%d,\"alt_count_ratio\":%d,\"directional_agreement\":%d,\"range_variance_ratio\":%d,"
		"\"relative_directional_efficiency_step\":%d,\"relative_directional_efficiency_body\":%d,\"relative_avg_body_efficiency\":%d,"
		"\"relative_overlap_ratio\":%d,\"sign_change_density\":%d,\"relative_retracement_ratio\":%d,\"abs_net_move_raw\":%d},"
		"\"trend_terms\":[",
		wave_token_class_name(tok, WAVE_MODEL_LAYERED),
		tok->fit_error,
		tok->second_best_error,
		dp_bias,
		child_count,
		aligned_seq_share,
		counter_seq_share,
		aligned_alt_share,
		counter_alt_share,
		alternating_share,
		seq_count_ratio,
		alt_count_ratio,
		tok->layered_directional_agreement,
		range_variance_ratio,
		tok->relative_directional_efficiency_step,
		tok->relative_directional_efficiency_body,
		tok->relative_avg_body_efficiency,
		tok->relative_overlap_ratio,
		tok->sign_change_density,
		tok->relative_retracement_ratio,
		move_raw) != 0) {
		return -1;
	}
#define APPEND_TERM(NAME, VALUE, THRESHOLD, WEIGHT, PENALTY, MODE, FIRST) \
	do { \
		if (!(FIRST) && append(out, out_cap, used, ",") != 0) { \
			return -1; \
		} \
		if (append_penalty_term_json(out, out_cap, used, (NAME), (VALUE), (THRESHOLD), (WEIGHT), (PENALTY), (MODE)) != 0) { \
			return -1; \
		} \
		(FIRST) = 0; \
	} while (0)
	APPEND_TERM("aligned_seq_share", aligned_seq_share, 520, 10, 10 * json_penalty_low(aligned_seq_share, 520), "low", first);
	APPEND_TERM("aligned_alt_share", aligned_alt_share, 90, 5, 5 * json_penalty_low(aligned_alt_share, 90), "low", first);
	APPEND_TERM("alternating_share", alternating_share, 520, 6, 6 * json_penalty_high(alternating_share, 520), "high", first);
	APPEND_TERM("counter_seq_share", counter_seq_share, 220, 10, 10 * json_penalty_high(counter_seq_share, 220), "high", first);
	APPEND_TERM("counter_alt_share", counter_alt_share, 260, 8, 8 * json_penalty_high(counter_alt_share, 260), "high", first);
	APPEND_TERM("seq_count_ratio", seq_count_ratio, 560, 7, 7 * json_penalty_low(seq_count_ratio, 560), "low", first);
	APPEND_TERM("directional_agreement", tok->layered_directional_agreement, 760, 6, 6 * json_penalty_low(tok->layered_directional_agreement, 760), "low", first);
	APPEND_TERM("relative_directional_efficiency_step", tok->relative_directional_efficiency_step, params->relative_trend_der_step_min - 60, 5, 5 * json_penalty_low(tok->relative_directional_efficiency_step, params->relative_trend_der_step_min - 60), "low", first);
	APPEND_TERM("relative_directional_efficiency_body", tok->relative_directional_efficiency_body, params->relative_trend_der_body_min - 60, 5, 5 * json_penalty_low(tok->relative_directional_efficiency_body, params->relative_trend_der_body_min - 60), "low", first);
	APPEND_TERM("relative_avg_body_efficiency", tok->relative_avg_body_efficiency, params->relative_trend_body_eff_min - 40, 3, 3 * json_penalty_low(tok->relative_avg_body_efficiency, params->relative_trend_body_eff_min - 40), "low", first);
	APPEND_TERM("relative_overlap_ratio", tok->relative_overlap_ratio, trend_overlap_ceiling, 4, 4 * json_penalty_high(tok->relative_overlap_ratio, trend_overlap_ceiling), "high", first);
	APPEND_TERM("sign_change_density", tok->sign_change_density, trend_sign_ceiling, 4, 4 * json_penalty_high(tok->sign_change_density, trend_sign_ceiling), "high", first);
	APPEND_TERM("relative_retracement_ratio", tok->relative_retracement_ratio, trend_retrace_ceiling, 4, 4 * json_penalty_high(tok->relative_retracement_ratio, trend_retrace_ceiling), "high", first);
	APPEND_TERM("abs_net_move_raw", move_raw, drift_floor, 3, 3 * json_penalty_low(move_raw, drift_floor), "low", first);
	if (append(out, out_cap, used, "],\"range_terms\":[") != 0) {
		return -1;
	}
	first = 1;
	APPEND_TERM("alternating_share", alternating_share, 420, 10, 10 * json_penalty_low(alternating_share, 420), "low", first);
	APPEND_TERM("aligned_seq_share", aligned_seq_share, 480, 8, 8 * json_penalty_high(aligned_seq_share, 480), "high", first);
	APPEND_TERM("aligned_alt_share", aligned_alt_share, 420, 6, 6 * json_penalty_high(aligned_alt_share, 420), "high", first);
	APPEND_TERM("counter_seq_share", counter_seq_share, 280, 7, 7 * json_penalty_high(counter_seq_share, 280), "high", first);
	APPEND_TERM("counter_alt_share", counter_alt_share, 420, 4, 4 * json_penalty_high(counter_alt_share, 420), "high", first);
	APPEND_TERM("alt_count_ratio", alt_count_ratio, 520, 6, 6 * json_penalty_low(alt_count_ratio, 520), "low", first);
	APPEND_TERM("seq_count_ratio", seq_count_ratio, 560, 5, 5 * json_penalty_high(seq_count_ratio, 560), "high", first);
	APPEND_TERM("range_variance_ratio", range_variance_ratio, 1800, 10, 10 * json_penalty_high(range_variance_ratio, 1800), "high", first);
	APPEND_TERM("directional_agreement", tok->layered_directional_agreement, 660, 5, 5 * json_penalty_high(tok->layered_directional_agreement, 660), "high", first);
	APPEND_TERM("relative_directional_efficiency_step", tok->relative_directional_efficiency_step, params->relative_range_der_step_max + 80, 4, 4 * json_penalty_high(tok->relative_directional_efficiency_step, params->relative_range_der_step_max + 80), "high", first);
	APPEND_TERM("relative_directional_efficiency_body", tok->relative_directional_efficiency_body, params->relative_range_der_step_max + 60, 4, 4 * json_penalty_high(tok->relative_directional_efficiency_body, params->relative_range_der_step_max + 60), "high", first);
	APPEND_TERM("sign_change_density", tok->sign_change_density, range_sign_floor, 4, 4 * json_penalty_low(tok->sign_change_density, range_sign_floor), "low", first);
	APPEND_TERM("relative_overlap_ratio", tok->relative_overlap_ratio, range_overlap_floor, 4, 4 * json_penalty_low(tok->relative_overlap_ratio, range_overlap_floor), "low", first);
	APPEND_TERM("abs_net_move_raw", move_raw, drift_floor * 3, 4, 4 * json_penalty_high(move_raw, drift_floor * 3), "high", first);
#undef APPEND_TERM
	if (append(out, out_cap, used, "]}") != 0) {
		return -1;
	}
	return 0;
}

int json_encode_wave_list(uint32_t dataset_id, uint32_t start, uint32_t count, const char *class_name, const char *order_by, char *out, size_t out_cap)
{
	WaveSummary summary;
	DatasetRecord *rec;
	int class_filter;
	int primitive_filter;
	int use_primitive_filter = 0;
	int sort_by_fit_error = 0;
	uint32_t primitive_consecutive_count = 0;
	uint32_t primitive_alternating_count = 0;
	uint32_t primitive_separator_count = 0;
	uint32_t written = 0;
	uint32_t total = 0;
	uint32_t i;
	size_t used = 0;
	WaveTokenSortEntry *sort_entries = NULL;

	if (core_get_wave_summary(dataset_id, &summary) != 0) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	class_filter = wave_class_id_from_name(class_name);
	primitive_filter = wave_primitive_filter_id_from_name(class_name);
	if (summary.params.wave_model == WAVE_MODEL_PRIMITIVE && primitive_filter >= -1) {
		use_primitive_filter = 1;
	}
	if (order_by != NULL && strcmp(order_by, "fit_error") == 0) {
		sort_by_fit_error = 1;
	}
	if (rec == NULL ||
		(summary.params.wave_model == WAVE_MODEL_PRIMITIVE && primitive_filter == -2) ||
		(summary.params.wave_model != WAVE_MODEL_PRIMITIVE && class_filter == -2)) {
		return -1;
	}
	if (append(out,
		out_cap,
		&used,
		"{\"dataset_id\":%u,\"start\":%u,\"selected_class\":\"%s\",\"count\":",
		dataset_id,
		start,
		(class_name != NULL && class_name[0] != '\0') ? class_name : "all") != 0) {
		return -1;
	}
	for (i = 0; i < rec->waves.token_count; ++i) {
		const WaveToken *tok = &rec->waves.tokens[i];
		const char *primitive_type = wave_primitive_type_name(tok);

		if (strcmp(primitive_type, "sequential_string") == 0) {
			primitive_consecutive_count += 1u;
		} else if (strcmp(primitive_type, "alternating_string") == 0) {
			primitive_alternating_count += 1u;
		} else if (strcmp(primitive_type, "separator_candle") == 0) {
			primitive_separator_count += 1u;
		}

		if (use_primitive_filter) {
			if (primitive_filter == 0 && strcmp(primitive_type, "sequential_string") != 0) {
				continue;
			}
			if (primitive_filter == 1 && strcmp(primitive_type, "alternating_string") != 0) {
				continue;
			}
			if (primitive_filter == 2 && strcmp(primitive_type, "separator_candle") != 0) {
				continue;
			}
		} else {
			if (class_filter >= 0 && (int)tok->class_id != class_filter) {
				continue;
			}
		}
		if (total >= start && written < count) {
			written += 1u;
		}
		total += 1u;
	}
	if (summary.params.wave_model == WAVE_MODEL_LAYERED) {
		primitive_consecutive_count = 0u;
		primitive_alternating_count = 0u;
		primitive_separator_count = 0u;
		for (i = 0; i < rec->waves.layer1_token_count; ++i) {
			const WaveToken *tok = &rec->waves.layer1_tokens[i];
			const char *primitive_type = wave_primitive_type_name(tok);

			if (strcmp(primitive_type, "sequential_string") == 0) {
				primitive_consecutive_count += 1u;
			} else if (strcmp(primitive_type, "alternating_string") == 0) {
				primitive_alternating_count += 1u;
			} else if (strcmp(primitive_type, "separator_candle") == 0) {
				primitive_separator_count += 1u;
			}
		}
	}
	if (sort_by_fit_error && total > 0u) {
		uint32_t filtered_index = 0u;

		sort_entries = (WaveTokenSortEntry *)malloc((size_t)total * sizeof(*sort_entries));
		if (sort_entries == NULL) {
			return -1;
		}
		for (i = 0; i < rec->waves.token_count; ++i) {
			const WaveToken *tok = &rec->waves.tokens[i];
			const char *primitive_type = wave_primitive_type_name(tok);

			if (use_primitive_filter) {
				if (primitive_filter == 0 && strcmp(primitive_type, "sequential_string") != 0) {
					continue;
				}
				if (primitive_filter == 1 && strcmp(primitive_type, "alternating_string") != 0) {
					continue;
				}
				if (primitive_filter == 2 && strcmp(primitive_type, "separator_candle") != 0) {
					continue;
				}
			} else {
				if (class_filter >= 0 && (int)tok->class_id != class_filter) {
					continue;
				}
			}
			sort_entries[filtered_index].token_index = i;
			sort_entries[filtered_index].fit_error = tok->fit_error;
			filtered_index += 1u;
		}
		qsort(sort_entries, total, sizeof(*sort_entries), compare_wave_token_fit_error_asc);
	}
	if (append(out,
		out_cap,
		&used,
		"%u,\"total\":%u,\"total_fit_error\":%lld,",
		written,
		total,
		(long long)summary.total_fit_error) != 0) {
		return -1;
	}
	if (summary.params.wave_model == WAVE_MODEL_PRIMITIVE) {
		if (append(out,
			out_cap,
			&used,
			"\"primitive_counts\":{\"sequential_string\":%u,\"alternating_string\":%u,\"separator_candle\":%u},",
			primitive_consecutive_count,
			primitive_alternating_count,
			primitive_separator_count) != 0) {
			return -1;
		}
	} else if (summary.params.wave_model == WAVE_MODEL_LAYERED) {
		if (append(out,
			out_cap,
			&used,
			"\"class_counts\":{\"trend\":%u,\"range\":%u},\"layer1_counts\":{\"sequential_string\":%u,\"alternating_string\":%u},",
			summary.class_counts[RELATIVE_CLASS_TREND],
			summary.class_counts[RELATIVE_CLASS_RANGE],
			primitive_consecutive_count,
			primitive_alternating_count) != 0) {
			return -1;
		}
	} else if (summary.params.wave_model == WAVE_MODEL_RELATIVE ||
		summary.params.wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		summary.params.wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		summary.params.wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) {
		if (summary.params.wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
			summary.params.wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) {
			if (append(out,
				out_cap,
				&used,
				"\"class_counts\":{\"range\":%u,\"seed_pass1\":%u,\"seed_pass2\":%u},",
				summary.class_counts[RELATIVE_CLASS_RANGE],
				summary.kpi.seeded_pass1_trend_count,
				summary.kpi.seeded_pass2_trend_count) != 0) {
				return -1;
			}
		} else {
			if (append(out,
				out_cap,
				&used,
				"\"class_counts\":{\"trend\":%u,\"range\":%u},",
				summary.class_counts[RELATIVE_CLASS_TREND],
				summary.class_counts[RELATIVE_CLASS_RANGE]) != 0) {
				return -1;
			}
		}
	} else {
		if (append(out,
			out_cap,
			&used,
			"\"class_counts\":{\"strong_trend\":%u,\"weak_trend\":%u,\"retracement\":%u,\"alternating_chop\":%u,\"mixed_compression\":%u,\"volatile_expansion\":%u,\"v_shaped_reversal\":%u},",
			summary.class_counts[WAVE_CLASS_STRONG_TREND],
			summary.class_counts[WAVE_CLASS_WEAK_TREND],
			summary.class_counts[WAVE_CLASS_RETRACEMENT],
			summary.class_counts[WAVE_CLASS_ALTERNATING_CHOP],
			summary.class_counts[WAVE_CLASS_MIXED_COMPRESSION],
			summary.class_counts[WAVE_CLASS_VOLATILE_EXPANSION],
			summary.class_counts[WAVE_CLASS_V_SHAPED_REVERSAL]) != 0) {
			return -1;
		}
	}
	if (append(out,
		out_cap,
		&used,
		"\"params\":{\"wave_model\":\"%s\",\"lmin_candles\":%u,\"lmax_candles\":%u,\"regime_context_window\":%u,\"regime_context_mode\":%u,\"minimum_pips_for_trend\":%d,\"segment_penalty_lambda\":%d,\"trend_priority_mode\":%d,\"retrace_break_threshold\":%d,\"trend_split_gain_min\":%d,\"compression_min_duration\":%d,\"expansion_min_duration\":%d,\"v_reversal_min_trend_strength\":%d,\"v_reversal_max_bridge_chop\":%d,\"v_reversal_max_duration\":%d,\"relative_trend_retracement_max\":%d,\"relative_strong_trend_retracement_max\":%d,\"relative_trend_der_step_min\":%d,\"relative_trend_der_body_min\":%d,\"relative_trend_body_eff_min\":%d,\"relative_trend_overlap_max\":%d,\"relative_trend_sign_flip_max\":%d,\"relative_trend_wick_bias_min\":%d,\"relative_range_der_step_max\":%d,\"relative_range_body_eff_max\":%d,\"relative_range_overlap_min\":%d,\"relative_range_sign_flip_min\":%d},\"kpi\":{\"avg_ambiguity_margin\":%.3f,\"low_margin_token_count\":%u,\"avg_token_length\":%.3f,\"tokens_per_1000_candles\":%.3f,\"pct_one_candle_tokens\":%.3f,\"pct_two_candle_tokens\":%.3f,\"pct_near_lmax_tokens\":%.3f,\"avg_long_token_error\":%.3f,\"avg_boundary_confidence\":%.3f,\"low_boundary_confidence_count\":%u,\"weak_fit_count\":%u,\"rule_retracement_edge_count\":%u,\"rule_retracement_context_count\":%u,\"rule_retracement_direction_count\":%u,\"rule_retracement_chain_count\":%u,\"same_direction_trend_merged_count\":%u,\"same_direction_trend_kept_count\":%u,\"opposite_direction_v_reversal_merged_count\":%u,\"opposite_direction_v_reversal_rejected_count\":%u,\"retracement_threshold_rejected_count\":%u,\"avg_relative_directional_efficiency_step\":%.3f,\"avg_relative_directional_efficiency_body\":%.3f,\"avg_relative_retracement_ratio\":%.3f,\"avg_relative_overlap_ratio\":%.3f,\"avg_relative_body_efficiency\":%.3f,\"relative_trend_count\":%u,\"relative_range_count\":%u,\"relative_trend_low_efficiency_count\":%u,\"relative_trend_high_retracement_count\":%u,\"relative_range_updrift_count\":%u,\"relative_range_downdrift_count\":%u},\"waves\":[",
		wave_model_name(summary.params.wave_model),
		summary.params.lmin_candles,
		summary.params.lmax_candles,
		summary.params.regime_context_window,
		summary.params.regime_context_mode,
		summary.params.minimum_pips_for_trend,
		summary.params.segment_penalty_lambda,
		summary.params.trend_priority_mode,
		summary.params.retrace_break_threshold,
		summary.params.trend_split_gain_min,
		summary.params.compression_min_duration,
		summary.params.expansion_min_duration,
		summary.params.v_reversal_min_trend_strength,
		summary.params.v_reversal_max_bridge_chop,
		summary.params.v_reversal_max_duration,
		summary.params.relative_trend_retracement_max,
		summary.params.relative_strong_trend_retracement_max,
		summary.params.relative_trend_der_step_min,
		summary.params.relative_trend_der_body_min,
		summary.params.relative_trend_body_eff_min,
		summary.params.relative_trend_overlap_max,
		summary.params.relative_trend_sign_flip_max,
		summary.params.relative_trend_wick_bias_min,
		summary.params.relative_range_der_step_max,
		summary.params.relative_range_body_eff_max,
		summary.params.relative_range_overlap_min,
		summary.params.relative_range_sign_flip_min,
		summary.kpi.avg_ambiguity_margin,
		summary.kpi.low_margin_token_count,
		summary.kpi.avg_token_length,
		summary.kpi.tokens_per_1000_candles,
		summary.kpi.pct_one_candle_tokens,
		summary.kpi.pct_two_candle_tokens,
		summary.kpi.pct_near_lmax_tokens,
		summary.kpi.avg_long_token_error,
		summary.kpi.avg_boundary_confidence,
		summary.kpi.low_boundary_confidence_count,
		summary.kpi.weak_fit_count,
		summary.kpi.rule_retracement_edge_count,
		summary.kpi.rule_retracement_context_count,
		summary.kpi.rule_retracement_direction_count,
		summary.kpi.rule_retracement_chain_count,
		summary.kpi.same_direction_trend_merged_count,
		summary.kpi.same_direction_trend_kept_count,
		summary.kpi.opposite_direction_v_reversal_merged_count,
		summary.kpi.opposite_direction_v_reversal_rejected_count,
		summary.kpi.retracement_threshold_rejected_count,
		summary.kpi.avg_relative_directional_efficiency_step,
		summary.kpi.avg_relative_directional_efficiency_body,
		summary.kpi.avg_relative_retracement_ratio,
		summary.kpi.avg_relative_overlap_ratio,
		summary.kpi.avg_relative_body_efficiency,
		summary.kpi.relative_trend_count,
		summary.kpi.relative_range_count,
		summary.kpi.relative_trend_low_efficiency_count,
		summary.kpi.relative_trend_high_retracement_count,
		summary.kpi.relative_range_updrift_count,
		summary.kpi.relative_range_downdrift_count) != 0) {
		return -1;
	}
	{
		uint32_t filtered_index = 0;
		uint32_t emitted = 0;

		for (i = 0; i < rec->waves.token_count; ++i) {
			uint32_t token_index = i;
			const WaveToken *tok;
			const char *tok_class_name;
			const char *primitive_type;

			if (sort_by_fit_error) {
				if (i >= total) {
					break;
				}
				token_index = sort_entries[i].token_index;
			}
			tok = &rec->waves.tokens[token_index];
			tok_class_name = wave_token_class_name(tok, summary.params.wave_model);
			primitive_type = wave_primitive_type_name(tok);
			if (!sort_by_fit_error) {
				if (use_primitive_filter) {
					if (primitive_filter == 0 && strcmp(primitive_type, "sequential_string") != 0) {
						continue;
					}
					if (primitive_filter == 1 && strcmp(primitive_type, "alternating_string") != 0) {
						continue;
					}
					if (primitive_filter == 2 && strcmp(primitive_type, "separator_candle") != 0) {
						continue;
					}
				} else {
					if (class_filter >= 0 && (int)tok->class_id != class_filter) {
						continue;
					}
				}
			}
			if (filtered_index < start) {
				filtered_index += 1u;
				continue;
			}
			if (emitted >= count) {
				break;
			}
			if (emitted > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"token_index\":%u,\"start_candle\":%u,\"end_candle\":%u,\"seed_start_candle\":%u,\"seed_end_candle\":%u,\"seed_pass\":%u,\"layered_child_start_index\":%u,\"layered_child_end_index\":%u,\"layered_sequential_count\":%u,\"layered_alternating_count\":%u,\"layered_sequential_candles\":%u,\"layered_alternating_candles\":%u,\"layered_sequential_net_move_sum_raw\":%d,\"layered_alternating_net_move_sum_raw\":%d,\"layered_directional_agreement\":%d,\"class_id\":%u,\"class_name\":\"%s\",\"primitive_type\":\"%s\",\"primitive_direction\":\"%s\",\"fit_error\":%d,\"second_best_error\":%d,\"ambiguity_margin\":%d,\"duration_candles\":%u,\"net_move_raw\":%d,\"retracement_ratio\":%d,\"sign_change_density\":%d,\"directional_purity_body\":%d,\"range_growth_ratio\":%d,\"context_range\":%d,\"primitive_consecutive_ratio\":%d,\"primitive_alternating_ratio\":%d,\"primitive_max_consecutive_run\":%d,\"primitive_max_alternating_run\":%d,\"primitive_alternating_pair_count\":%d,\"primitive_counter_body_ratio\":%d,\"primitive_minority_body_ratio\":%d,\"relative_net_move_raw\":%d,\"relative_abs_body_sum_raw\":%d,\"relative_abs_step_sum_raw\":%d,\"relative_directional_efficiency_body\":%d,\"relative_directional_efficiency_step\":%d,\"relative_avg_body_efficiency\":%d,\"relative_body_direction_share\":%d,\"relative_upper_wick_sum_raw\":%d,\"relative_lower_wick_sum_raw\":%d,\"relative_total_wick_sum_raw\":%d,\"relative_wick_to_body_ratio\":%d,\"relative_wick_bias_up\":%d,\"relative_wick_bias_down\":%d,\"relative_wick_bias_aligned\":%d,\"relative_max_pullback_raw\":%d,\"relative_impulse_leg_raw\":%d,\"relative_retracement_ratio\":%d,\"relative_body_overlap_sum_raw\":%d,\"relative_overlap_ratio\":%d,\"relative_range_growth_ratio\":%d,\"relative_body_growth_ratio\":%d,\"relative_trend_quality_raw\":%d,\"relative_trend_quality_bucket\":%d,\"knn_class_agreement\":%d,\"knn_neighbor_purity\":%d,\"knn_distance_continuation\":%d,\"knn_distance_exhaustion\":%d,\"knn_majority_label\":%d,\"knn_margin\":%d,\"dominant_direction\":%d,\"boundary_confidence\":%d,\"weak_fit\":%d,\"debug_flags\":%u,\"debug_reduction_flags\":%u,\"debug_reduction_count\":%u,\"separator_debug\":\"%s\"",
				token_index,
				tok->start_candle,
				tok->end_candle,
				tok->seed_start_candle,
				tok->seed_end_candle,
				tok->seed_pass,
				tok->layered_child_start_index,
				tok->layered_child_end_index,
				tok->layered_sequential_count,
				tok->layered_alternating_count,
				tok->layered_sequential_candles,
				tok->layered_alternating_candles,
				tok->layered_sequential_net_move_sum_raw,
				tok->layered_alternating_net_move_sum_raw,
				tok->layered_directional_agreement,
				tok->class_id,
				tok_class_name,
				primitive_type,
				wave_primitive_direction_name(tok),
				tok->fit_error,
				tok->second_best_error,
				tok->ambiguity_margin,
				tok->duration_candles,
				tok->net_move_raw,
				tok->retracement_ratio,
				tok->sign_change_density,
				tok->directional_purity_body,
				tok->range_growth_ratio,
				tok->context_range,
				tok->primitive_consecutive_ratio,
				tok->primitive_alternating_ratio,
				tok->primitive_max_consecutive_run,
				tok->primitive_max_alternating_run,
				tok->primitive_alternating_pair_count,
				tok->primitive_counter_body_ratio,
				tok->primitive_minority_body_ratio,
				tok->relative_net_move_raw,
				tok->relative_abs_body_sum_raw,
				tok->relative_abs_step_sum_raw,
				tok->relative_directional_efficiency_body,
				tok->relative_directional_efficiency_step,
				tok->relative_avg_body_efficiency,
				tok->relative_body_direction_share,
				tok->relative_upper_wick_sum_raw,
				tok->relative_lower_wick_sum_raw,
				tok->relative_total_wick_sum_raw,
				tok->relative_wick_to_body_ratio,
				tok->relative_wick_bias_up,
				tok->relative_wick_bias_down,
				tok->relative_wick_bias_aligned,
				tok->relative_max_pullback_raw,
				tok->relative_impulse_leg_raw,
				tok->relative_retracement_ratio,
				tok->relative_body_overlap_sum_raw,
				tok->relative_overlap_ratio,
				tok->relative_range_growth_ratio,
				tok->relative_body_growth_ratio,
				tok->relative_trend_quality_raw,
				tok->relative_trend_quality_bucket,
				tok->knn_class_agreement,
				tok->knn_neighbor_purity,
				tok->knn_distance_continuation,
				tok->knn_distance_exhaustion,
				tok->knn_majority_label,
				tok->knn_margin,
				tok->dominant_direction,
				tok->boundary_confidence,
				tok->weak_fit,
				tok->debug_flags,
				tok->debug_reduction_flags,
				tok->debug_reduction_count,
				wave_token_debug_marker(tok)) != 0) {
				return -1;
			}
			if (summary.params.wave_model == WAVE_MODEL_LAYERED &&
				rec->waves.layer1_tokens != NULL &&
				tok->layered_child_start_index <= tok->layered_child_end_index &&
				tok->layered_child_end_index < rec->waves.layer1_token_count) {
				uint32_t child_index;

				if (append(out, out_cap, &used, ",\"child_strings\":[") != 0) {
					return -1;
				}
				for (child_index = tok->layered_child_start_index; child_index <= tok->layered_child_end_index; ++child_index) {
					const WaveToken *child = &rec->waves.layer1_tokens[child_index];

					if (child_index > tok->layered_child_start_index &&
						append(out, out_cap, &used, ",") != 0) {
						return -1;
					}
					if (append(out,
						out_cap,
						&used,
						"{\"child_index\":%u,\"start_candle\":%u,\"end_candle\":%u,\"primitive_type\":\"%s\",\"primitive_direction\":\"%s\",\"duration_candles\":%u,\"net_move_raw\":%d,\"primitive_consecutive_ratio\":%d,\"primitive_alternating_ratio\":%d,\"primitive_alternating_pair_count\":%d}",
						child_index,
						child->start_candle,
						child->end_candle,
						wave_primitive_type_name(child),
						wave_primitive_direction_name(child),
						child->duration_candles,
						child->net_move_raw,
						child->primitive_consecutive_ratio,
						child->primitive_alternating_ratio,
						child->primitive_alternating_pair_count) != 0) {
						return -1;
					}
				}
				if (append(out, out_cap, &used, "]") != 0) {
					return -1;
				}
			}
			if (append_layered_penalty_trace_json(out, out_cap, &used, &summary, rec, tok) != 0) {
				return -1;
			}
			if (append(out, out_cap, &used, "}") != 0) {
				return -1;
			}
			filtered_index += 1u;
			emitted += 1u;
		}
	}
	free(sort_entries);
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}
